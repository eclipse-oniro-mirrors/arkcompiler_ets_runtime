/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ECMASCRIPT_JS_RUNTIME_OPTIONS_H_
#define ECMASCRIPT_JS_RUNTIME_OPTIONS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ecmascript/compiler/bc_call_signature.h"
#include "ecmascript/mem/mem_common.h"

// namespace panda {
namespace panda::ecmascript {
using arg_list_t = std::vector<std::string>;
enum ArkProperties {
    DEFAULT = -1,
    OPTIONAL_LOG = 1,
    GC_STATS_PRINT = 1 << 1,
    PARALLEL_GC = 1 << 2,
    CONCURRENT_MARK = 1 << 3,
    CONCURRENT_SWEEP = 1 << 4,
    THREAD_CHECK = 1 << 5,
    ENABLE_ARKTOOLS = 1 << 6,
    ENABLE_SNAPSHOT_SERIALIZE = 1 << 7,
    ENABLE_SNAPSHOT_DESERIALIZE = 1 << 8,
    EXCEPTION_BACKTRACE = 1 << 9,
};

// asm interpreter control parsed option
struct AsmInterParsedOption {
    int handleStart {-1};
    int handleEnd {-1};
    bool enableAsm {false};
};

extern const std::string PUBLIC_API COMMON_HELP_HEAD_MSG;
extern const std::string PUBLIC_API STUB_HELP_HEAD_MSG;
extern const std::string PUBLIC_API HELP_OPTION_MSG;
extern const std::string PUBLIC_API HELP_TAIL_MSG;

enum CommandValues {
    OPTION_DEFAULT,
    OPTION_ENABLE_ARK_TOOLS,
    OPTION_ENABLE_CPUPROFILER,
    OPTION_STUB_FILE,
    OPTION_ENABLE_FORCE_GC,
    OPTION_FORCE_FULL_GC,
    OPTION_ARK_PROPERTIES,
    OPTION_GC_THREADNUM,
    OPTION_LONG_PAUSE_TIME,
    OPTION_AOT_FILE,
    OPTION_TARGET_TRIPLE,
    OPTION_ASM_OPT_LEVEL,
    OPTION_RELOCATION_MODE,
    OPTION_MAX_NONMOVABLE_SPACE_CAPACITY,
    OPTION_ENABLE_ASM_INTERPRETER,
    OPTION_ASM_OPCODE_DISABLE_RANGE,
    OPTION_SERIALIZER_BUFFER_SIZE_LIMIT,
    OPTION_HEAP_SIZE_LIMIT,
    OPTION_ENABLE_IC,
    OPTION_SNAPSHOT_FILE,
    OPTION_FRAMEWORK_ABC_FILE,
    OPTION_ICU_DATA_PATH,
    OPTION_STARTUP_TIME,
    OPTION_COMPILER_LOG_OPT,
    OPTION_COMPILER_LOG_METHODS,
    OPTION_ENABLE_RUNTIME_STAT,
    OPTION_ASSERT_TYPES,
    OPTION_PRINT_ANY_TYPES,
    OPTION_COMPILER_LOG_SNAPSHOT,
    OPTION_COMPILER_LOG_TIME,
    OPTION_IS_WORKER,
    OPTION_BUILTINS_DTS,
    OPTION_ENABLE_BC_TRACE,
    OPTION_ENABLE_DEOPT_TRACE,
    OPTION_LOG_LEVEL,
    OPTION_LOG_DEBUG,
    OPTION_LOG_INFO,
    OPTION_LOG_WARNING,
    OPTION_LOG_ERROR,
    OPTION_LOG_FATAL,
    OPTION_LOG_COMPONENTS,
    OPTION_MAX_AOTMETHODSIZE,
    OPTION_ENTRY_POINT,
    OPTION_MERGE_ABC,
    OPTION_ENABLE_TYPE_LOWERING,
    OPTION_HELP,
    OPTION_PGO_PROFILER_PATH,
    OPTION_PGO_HOTNESS_THRESHOLD,
    OPTION_ENABLE_PGO_PROFILER,
    OPTION_OPTIONS,
    OPTION_PRINT_EXECUTE_TIME
};

class PUBLIC_API JSRuntimeOptions {
public:
    explicit JSRuntimeOptions() {}
    ~JSRuntimeOptions() = default;
    DEFAULT_COPY_SEMANTIC(JSRuntimeOptions);
    DEFAULT_MOVE_SEMANTIC(JSRuntimeOptions);

