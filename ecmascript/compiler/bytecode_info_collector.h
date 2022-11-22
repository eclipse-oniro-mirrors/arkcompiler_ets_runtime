/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ECMASCRIPT_COMPILER_BYTECODE_INFO_COLLECTOR_H
#define ECMASCRIPT_COMPILER_BYTECODE_INFO_COLLECTOR_H

#include "ecmascript/dfx/pgo_profiler/pgo_profiler_loader.h"
#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/jspandafile/method_literal.h"
#include "ecmascript/jspandafile/bytecode_inst/old_instruction.h"
#include "libpandafile/bytecode_instruction-inl.h"

namespace panda::ecmascript::kungfu {
/*    ts source code
 *    let a:number = 1;
 *    function f() {
 *        let b:number = 1;
 *        function g() {
 *            return a + b;
 *        }
 *        return g();
 *    }
 *
 *                                     The structure of Lexical Environment
 *
 *                                               Lexical Environment             Lexical Environment
 *               Global Environment                 of function f                   of function g
 *              +-------------------+ <----+    +-------------------+ <----+    +-------------------+
 *    null <----|  Outer Reference  |      +----|  Outer Reference  |      +----|  Outer Reference  |
 *              +-------------------+           +-------------------+           +-------------------+
 *              |Environment Recoder|           |Environment Recoder|           |Environment Recoder|
 *              +-------------------+           +-------------------+           +-------------------+
 *
 *    We only record the type of the variable in Environment Recoder.
 *    In the design of the Ark bytecode, if a method does not have any
 *    lex-env variable in its Lexical Environment, then there will be
 *    no EcmaOpcode::NEWLEXENV in it which leads to ARK runtime will
 *    not create a Lexical Environment when the method is executed.
 *    In order to simulate the state of the runtime as much as possible,
 *    a field named 'status' will be added into the class LexEnv to
 *    measure this state. Take the above code as an example, although in
 *    static analysis, we will create LexEnv for each method, only Lexenvs
 *    of global and function f will be created when methods are executed.
 */

enum class LexicalEnvStatus : uint8_t {
    VIRTUAL_LEXENV,
    REALITY_LEXENV
};

struct LexEnv {
    explicit LexEnv(uint32_t methodIdx, uint32_t num, LexicalEnvStatus lexEnvStatus)
        : outmethodId(methodIdx), lexVarTypes(num, GateType::AnyType()),
          status(lexEnvStatus)
    {
    }

    uint32_t outmethodId { 0 };
    std::vector<GateType> lexVarTypes {};
    LexicalEnvStatus status { LexicalEnvStatus::VIRTUAL_LEXENV };
};

// each method in the abc file corresponds to one MethodInfo and
// methods with the same instructions share one common MethodPcInfo
struct MethodPcInfo {
    std::vector<const uint8_t*> pcOffsets {};
    uint32_t methodsSize {0};
};

struct MethodInfo {
    explicit MethodInfo(size_t methodIdx, size_t methodPcIdx, uint32_t outMethodIdx, uint32_t num = 0,
                        LexicalEnvStatus lexEnvStatus = LexicalEnvStatus::VIRTUAL_LEXENV)
        : methodInfoIndex(methodIdx), methodPcInfoIndex(methodPcIdx),
          lexEnv(outMethodIdx, num, lexEnvStatus)
    {
    }

    // used to record the index of the current MethodInfo to speed up the lookup of lexEnv
    size_t methodInfoIndex { 0 };
    // used to obtain MethodPcInfo from the vector methodPcInfos of struct BCInfo
    size_t methodPcInfoIndex { 0 };
    std::vector<uint32_t> innerMethods {};
    LexEnv lexEnv;
};

enum class ConstantPoolIndexType : uint8_t {
    STRING,
    METHOD,
    CLASS_LITERAL,
    OBJECT_LITERAL,
    ARRAY_LITERAL,
};

class ConstantPoolIndexInfo {
public:
    const std::set<uint32_t>& GetStringOrMethodIndexSet(ConstantPoolIndexType type)
    {
        switch (type) {
            case ConstantPoolIndexType::STRING: {
                return stringIndex_;
            }
            case ConstantPoolIndexType::METHOD: {
                return methodIndex_;
            }
            default:
                UNREACHABLE();
        }
    }

