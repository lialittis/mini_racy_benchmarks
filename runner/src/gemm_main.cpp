#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "acl/acl.h"
#include "acl/ops/acl_cblas.h"
#include "common.h"

bool g_isDevice = false;

namespace {

struct Options {
    std::string modelDir;
    std::string inputDir;
    std::string outputDir;
    std::string aclConfig;
    int deviceId = 0;
    uint32_t m = 16;
    uint32_t n = 16;
    uint32_t k = 16;
    float alpha = 2.0F;
    float beta = 1.0F;
};

void PrintUsage(const char *program)
{
    std::cout << "Usage: " << program
              << " --model-dir PATH --input-dir PATH --output-dir PATH --acl-config PATH"
              << " [--device ID] [--m N] [--n N] [--k N] [--alpha F] [--beta F]"
              << std::endl;
}

bool ParseUnsigned(const std::string &value, uint32_t &result)
{
    char *end = nullptr;
    errno = 0;
    unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' || parsed == 0 ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    result = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseDevice(const std::string &value, int &result)
{
    uint32_t parsed = 0;
    if (!ParseUnsigned(value, parsed) || parsed > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        if (value == "0") {
            result = 0;
            return true;
        }
        return false;
    }
    result = static_cast<int>(parsed);
    return true;
}

bool ParseFloat(const std::string &value, float &result)
{
    char *end = nullptr;
    errno = 0;
    result = std::strtof(value.c_str(), &end);
    return errno == 0 && end != value.c_str() && *end == '\0';
}

bool ParseOptions(int argc, char **argv, Options &options)
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            std::exit(SUCCESS);
        }
        if (i + 1 >= argc) {
            ERROR_LOG("Missing value for %s", arg.c_str());
            return false;
        }
        std::string value = argv[++i];
        if (arg == "--model-dir") {
            options.modelDir = value;
        } else if (arg == "--input-dir") {
            options.inputDir = value;
        } else if (arg == "--output-dir") {
            options.outputDir = value;
        } else if (arg == "--acl-config") {
            options.aclConfig = value;
        } else if (arg == "--device") {
            if (!ParseDevice(value, options.deviceId)) {
                return false;
            }
        } else if (arg == "--m") {
            if (!ParseUnsigned(value, options.m)) {
                return false;
            }
        } else if (arg == "--n") {
            if (!ParseUnsigned(value, options.n)) {
                return false;
            }
        } else if (arg == "--k") {
            if (!ParseUnsigned(value, options.k)) {
                return false;
            }
        } else if (arg == "--alpha") {
            if (!ParseFloat(value, options.alpha)) {
                return false;
            }
        } else if (arg == "--beta") {
            if (!ParseFloat(value, options.beta)) {
                return false;
            }
        } else {
            ERROR_LOG("Unknown option: %s", arg.c_str());
            return false;
        }
    }
    return !options.modelDir.empty() && !options.inputDir.empty() && !options.outputDir.empty() &&
           !options.aclConfig.empty();
}