    bool ParseCommand(const int argc, const char **argv);
    bool SetDefaultValue(char* argv);

    bool EnableArkTools() const
    {
        return (enableArkTools_) ||
            ((static_cast<uint32_t>(arkProperties_) & ArkProperties::ENABLE_ARKTOOLS) != 0);
    }

    void SetEnableArkTools(bool value) {
        enableArkTools_ = value;
    }

    bool WasSetEnableArkTools() const
    {
        return WasOptionSet(OPTION_ENABLE_ARK_TOOLS);
    }

    bool IsEnableRuntimeStat() const
    {
        return enableRuntimeStat_;
    }

    void SetEnableRuntimeStat(bool value)
    {
        enableRuntimeStat_ = value;
    }

    bool WasSetEnableRuntimeStat() const
    {
        return WasOptionSet(OPTION_ENABLE_RUNTIME_STAT);
    }

    std::string GetStubFile() const
    {
        return stubFile_;
    }

    void SetStubFile(std::string value)
    {
        stubFile_ = std::move(value);
    }

    bool WasStubFileSet() const
    {
        return WasOptionSet(OPTION_STUB_FILE);
    }

    void SetEnableAOT(bool value)
    {
        enableAOT_ = value;
    }

    bool GetEnableAOT() const
    {
        return enableAOT_;
    }

    std::string GetAOTOutputFile() const
    {
        return aotOutputFile_;
    }

    void SetAOTOutputFile(std::string value)
    {
        aotOutputFile_ = std::move(value);
    }

    bool WasAOTOutputFileSet() const
    {
        return WasOptionSet(OPTION_AOT_FILE);
    }

    std::string GetTargetTriple() const
    {
        return targetTriple_;
    }

    void SetTargetTriple(std::string value)
    {
        targetTriple_ = std::move(value);
    }

    size_t GetOptLevel() const
    {
        return asmOptLevel_;
    }

    void SetOptLevel(size_t value)
    {
        asmOptLevel_ = value;
    }

    size_t GetRelocMode() const
    {
        return relocationMode_;
    }

    void SetRelocMode(size_t value)
    {
        relocationMode_ = value;
    }

    bool EnableForceGC() const
    {
        return enableForceGc_;
    }

    void SetEnableForceGC(bool value)
    {
        enableForceGc_ = value;
    }

    bool ForceFullGC() const
    {
        return forceFullGc_;
    }

    void SetForceFullGC(bool value)
    {
        forceFullGc_ = value;
    }

    bool EnableCpuProfiler() const
    {
        return enableCpuprofiler_;
    }

    void SetEnableCpuprofiler(bool value)
    {
        enableCpuprofiler_ = value;
    }

    void SetGcThreadNum(size_t num)
    {
        gcThreadNum_ = num;
    }

    size_t GetGcThreadNum() const
    {
        return gcThreadNum_;
    }

    void SetLongPauseTime(size_t time)
    {
        longPauseTime_ = time;
    }

    size_t GetLongPauseTime() const
    {
        return longPauseTime_;
    }

    void SetArkProperties(int prop)
    {
        if (prop != ArkProperties::DEFAULT) {
            arkProperties_ = prop;
        }
    }

    int GetDefaultProperties()
    {
        return ArkProperties::PARALLEL_GC | ArkProperties::CONCURRENT_MARK | ArkProperties::CONCURRENT_SWEEP
            | ArkProperties::ENABLE_ARKTOOLS;
    }

    int GetArkProperties()
    {
        return arkProperties_;
    }

    bool EnableOptionalLog() const
    {
        return (static_cast<uint32_t>(arkProperties_) & ArkProperties::OPTIONAL_LOG) != 0;
    }