    const std::set<std::pair<uint32_t, uint32_t>>& GetLiteralIndexSet(ConstantPoolIndexType type)
    {
        switch (type) {
            case ConstantPoolIndexType::CLASS_LITERAL: {
                return classLiteralIndex_;
            }
            case ConstantPoolIndexType::OBJECT_LITERAL: {
                return objectLiteralIndex_;
            }
            case ConstantPoolIndexType::ARRAY_LITERAL: {
                return arrayLiteralIndex_;
            }
            default:
                UNREACHABLE();
        }
    }

    void AddConstantPoolIndex(ConstantPoolIndexType type, uint32_t index, uint32_t methodOffset = 0);

private:
    std::set<uint32_t> stringIndex_ {};
    std::set<uint32_t> methodIndex_ {};

    // literal need to record methodOffset (constantpool index, methodOffset)
    std::set<std::pair<uint32_t, uint32_t>> classLiteralIndex_ {};
    std::set<std::pair<uint32_t, uint32_t>> objectLiteralIndex_ {};
    std::set<std::pair<uint32_t, uint32_t>> arrayLiteralIndex_ {};
};

class BCInfo {
public:
    explicit BCInfo(PGOProfilerLoader &profilerLoader, size_t maxAotMethodSize)
        : pfLoader_(profilerLoader), maxMethodSize_(maxAotMethodSize)
    {
    }

    std::vector<uint32_t>& GetMainMethodIndexes()
    {
        return mainMethodIndexes_;
    }

    std::vector<CString>& GetRecordNames()
    {
        return recordNames_;
    }

    std::vector<MethodPcInfo>& GetMethodPcInfos()
    {
        return methodPcInfos_;
    }

    std::unordered_map<uint32_t, MethodInfo>& GetMethodList()
    {
        return methodList_;
    }

    bool IsSkippedMethod(uint32_t methodOffset) const
    {
        if (skippedMethods_.find(methodOffset) == skippedMethods_.end()) {
            return false;
        }
        return true;
    }

    size_t GetSkippedMethodSize() const
    {
        return skippedMethods_.size();
    }

    void AddConstantPoolIndex(ConstantPoolIndexType type, uint32_t index, uint32_t methodOffset = 0)
    {
        cpIndexInfo_.AddConstantPoolIndex(type, index, methodOffset);
    }

    template <class Callback>
    void IterateStringOrMethodIndex(ConstantPoolIndexType type, const Callback &cb)
    {
        const auto &indexSet = cpIndexInfo_.GetStringOrMethodIndexSet(type);
        for (uint32_t index : indexSet) {
            cb(index);
        }
    }

    template <class Callback>
    void IterateLiteralIndex(ConstantPoolIndexType type, const Callback &cb)
    {
        const auto &indexSet = cpIndexInfo_.GetLiteralIndexSet(type);
        for (const auto &item : indexSet) {
            cb(item.first, methodOffsetToRecordName_[item.second]);
        }
    }

    template <class Callback>
    void EnumerateBCInfo(JSPandaFile *jsPandaFile, const Callback &cb)
    {
        for (uint32_t i = 0; i < mainMethodIndexes_.size(); i++) {
            std::queue<uint32_t> methodCompiledOrder;
            methodCompiledOrder.push(mainMethodIndexes_[i]);
            while (!methodCompiledOrder.empty()) {
                auto compilingMethod = methodCompiledOrder.front();
                methodCompiledOrder.pop();
                methodOffsetToRecordName_.emplace(compilingMethod, recordNames_[i]);
                auto &methodInfo = methodList_.at(compilingMethod);
                auto &methodPcInfo = methodPcInfos_[methodInfo.methodPcInfoIndex];
                auto methodLiteral = jsPandaFile->FindMethodLiteral(compilingMethod);
                const std::string methodName(MethodLiteral::GetMethodName(jsPandaFile, methodLiteral->GetMethodId()));
                if (FilterMethod(recordNames_[i], methodLiteral, methodPcInfo)) {
                    skippedMethods_.insert(compilingMethod);
                    LOG_COMPILER(INFO) << " method " << methodName << " has been skipped";
                } else {
                    cb(recordNames_[i], methodName, methodLiteral, compilingMethod,
                       methodPcInfo, methodInfo.methodInfoIndex);
                }
                auto &innerMethods = methodInfo.innerMethods;
                for (auto it : innerMethods) {
                    methodCompiledOrder.push(it);
                }
            }
        }
    }
private:
    bool FilterMethod(const CString &recordName, const MethodLiteral *methodLiteral,
                      const MethodPcInfo &methodPCInfo) const
    {
        if (methodPCInfo.methodsSize > maxMethodSize_ ||
            !pfLoader_.Match(recordName, methodLiteral->GetMethodId())) {
            return true;
        }
        return false;
    }

