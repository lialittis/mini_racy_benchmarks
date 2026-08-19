#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <sys/stat.h>

#include "acl/acl.h"
#include "common.h"
#include "op_runner.h"

bool g_isDevice = false;

namespace {

struct Options {
    std::string modelDir;
    std::string inputDir;
    std::string outputDir;
    std::string aclConfig;
    int deviceId = 0;
    bool verboseData = false;
};

void PrintUsage(const char *program)
{
    std::cout << "Usage: " << program << " --model-dir PATH --input-dir PATH --output-dir PATH "
              << "--acl-config PATH [--device ID] [--verbose-data]" << std::endl;
}

bool ParseDeviceId(const std::string &value, int &deviceId)
{
    char *end = nullptr;
    errno = 0;
    long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' || parsed < 0 ||
        parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    deviceId = static_cast<int>(parsed);
    return true;
}

bool ParseOptions(int argc, char **argv, Options &options)
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--verbose-data") {
            options.verboseData = true;
            continue;
        }
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
            if (!ParseDeviceId(value, options.deviceId)) {
                ERROR_LOG("Invalid device id: %s", value.c_str());
                return false;
            }
        } else {
            ERROR_LOG("Unknown option: %s", arg.c_str());
            return false;
        }
    }

    if (options.modelDir.empty() || options.inputDir.empty() || options.outputDir.empty() ||
        options.aclConfig.empty()) {
        ERROR_LOG("model-dir, input-dir, output-dir, and acl-config are required");
        return false;
    }
    return true;
}

bool EnsureOutputDirectory(const std::string &path)
{
    struct stat info {};
    if (stat(path.c_str(), &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    if (mkdir(path.c_str(), 0750) != 0) {
        ERROR_LOG("Create output directory failed: %s", path.c_str());
        return false;
    }
    return true;
}

bool SetInputData(OpRunner &runner, const Options &options)
{
    for (size_t i = 0; i < runner.NumInputs(); ++i) {
        size_t fileSize = 0;
        std::string filePath = options.inputDir + "/input_" + std::to_string(i) + ".bin";
        char *fileData = ReadFile(filePath, fileSize, runner.GetInputBuffer<void>(i), runner.GetInputSize(i));
        if (fileData == nullptr || fileSize != runner.GetInputSize(i)) {
            ERROR_LOG("Read input[%zu] failed or input size mismatched: %s", i, filePath.c_str());
            return false;
        }
        INFO_LOG("Loaded input[%zu] from %s", i, filePath.c_str());
        if (options.verboseData) {
            runner.PrintInput(i);
        }
    }
    return true;
}

bool ProcessOutputData(OpRunner &runner, const Options &options)
{
    for (size_t i = 0; i < runner.NumOutputs(); ++i) {
        if (options.verboseData) {
            runner.PrintOutput(i);
        }
        std::string filePath = options.outputDir + "/output_" + std::to_string(i) + ".bin";
        if (!WriteFile(filePath, runner.GetOutputBuffer<void>(i), runner.GetOutputSize(i))) {
            ERROR_LOG("Write output[%zu] failed: %s", i, filePath.c_str());
            return false;
        }
        INFO_LOG("Wrote output[%zu] to %s", i, filePath.c_str());
    }
    return true;
}

bool RunMatmul(const Options &options)
{
    std::vector<int64_t> shapeA {16, 64};
    std::vector<int64_t> shapeB {64, 1024};
    std::vector<int64_t> shapeC {16, 1024};
    OperatorDesc opDesc("MatMul");
    opDesc.AddInputTensorDesc(ACL_FLOAT16, shapeA.size(), shapeA.data(), ACL_FORMAT_ND);
    opDesc.AddInputTensorDesc(ACL_FLOAT16, shapeB.size(), shapeB.data(), ACL_FORMAT_ND);
    opDesc.AddOutputTensorDesc(ACL_FLOAT16, shapeC.size(), shapeC.data(), ACL_FORMAT_ND);

    OpRunner opRunner(&opDesc);
    if (!opRunner.Init()) {
        ERROR_LOG("Initialize OpRunner failed");
        return false;
    }
    return SetInputData(opRunner, options) && opRunner.RunOp() && ProcessOutputData(opRunner, options);
}

}  // namespace

int main(int argc, char **argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options)) {
        PrintUsage(argv[0]);
        return FAILED;
    }
    if (!EnsureOutputDirectory(options.outputDir)) {
        return FAILED;
    }
    if (aclInit(options.aclConfig.c_str()) != ACL_SUCCESS) {
        ERROR_LOG("Initialize ACL failed");
        return FAILED;
    }

    int result = FAILED;
    bool deviceOpened = false;
    if (aclopSetModelDir(options.modelDir.c_str()) != ACL_SUCCESS) {
        ERROR_LOG("Load model directory failed: %s", options.modelDir.c_str());
    } else if (aclrtSetDevice(options.deviceId) != ACL_SUCCESS) {
        ERROR_LOG("Open device failed: %d", options.deviceId);
    } else {
        deviceOpened = true;
        aclrtRunMode runMode;
        if (aclrtGetRunMode(&runMode) != ACL_SUCCESS) {
            ERROR_LOG("Get ACL run mode failed");
        } else {
            g_isDevice = (runMode == ACL_DEVICE);
            INFO_LOG("Running MatMul on device %d with models from %s", options.deviceId,
                     options.modelDir.c_str());
            if (RunMatmul(options)) {
                INFO_LOG("Run MatMul successfully");
                result = SUCCESS;
            } else {
                ERROR_LOG("Run MatMul failed");
            }
        }
    }

    if (deviceOpened) {
        (void)aclrtResetDevice(options.deviceId);
    }
    if (aclFinalize() != ACL_SUCCESS) {
        ERROR_LOG("Finalize ACL failed");
        result = FAILED;
    }
    return result;
}