    bool EnableGCStatsPrint() const
    {
        return (static_cast<uint32_t>(arkProperties_) & ArkProperties::GC_STATS_PRINT) != 0;
    }

    bool EnableParallelGC() const
    {
        return (static_cast<uint32_t>(arkProperties_) & ArkProperties::PARALLEL_GC) != 0;
    }

    bool EnableConcurrentMark() const
    {
        return (static_cast<uint32_t>(arkProperties_) & ArkProperties::CONCURRENT_MARK) != 0;
    }

    bool EnableExceptionBacktrace() const
    {
        return (static_cast<uint32_t>(arkProperties_) & ArkProperties::EXCEPTION_BACKTRACE) != 0;
    }

    bool EnableConcurrentSweep() const
    {
        return (static_cast<uint32_t>(arkProperties_) & ArkProperties::CONCURRENT_SWEEP) != 0;
    }

    bool EnableThreadCheck() const
    {
        return (static_cast<uint32_t>(arkProperties_) & ArkProperties::THREAD_CHECK) != 0;
    }

    bool EnableSnapshotSerialize() const
    {
        return (static_cast<uint32_t>(arkProperties_) & ArkProperties::ENABLE_SNAPSHOT_SERIALIZE) != 0;
    }

    bool EnableSnapshotDeserialize() const
    {
        if (WIN_OR_MAC_OR_IOS_PLATFORM) {
            return false;
        }

        return (static_cast<uint32_t>(arkProperties_) & ArkProperties::ENABLE_SNAPSHOT_DESERIALIZE) != 0;
    }

    bool WasSetMaxNonmovableSpaceCapacity() const
    {
        return WasOptionSet(OPTION_MAX_NONMOVABLE_SPACE_CAPACITY);
    }

    size_t MaxNonmovableSpaceCapacity() const
    {
        return maxNonmovableSpaceCapacity_;
    }

    void SetMaxNonmovableSpaceCapacity(uint32_t value)
    {
        maxNonmovableSpaceCapacity_ = value;
    }

    void SetEnableAsmInterpreter(bool value)
    {
        enableAsmInterpreter_ = value;
    }

    bool GetEnableAsmInterpreter() const
    {
        return enableAsmInterpreter_;
    }

    void SetAsmOpcodeDisableRange(std::string value)
    {
        asmOpcodeDisableRange_ = std::move(value);
    }

    void ParseAsmInterOption()
    {
        asmInterParsedOption_.enableAsm = enableAsmInterpreter_;
        std::string strAsmOpcodeDisableRange = asmOpcodeDisableRange_;
        if (strAsmOpcodeDisableRange.empty()) {
            return;
        }

        // asm interpreter handle disable range
        size_t pos = strAsmOpcodeDisableRange.find(",");
        if (pos != std::string::npos) {
            std::string strStart = strAsmOpcodeDisableRange.substr(0, pos);
            std::string strEnd = strAsmOpcodeDisableRange.substr(pos + 1);
            int start =  strStart.empty() ? 0 : std::stoi(strStart);
            int end = strEnd.empty() ? kungfu::BYTECODE_STUB_END_ID : std::stoi(strEnd);
            if (start >= 0 && start < kungfu::BytecodeStubCSigns::NUM_OF_ALL_NORMAL_STUBS
                && end >= 0 && end < kungfu::BytecodeStubCSigns::NUM_OF_ALL_NORMAL_STUBS
                && start <= end) {
                asmInterParsedOption_.handleStart = start;
                asmInterParsedOption_.handleEnd = end;
            }
        }
    }

    AsmInterParsedOption GetAsmInterParsedOption() const
    {
        return asmInterParsedOption_;
    }

    std::string GetCompilerLogOption() const
    {
        return compilerLogOpt_;
    }

    void SetCompilerLogOption(std::string value)
    {
        compilerLogOpt_ = std::move(value);
    }