    std::vector<uint32_t> mainMethodIndexes_ {};
    std::vector<CString> recordNames_ {};
    std::vector<MethodPcInfo> methodPcInfos_ {};
    std::unordered_map<uint32_t, MethodInfo> methodList_ {};
    std::unordered_map<uint32_t, CString> methodOffsetToRecordName_ {};
    std::set<uint32_t> skippedMethods_ {};
    ConstantPoolIndexInfo cpIndexInfo_;
    PGOProfilerLoader &pfLoader_;
    size_t maxMethodSize_;
};

class LexEnvManager {
public:
    explicit LexEnvManager(BCInfo &bcInfo);
    ~LexEnvManager() = default;
    NO_COPY_SEMANTIC(LexEnvManager);
    NO_MOVE_SEMANTIC(LexEnvManager);

    void SetLexEnvElementType(uint32_t methodId, uint32_t level, uint32_t slot, const GateType &type);
    GateType GetLexEnvElementType(uint32_t methodId, uint32_t level, uint32_t slot) const;

private:
    uint32_t GetTargetLexEnv(uint32_t methodId, uint32_t level) const;

    std::vector<LexEnv *> lexEnvs_ {};
};

class BytecodeInfoCollector {
public:
    explicit BytecodeInfoCollector(JSPandaFile *jsPandaFile, PGOProfilerLoader &profilerLoader,
                                   size_t maxAotMethodSize)
        : jsPandaFile_(jsPandaFile), bytecodeInfo_(profilerLoader, maxAotMethodSize)
    {
        ProcessClasses();
    }
    ~BytecodeInfoCollector() = default;
    NO_COPY_SEMANTIC(BytecodeInfoCollector);
    NO_MOVE_SEMANTIC(BytecodeInfoCollector);

    BCInfo &GetBytecodeInfo()
    {
        return bytecodeInfo_;
    }

    bool IsSkippedMethod(uint32_t methodOffset) const
    {
        return bytecodeInfo_.IsSkippedMethod(methodOffset);
    }

    template <class Callback>
    void IterateStringOrMethodIndex(ConstantPoolIndexType type, const Callback &cb)
    {
        bytecodeInfo_.IterateStringOrMethodIndex(type, cb);
    }

    template <class Callback>
    void IterateLiteralIndex(ConstantPoolIndexType type, const Callback &cb)
    {
        bytecodeInfo_.IterateLiteralIndex(type, cb);
    }

private:
    inline size_t GetMethodInfoID()
    {
        return methodInfoIndex_++;
    }

    void AddConstantPoolIndexToBCInfo(ConstantPoolIndexType type, uint32_t index, uint32_t methodOffset = 0)
    {
        bytecodeInfo_.AddConstantPoolIndex(type, index, methodOffset);
    }

    const CString GetEntryFunName(const std::string_view &entryPoint) const;
    void ProcessClasses();
    void CollectMethodPcsFromBC(const uint32_t insSz, const uint8_t *insArr, const MethodLiteral *method);
    void SetMethodPcInfoIndex(uint32_t methodOffset, size_t index);
    void CollectInnerMethods(const MethodLiteral *method, uint32_t innerMethodOffset);
    void CollectInnerMethods(uint32_t methodId, uint32_t innerMethodOffset);
    void CollectInnerMethodsFromLiteral(const MethodLiteral *method, uint64_t index);
    void NewLexEnvWithSize(const MethodLiteral *method, uint64_t numOfLexVars);

    void CollectInnerMethodsFromNewLiteral(const MethodLiteral *method, panda_file::File::EntityId literalId);
    void CollectMethodInfoFromBC(const BytecodeInstruction &bcIns, const MethodLiteral *method);
    void CollectConstantPoolIndexInfoFromBC(const BytecodeInstruction &bcIns, const MethodLiteral *method);

    JSPandaFile *jsPandaFile_ {nullptr};
    BCInfo bytecodeInfo_;
    size_t methodInfoIndex_ {0};
};
}  // namespace panda::ecmascript::kungfu
#endif  // ECMASCRIPT_COMPILER_BYTECODE_INFO_COLLECTOR_H