bool EnsureOutputDirectory(const std::string &path)
{
    struct stat info {};
    if (stat(path.c_str(), &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    return mkdir(path.c_str(), 0750) == 0;
}

bool LoadInput(const std::string &path, std::vector<aclFloat16> &data)
{
    size_t fileSize = 0;
    char *result = ReadFile(path, fileSize, data.data(), data.size() * sizeof(aclFloat16));
    return result != nullptr && fileSize == data.size() * sizeof(aclFloat16);
}

void FreeDevice(void *&buffer)
{
    if (buffer != nullptr) {
        (void)aclrtFree(buffer);
        buffer = nullptr;
    }
}

bool RunGemm(const Options &options)
{
    std::vector<aclFloat16> matrixA(options.m * options.k);
    std::vector<aclFloat16> matrixB(options.k * options.n);
    std::vector<aclFloat16> matrixC(options.m * options.n);
    if (!LoadInput(options.inputDir + "/input_0.bin", matrixA) ||
        !LoadInput(options.inputDir + "/input_1.bin", matrixB) ||
        !LoadInput(options.inputDir + "/input_2.bin", matrixC)) {
        ERROR_LOG("Load GEMM input files failed");
        return false;
    }

    const size_t sizeA = matrixA.size() * sizeof(aclFloat16);
    const size_t sizeB = matrixB.size() * sizeof(aclFloat16);
    const size_t sizeC = matrixC.size() * sizeof(aclFloat16);
    const size_t scalarSize = sizeof(aclFloat16);
    void *devA = nullptr;
    void *devB = nullptr;
    void *devC = nullptr;
    void *devAlpha = nullptr;
    void *devBeta = nullptr;
    aclrtStream stream = nullptr;
    bool success = false;

    aclFloat16 alpha = aclFloatToFloat16(options.alpha);
    aclFloat16 beta = aclFloatToFloat16(options.beta);
    if (aclrtMalloc(&devA, sizeA, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS ||
        aclrtMalloc(&devB, sizeB, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS ||
        aclrtMalloc(&devC, sizeC, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS ||
        aclrtMalloc(&devAlpha, scalarSize, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS ||
        aclrtMalloc(&devBeta, scalarSize, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS) {
        ERROR_LOG("Allocate GEMM device buffers failed");
        goto cleanup;
    }
    if (aclrtMemcpy(devA, sizeA, matrixA.data(), sizeA, ACL_MEMCPY_HOST_TO_DEVICE) != ACL_SUCCESS ||
        aclrtMemcpy(devB, sizeB, matrixB.data(), sizeB, ACL_MEMCPY_HOST_TO_DEVICE) != ACL_SUCCESS ||
        aclrtMemcpy(devC, sizeC, matrixC.data(), sizeC, ACL_MEMCPY_HOST_TO_DEVICE) != ACL_SUCCESS ||
        aclrtMemcpy(devAlpha, scalarSize, &alpha, scalarSize, ACL_MEMCPY_HOST_TO_DEVICE) != ACL_SUCCESS ||
        aclrtMemcpy(devBeta, scalarSize, &beta, scalarSize, ACL_MEMCPY_HOST_TO_DEVICE) != ACL_SUCCESS) {
        ERROR_LOG("Copy GEMM inputs to device failed");
        goto cleanup;
    }
    if (aclrtCreateStream(&stream) != ACL_SUCCESS) {
        ERROR_LOG("Create GEMM stream failed");
        goto cleanup;
    }
    if (aclblasGemmEx(ACL_TRANS_N, ACL_TRANS_N, ACL_TRANS_N, options.m, options.n, options.k,
                      devAlpha, devA, -1, ACL_FLOAT16, devB, -1, ACL_FLOAT16,
                      devBeta, devC, -1, ACL_FLOAT16, ACL_COMPUTE_HIGH_PRECISION,
                      stream) != ACL_SUCCESS) {
        ERROR_LOG("Launch aclblasGemmEx failed");
        goto cleanup;
    }
    if (aclrtSynchronizeStream(stream) != ACL_SUCCESS ||
        aclrtMemcpy(matrixC.data(), sizeC, devC, sizeC, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        ERROR_LOG("Synchronize GEMM or copy output failed");
        goto cleanup;
    }
    success = WriteFile(options.outputDir + "/output_0.bin", matrixC.data(), sizeC);

cleanup:
    if (stream != nullptr) {
        (void)aclrtDestroyStream(stream);
    }
    FreeDevice(devA);
    FreeDevice(devB);
    FreeDevice(devC);
    FreeDevice(devAlpha);
    FreeDevice(devBeta);
    return success;
}

}  // namespace

int main(int argc, char **argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options)) {
        PrintUsage(argv[0]);
        return FAILED;
    }
    if (!EnsureOutputDirectory(options.outputDir) ||
        aclInit(options.aclConfig.c_str()) != ACL_SUCCESS) {
        ERROR_LOG("Initialize output directory or ACL failed");
        return FAILED;
    }

    int result = FAILED;
    bool deviceOpened = false;
    if (aclrtSetDevice(options.deviceId) != ACL_SUCCESS) {
        ERROR_LOG("Open device failed: %d", options.deviceId);
    } else {
        deviceOpened = true;
        aclrtRunMode runMode;
        if (aclrtGetRunMode(&runMode) == ACL_SUCCESS) {
            g_isDevice = (runMode == ACL_DEVICE);
        }
        if (aclopSetModelDir(options.modelDir.c_str()) != ACL_SUCCESS) {
            ERROR_LOG("Load model directory failed: %s", options.modelDir.c_str());
        } else if (RunGemm(options)) {
            INFO_LOG("Run GEMM successfully");
            result = SUCCESS;
        }
    }

    if (deviceOpened) {
        (void)aclrtResetDevice(options.deviceId);
    }
    if (aclFinalize() != ACL_SUCCESS) {
        result = FAILED;
    }
    return result;
}