    bool WasSetCompilerLogOption() const
    {
        return 1ULL << static_cast<uint64_t>(OPTION_COMPILER_LOG_OPT) & wasSet_ &&
            GetCompilerLogOption().find("none") == std::string::npos;
    }

    std::string GetMethodsListForLog() const
    {
        return compilerLogMethods_;
    }

    void SetMethodsListForLog(std::string value)
    {
        compilerLogMethods_ = std::move(value);
    }

    bool WasSetMethodsListForLog() const
    {
        return 1ULL << static_cast<uint64_t>(OPTION_COMPILER_LOG_METHODS) & wasSet_ &&
            GetCompilerLogOption().find("none") == std::string::npos &&
            GetCompilerLogOption().find("all") == std::string::npos;
    }

    void SetCompilerLogSnapshot(bool value)
    {
        compilerLogSnapshot_ = value;
    }

    bool IsEnableCompilerLogSnapshot() const
    {
        return compilerLogSnapshot_;
    }

    bool WasSetCompilerLogSnapshot() const
    {
        return WasOptionSet(OPTION_COMPILER_LOG_SNAPSHOT);
    }

    void SetCompilerLogTime(bool value)
    {
        compilerLogTime_ = value;
    }

    bool IsEnableCompilerLogTime() const
    {
        return compilerLogTime_;
    }

    bool WasSetCompilerLogTime() const
    {
        return WasOptionSet(OPTION_COMPILER_LOG_TIME);
    }

    uint64_t GetSerializerBufferSizeLimit() const
    {
        return serializerBufferSizeLimit_;
    }

    void SetSerializerBufferSizeLimit(uint64_t value)
    {
        serializerBufferSizeLimit_ = value;
    }

    uint32_t GetHeapSizeLimit() const
    {
        return heapSizeLimit_;
    }

    void SetHeapSizeLimit(uint32_t value)
    {
        heapSizeLimit_ = value;
    }

    bool WasSetHeapSizeLimit() const
    {
        return WasOptionSet(OPTION_HEAP_SIZE_LIMIT);
    }

    void SetIsWorker(bool isWorker)
    {
        isWorker_ = isWorker;
    }

    bool IsWorker() const
    {
        return isWorker_;
    }

    bool EnableIC() const
    {
        return enableIC_;
    }

    void SetEnableIC(bool value)
    {
        enableIC_ = value;
    }

    bool WasSetEnableIC() const
    {
        return WasOptionSet(OPTION_ENABLE_IC);
    }

    std::string GetSnapshotFile() const
    {
        return snapshotFile_;
    }

    void SetSnapshotFile(std::string value)
    {
        snapshotFile_ = std::move(value);
    }

    bool WasSetSnapshotFile() const
    {
        return WasOptionSet(OPTION_SNAPSHOT_FILE);
    }

    std::string GetFrameworkAbcFile() const
    {
        return frameworkAbcFile_;
    }

    void SetFrameworkAbcFile(std::string value)
    {
        frameworkAbcFile_ = std::move(value);
    }

    bool WasSetFrameworkAbcFile() const
    {
        return WasOptionSet(OPTION_FRAMEWORK_ABC_FILE);
    }

    std::string GetIcuDataPath() const
    {
        return icuDataPath_;
    }

    void SetIcuDataPath(std::string value)
    {
        icuDataPath_ = std::move(value);
    }

    bool WasSetIcuDataPath() const
    {
        return WasOptionSet(OPTION_ICU_DATA_PATH);
    }

    bool IsStartupTime() const
    {
        return startupTime_;
    }

    void SetStartupTime(bool value)
    {
        startupTime_ = value;
    }

    bool WasSetStartupTime() const
    {
        return WasOptionSet(OPTION_STARTUP_TIME);
    }

    bool AssertTypes() const
    {
        return assertTypes_;
    }

    void SetAssertTypes(bool value)
    {
        assertTypes_ = value;
    }

    bool PrintAnyTypes() const
    {
        return printAnyTypes_;
    }

    void SetPrintAnyTypes(bool value)
    {
        printAnyTypes_ = value;
    }

    void SetBuiltinsDTS(std::string value)
    {
        builtinsDTS_ = std::move(value);
    }

