#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "acl/acl.h"
#include "common.h"
#include "op_runner.h"

bool g_isDevice = false;

namespace {

struct TensorSpec {
    aclDataType dataType;
    std::vector<int64_t> shape;
};

struct Options {
    std::string modelDir;
    std::string inputDir;
    std::string outputDir;
    std::string aclConfig;
    std::string operatorType;
    std::vector<TensorSpec> inputs;
    std::vector<TensorSpec> outputs;
    int deviceId = 0;
    bool verboseData = false;
};

void PrintUsage(const char *program)
{
    std::cout << "Usage: " << program
              << " --model-dir PATH --input-dir PATH --output-dir PATH --acl-config PATH"
              << " --operator TYPE --input-spec DTYPE:DIMS [--input-spec ...]"
              << " --output-spec DTYPE:DIMS [--output-spec ...] [--device ID] [--verbose-data]"
              << std::endl;
}

bool ParseNonNegativeInt(const std::string &value, int &result)
{
    char *end = nullptr;
    errno = 0;
    long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' || parsed < 0 ||
        parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    result = static_cast<int>(parsed);
    return true;
}

bool ParseDataType(const std::string &value, aclDataType &dataType)
{
    if (value == "float16") {
        dataType = ACL_FLOAT16;
    } else if (value == "float32") {
        dataType = ACL_FLOAT;
    } else if (value == "int32") {
        dataType = ACL_INT32;
    } else if (value == "int8") {
        dataType = ACL_INT8;
    } else {
        return false;
    }
    return true;
}

bool ParseTensorSpec(const std::string &value, TensorSpec &spec)
{
    const size_t separator = value.find(':');
    if (separator == std::string::npos || !ParseDataType(value.substr(0, separator), spec.dataType)) {
        return false;
    }

    std::stringstream dimensions(value.substr(separator + 1));
    std::string token;
    while (std::getline(dimensions, token, ',')) {
        int parsed = 0;
        if (!ParseNonNegativeInt(token, parsed) || parsed == 0) {
            return false;
        }
        spec.shape.push_back(parsed);
    }
    return !spec.shape.empty();
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
        } else if (arg == "--operator") {
            options.operatorType = value;
        } else if (arg == "--input-spec" || arg == "--output-spec") {
            TensorSpec spec;
            if (!ParseTensorSpec(value, spec)) {
                ERROR_LOG("Invalid tensor spec: %s", value.c_str());
                return false;
            }
            (arg == "--input-spec" ? options.inputs : options.outputs).push_back(spec);
        } else if (arg == "--device") {
            if (!ParseNonNegativeInt(value, options.deviceId)) {
                ERROR_LOG("Invalid device id: %s", value.c_str());
                return false;
            }
        } else {
            ERROR_LOG("Unknown option: %s", arg.c_str());
            return false;
        }
    }

    if (options.modelDir.empty() || options.inputDir.empty() || options.outputDir.empty() ||
        options.aclConfig.empty() || options.operatorType.empty() || options.inputs.empty() ||
        options.outputs.empty()) {
        ERROR_LOG("model/input/output directories, ACL config, operator and tensor specs are required");
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

bool RunOperator(const Options &options)
{
    OperatorDesc opDesc(options.operatorType);
    for (const TensorSpec &spec : options.inputs) {
        opDesc.AddInputTensorDesc(spec.dataType, spec.shape.size(), spec.shape.data(), ACL_FORMAT_ND);
    }
    for (const TensorSpec &spec : options.outputs) {
        opDesc.AddOutputTensorDesc(spec.dataType, spec.shape.size(), spec.shape.data(), ACL_FORMAT_ND);
    }

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
            INFO_LOG("Running %s on device %d with models from %s", options.operatorType.c_str(),
                     options.deviceId, options.modelDir.c_str());
            if (RunOperator(options)) {
                INFO_LOG("Run %s successfully", options.operatorType.c_str());
                result = SUCCESS;
            } else {
                ERROR_LOG("Run %s failed", options.operatorType.c_str());
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