    bool WasSetBuiltinsDTS() const
    {
        return WasOptionSet(OPTION_BUILTINS_DTS);
    }

    std::string GetBuiltinsDTS() const
    {
        return builtinsDTS_;
    }

    void SetEnableByteCodeTrace(bool value)
    {
        enablebcTrace_ = value;
    }

    bool IsEnableByteCodeTrace() const
    {
        return enablebcTrace_;
    }

    bool WasSetEnableByteCodeTrace() const
    {
        return WasOptionSet(OPTION_ENABLE_BC_TRACE);
    }


    std::string GetLogLevel() const
    {
        return logLevel_;
    }

    void SetLogLevel(std::string value)
    {
        logLevel_ = std::move(value);
    }

    bool WasSetLogLevel() const
    {
        return WasOptionSet(OPTION_LOG_LEVEL);
    }

    arg_list_t GetLogComponents() const
    {
        return logComponents_;
    }

    void SetLogComponents(arg_list_t value)
    {
        logComponents_ = std::move(value);
    }

    bool WasSetLogComponents() const
    {
        return WasOptionSet(OPTION_LOG_COMPONENTS);
    }

    arg_list_t GetLogDebug() const
    {
        return logDebug_;
    }

    void SetLogDebug(arg_list_t value)
    {
        logDebug_ = std::move(value);
    }

    bool WasSetLogDebug() const
    {
        return WasOptionSet(OPTION_LOG_DEBUG);
    }

    arg_list_t GetLogInfo() const
    {
        return logInfo_;
    }

    void SetLogInfo(arg_list_t value)
    {
        logInfo_ = std::move(value);
    }

    bool WasSetLogInfo() const
    {
        return WasOptionSet(OPTION_LOG_INFO);
    }

    arg_list_t GetLogWarning() const
    {
        return logWarning_;
    }

    void SetLogWarning(arg_list_t value)
    {
        logWarning_ = std::move(value);
    }

    bool WasSetLogWarning() const
    {
        return WasOptionSet(OPTION_LOG_WARNING);
    }

    arg_list_t GetLogError() const
    {
        return logError_;
    }

    void SetLogError(arg_list_t value)
    {
        logError_ = std::move(value);
    }

    bool WasSetLogError() const
    {
        return WasOptionSet(OPTION_LOG_ERROR);
    }

    arg_list_t GetLogFatal() const
    {
        return logFatal_;
    }

    void SetLogFatal(arg_list_t value)
    {
        logFatal_ = std::move(value);
    }

    bool WasSetLogFatal() const
    {
        return WasOptionSet(OPTION_LOG_FATAL);
    }

    size_t GetMaxAotMethodSize() const
    {
        return maxAotMethodSize_;
    }

    void SetMaxAotMethodSize(uint32_t value)
    {
        maxAotMethodSize_ = value;
    }

    std::string GetEntryPoint() const
    {
        return entryPoint_;
    }

    void SetEntryPoint(std::string value)
    {
        entryPoint_ = std::move(value);
    }

    bool WasSetEntryPoint() const
    {
        return WasOptionSet(OPTION_ENTRY_POINT);
    }

    bool GetMergeAbc() const
    {
        return mergeAbc_;
    }

    void SetMergeAbc(bool value)
    {
        mergeAbc_ = value;
    }

    void SetEnablePrintExecuteTime(bool value)
    {
        enablePrintExecuteTime_ = value;
    }

    bool IsEnablePrintExecuteTime()
    {
        return enablePrintExecuteTime_;
    }

    void SetEnablePGOProfiler(bool value)
    {
        enablePGOProfiler_ = value;
    }

    bool IsEnablePGOProfiler() const
    {
        return enablePGOProfiler_;
    }

    uint32_t GetPGOHotnessThreshold() const
    {
        return pgoHotnessThreshold_;
    }

    void SetPGOHotnessThreshold(uint32_t threshold)
    {
        pgoHotnessThreshold_ = threshold;
    }

    std::string GetPGOProfilerPath() const
    {
        return pgoProfilerPath_;
    }

    void SetPGOProfilerPath(std::string value)
    {
        pgoProfilerPath_ = std::move(value);
    }

    void SetEnableTypeLowering(bool value)
    {
        enableTypeLowering_ = value;
    }

    bool IsEnableTypeLowering() const
    {
        return enableTypeLowering_;
    }

    void WasSet(int opt)
    {
        wasSet_ |= 1ULL << static_cast<uint64_t>(opt);
    }

    void SetEnableDeoptTrace(bool value)
    {
        enableDeoptTrace_ = value;
    }

    bool IsEnableDeoptTrace() const
    {
        return enableDeoptTrace_;
    }
private:
    static bool StartsWith(const std::string &haystack, const std::string &needle)
    {
        return std::equal(needle.begin(), needle.end(), haystack.begin());
    }

    bool WasOptionSet(int option) const
    {
        return ((1ULL << static_cast<uint64_t>(option)) & wasSet_) != 0;
    }

    bool ParseBoolParam(bool* argBool);
    bool ParseIntParam(const std::string &option, int* argInt);
    bool ParseUint32Param(const std::string &option, uint32_t *argUInt32);
    bool ParseUint64Param(const std::string &option, uint64_t *argUInt64);
    void ParseListArgParam(const std::string &option, arg_list_t *argListStr, std::string delimiter);

    bool enableArkTools_ {true};
    bool enableCpuprofiler_ {false};
    std::string stubFile_ {"stub.an"};
    bool enableForceGc_ {true};
    bool forceFullGc_ {true};
    int arkProperties_ = GetDefaultProperties();
    uint32_t gcThreadNum_ {7}; // 7: default thread num
    uint32_t longPauseTime_ {40}; // 40: default pause time
    std::string aotOutputFile_ {"aot_file"};
    std::string targetTriple_ {"x86_64-unknown-linux-gnu"};
    uint32_t asmOptLevel_ {3}; // 3: default opt level
    uint32_t relocationMode_ {2}; // 2: default relocation mode
    uint32_t maxNonmovableSpaceCapacity_ {4_MB};
    bool enableAsmInterpreter_ {true};
    std::string asmOpcodeDisableRange_ {""};
    AsmInterParsedOption asmInterParsedOption_;
    uint64_t serializerBufferSizeLimit_ {2_GB};
    uint32_t heapSizeLimit_ {512_MB};
    bool enableIC_ {true};
    std::string snapshotFile_ {"/system/etc/snapshot"};
    std::string frameworkAbcFile_ {"strip.native.min.abc"};
    std::string icuDataPath_ {"default"};
    bool startupTime_ {false};
    std::string compilerLogOpt_ {"none"};
    std::string compilerLogMethods_ {"none"};
    bool compilerLogSnapshot_ {false};
    bool compilerLogTime_ {false};
    bool enableRuntimeStat_ {false};
    bool assertTypes_ {false};
    bool printAnyTypes_ {false};
    bool isWorker_ {false};
    std::string builtinsDTS_ {""};
    bool enablebcTrace_ {false};
    std::string logLevel_ {"error"};
    arg_list_t logDebug_ {{"all"}};
    arg_list_t logInfo_ {{"all"}};
    arg_list_t logWarning_ {{"all"}};
    arg_list_t logError_ {{"all"}};
    arg_list_t logFatal_ {{"all"}};
    arg_list_t logComponents_ {{"all"}};
    bool enableAOT_ {false};
    uint32_t maxAotMethodSize_ {32_KB};
    std::string entryPoint_ {"_GLOBAL::func_main_0"};
    bool mergeAbc_ {false};
    bool enableTypeLowering_ {true};
    uint64_t wasSet_ {0};
    bool enablePrintExecuteTime_ {false};
    bool enablePGOProfiler_ {false};
    uint32_t pgoHotnessThreshold_ {2};
    std::string pgoProfilerPath_ {""};
    bool enableDeoptTrace_ {false};
};
}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_JS_RUNTIME_OPTIONS_H_
