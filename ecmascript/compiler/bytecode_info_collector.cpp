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

#include "ecmascript/compiler/bytecode_info_collector.h"

#include "ecmascript/interpreter/interpreter-inl.h"
#include "ecmascript/module/module_path_helper.h"
#include "ecmascript/pgo_profiler/pgo_profiler_decoder.h"
#include "libpandafile/code_data_accessor.h"

namespace panda::ecmascript::kungfu {
template<class T, class... Args>
static T *InitializeMemory(T *mem, Args... args)
{
    return new (mem) T(std::forward<Args>(args)...);
}

BytecodeInfoCollector::BytecodeInfoCollector(CompilationEnv *env, JSPandaFile *jsPandaFile,
                                             PGOProfilerDecoder &pfDecoder,
                                             size_t maxAotMethodSize)
    : compilationEnv_(env),
      jsPandaFile_(jsPandaFile),
      bytecodeInfo_(maxAotMethodSize),
      pfDecoder_(pfDecoder),
      snapshotCPData_(new SnapshotConstantPoolData(env->GetEcmaVM(), jsPandaFile, &pfDecoder))
{
    ASSERT(env->IsAotCompiler());
    ProcessClasses();
}

BytecodeInfoCollector::BytecodeInfoCollector(CompilationEnv *env, JSPandaFile *jsPandaFile,
                                             PGOProfilerDecoder &pfDecoder)
    : compilationEnv_(env),
      jsPandaFile_(jsPandaFile),
      // refactor: jit max method size
      bytecodeInfo_(env->GetJSOptions().GetMaxAotMethodSize()),
      pfDecoder_(pfDecoder),
      snapshotCPData_(nullptr) // jit no need
{
    ASSERT(env->IsJitCompiler());
    ProcessMethod();
}

void BytecodeInfoCollector::ProcessClasses()
{
    ASSERT(jsPandaFile_ != nullptr && jsPandaFile_->GetMethodLiterals() != nullptr);
    MethodLiteral *methods = jsPandaFile_->GetMethodLiterals();
    const panda_file::File *pf = jsPandaFile_->GetPandaFile();
    size_t methodIdx = 0;
    std::map<uint32_t, std::pair<size_t, uint32_t>> processedMethod;
    Span<const uint32_t> classIndexes = jsPandaFile_->GetClasses();

    auto &recordNames = bytecodeInfo_.GetRecordNames();
    auto &methodPcInfos = bytecodeInfo_.GetMethodPcInfos();
    std::vector<panda_file::File::EntityId> methodIndexes;
    std::vector<panda_file::File::EntityId> classConstructIndexes;
    for (const uint32_t index : classIndexes) {
        panda_file::File::EntityId classId(index);
        if (jsPandaFile_->IsExternal(classId)) {
            continue;
        }
        panda_file::ClassDataAccessor cda(*pf, classId);
        CString desc = utf::Mutf8AsCString(cda.GetDescriptor());
        const CString recordName = JSPandaFile::ParseEntryPoint(desc);
        cda.EnumerateMethods([this, methods, &methodIdx, pf, &processedMethod,
            &recordNames, &methodPcInfos, &recordName,
            &methodIndexes, &classConstructIndexes] (panda_file::MethodDataAccessor &mda) {
            auto methodId = mda.GetMethodId();
            methodIndexes.emplace_back(methodId);

            // Generate all constpool
            compilationEnv_->FindOrCreateConstPool(jsPandaFile_, methodId);

            auto methodOffset = methodId.GetOffset();
            CString name = reinterpret_cast<const char *>(jsPandaFile_->GetStringData(mda.GetNameId()).data);
            if (JSPandaFile::IsEntryOrPatch(name)) {
                jsPandaFile_->UpdateMainMethodIndex(methodOffset, recordName);
                recordNames.emplace_back(recordName);
            }

            MethodLiteral *methodLiteral = methods + (methodIdx++);
            InitializeMemory(methodLiteral, methodId);
            methodLiteral->Initialize(jsPandaFile_);

            ASSERT(jsPandaFile_->IsNewVersion());
            panda_file::IndexAccessor indexAccessor(*pf, methodId);
            panda_file::FunctionKind funcKind = indexAccessor.GetFunctionKind();
            FunctionKind kind = JSPandaFile::GetFunctionKind(funcKind);
            methodLiteral->SetFunctionKind(kind);

            auto codeId = mda.GetCodeId();
            ASSERT(codeId.has_value());
            panda_file::CodeDataAccessor codeDataAccessor(*pf, codeId.value());
            uint32_t codeSize = codeDataAccessor.GetCodeSize();
            const uint8_t *insns = codeDataAccessor.GetInstructions();
            auto it = processedMethod.find(methodOffset);
            if (it == processedMethod.end()) {
                CollectMethodPcsFromBC(codeSize, insns, methodLiteral,
                    recordName, methodOffset, classConstructIndexes);
                processedMethod[methodOffset] = std::make_pair(methodPcInfos.size() - 1, methodOffset);
            }

            SetMethodPcInfoIndex(methodOffset, processedMethod[methodOffset]);
            jsPandaFile_->SetMethodLiteralToMap(methodLiteral);
            pfDecoder_.MatchAndMarkMethod(jsPandaFile_, recordName, name.c_str(), methodId);
        });
    }
    // class Construct need to use new target, can not fastcall
    for (auto index : classConstructIndexes) {
        MethodLiteral *method = jsPandaFile_->GetMethodLiteralByIndex(index.GetOffset());
        if (method != nullptr) {
            method->SetFunctionKind(FunctionKind::CLASS_CONSTRUCTOR);
        }
    }
    RearrangeInnerMethods();
    LOG_COMPILER(INFO) << "Total number of methods in file: "
                       << jsPandaFile_->GetJSPandaFileDesc()
                       << " is: "
                       << methodIdx;
}

void BytecodeInfoCollector::ProcessMethod()
{
    auto &recordNames = bytecodeInfo_.GetRecordNames();
    auto &methodPcInfos = bytecodeInfo_.GetMethodPcInfos();
    MethodLiteral *methodLiteral = compilationEnv_->GetMethodLiteral();

    const panda_file::File *pf = jsPandaFile_->GetPandaFile();
    panda_file::File::EntityId methodIdx = methodLiteral->GetMethodId();
    panda_file::MethodDataAccessor mda(*pf, methodIdx);
    const CString recordName = jsPandaFile_->GetRecordNameWithBundlePack(methodIdx);
    recordNames.emplace_back(recordName);
    auto methodId = mda.GetMethodId();

    ASSERT(jsPandaFile_->IsNewVersion());

    auto methodOffset = methodId.GetOffset();
    auto codeId = mda.GetCodeId();
    ASSERT(codeId.has_value());
    panda_file::CodeDataAccessor codeDataAccessor(*pf, codeId.value());
    uint32_t codeSize = codeDataAccessor.GetCodeSize();
    const uint8_t *insns = codeDataAccessor.GetInstructions();

    std::map<uint32_t, std::pair<size_t, uint32_t>> processedMethod;
    std::vector<panda_file::File::EntityId> classConstructIndexes;

    CollectMethodPcsFromBC(codeSize, insns, methodLiteral,
        recordName, methodOffset, classConstructIndexes);
    processedMethod[methodOffset] = std::make_pair(methodPcInfos.size() - 1, methodOffset);
    SetMethodPcInfoIndex(methodOffset, processedMethod[methodOffset]);
}

void BytecodeInfoCollector::CollectMethodPcsFromBC(const uint32_t insSz, const uint8_t *insArr,
    MethodLiteral *method, const CString &recordName, uint32_t methodOffset,
    std::vector<panda_file::File::EntityId> &classConstructIndexes)
{
    auto bcIns = BytecodeInst(insArr);
    auto bcInsLast = bcIns.JumpTo(insSz);
    int32_t bcIndex = 0;
    auto &methodPcInfos = bytecodeInfo_.GetMethodPcInfos();
    methodPcInfos.emplace_back(MethodPcInfo { {}, insSz });
    auto &pcOffsets = methodPcInfos.back().pcOffsets;
    const uint8_t *curPc = bcIns.GetAddress();
    bool canFastCall = true;
    bool noGC = true;
    bool debuggerStmt = false;

    while (bcIns.GetAddress() != bcInsLast.GetAddress()) {
        bool fastCallFlag = true;
        CollectMethodInfoFromBC(bcIns, method, bcIndex, classConstructIndexes, &fastCallFlag);
        if (!fastCallFlag) {
            canFastCall = false;
        }
        if (snapshotCPData_ != nullptr) {
            snapshotCPData_->Record(bcIns, bcIndex, recordName, method);
        }
        pgoBCInfo_.Record(bcIns, bcIndex, recordName, method);
        if (noGC && !bytecodes_.GetBytecodeMetaData(curPc).IsNoGC()) {
            noGC = false;
        }
        if (!debuggerStmt && bytecodes_.GetBytecodeMetaData(curPc).HasDebuggerStmt()) {
            debuggerStmt = true;
        }
        curPc = bcIns.GetAddress();
        auto nextInst = bcIns.GetNext();
        bcIns = nextInst;
        pcOffsets.emplace_back(curPc);
        bcIndex++;
    }
    bytecodeInfo_.SetMethodOffsetToFastCallInfo(methodOffset, canFastCall, noGC);
    method->SetIsFastCall(canFastCall);
    method->SetNoGCBit(noGC);
    method->SetHasDebuggerStmtBit(debuggerStmt);
}

void BytecodeInfoCollector::SetMethodPcInfoIndex(uint32_t methodOffset,
                                                 const std::pair<size_t, uint32_t> &processedMethodInfo)
{
    auto processedMethodPcInfoIndex = processedMethodInfo.first;
    auto &methodList = bytecodeInfo_.GetMethodList();

    auto iter = methodList.find(methodOffset);
    if (iter != methodList.end()) {
        MethodInfo &methodInfo = iter->second;
        methodInfo.SetMethodPcInfoIndex(processedMethodPcInfoIndex);
        return;
    }
    MethodInfo info(GetMethodInfoID(), processedMethodPcInfoIndex,
        MethodInfo::DEFAULT_OUTMETHOD_OFFSET);
    methodList.emplace(methodOffset, info);
}

void BytecodeInfoCollector::CollectInnerMethods(const MethodLiteral *method,
                                                uint32_t innerMethodOffset,
                                                bool isConstructor)
{
    auto methodId = method->GetMethodId().GetOffset();
    CollectInnerMethods(methodId, innerMethodOffset, isConstructor);
}

void BytecodeInfoCollector::CollectInnerMethods(uint32_t methodId, uint32_t innerMethodOffset, bool isConstructor)
{
    auto &methodList = bytecodeInfo_.GetMethodList();
    uint32_t methodInfoId = 0;
    auto methodIter = methodList.find(methodId);
    if (methodIter != methodList.end()) {
        MethodInfo &methodInfo = methodIter->second;
        methodInfoId = methodInfo.GetMethodInfoIndex();
        methodInfo.AddInnerMethod(innerMethodOffset, isConstructor);
    } else {
        methodInfoId = GetMethodInfoID();
        MethodInfo info(methodInfoId, 0);
        methodList.emplace(methodId, info);
        methodList.at(methodId).AddInnerMethod(innerMethodOffset, isConstructor);
    }

    auto innerMethodIter = methodList.find(innerMethodOffset);
    if (innerMethodIter != methodList.end()) {
        innerMethodIter->second.SetOutMethodId(methodInfoId);
        innerMethodIter->second.SetOutMethodOffset(methodId);
        return;
    }
    MethodInfo innerInfo(GetMethodInfoID(), 0, methodInfoId, methodId);
    methodList.emplace(innerMethodOffset, innerInfo);
}

void BytecodeInfoCollector::CollectInnerMethodsFromLiteral(const MethodLiteral *method, uint64_t index)
{
    std::vector<uint32_t> methodOffsets;
    LiteralDataExtractor::GetMethodOffsets(jsPandaFile_, index, methodOffsets);
    for (auto methodOffset : methodOffsets) {
        CollectInnerMethods(method, methodOffset);
    }
}

void BytecodeInfoCollector::CollectInnerMethodsFromNewLiteral(const MethodLiteral *method,
                                                              panda_file::File::EntityId literalId)
{
    std::vector<uint32_t> methodOffsets;
    LiteralDataExtractor::GetMethodOffsets(jsPandaFile_, literalId, methodOffsets);
    for (auto methodOffset : methodOffsets) {
        CollectInnerMethods(method, methodOffset);
    }
}

void BytecodeInfoCollector::CollectMethodInfoFromBC(const BytecodeInstruction &bcIns, const MethodLiteral *method,
    int32_t bcIndex, std::vector<panda_file::File::EntityId> &classConstructIndexes, bool *canFastCall)
{
    if (!(bcIns.HasFlag(BytecodeInstruction::Flags::STRING_ID) &&
        BytecodeInstruction::HasId(BytecodeInstruction::GetFormat(bcIns.GetOpcode()), 0))) {
        BytecodeInstruction::Opcode opcode = static_cast<BytecodeInstruction::Opcode>(bcIns.GetOpcode());
        switch (opcode) {
            uint32_t methodId;
            case BytecodeInstruction::Opcode::DEFINEFUNC_IMM8_ID16_IMM8:
            case BytecodeInstruction::Opcode::DEFINEFUNC_IMM16_ID16_IMM8: {
                methodId = jsPandaFile_->ResolveMethodIndex(method->GetMethodId(),
                    static_cast<uint16_t>(bcIns.GetId().AsRawValue())).GetOffset();
                CollectInnerMethods(method, methodId);
                break;
            }
            case BytecodeInstruction::Opcode::DEFINEMETHOD_IMM8_ID16_IMM8:
            case BytecodeInstruction::Opcode::DEFINEMETHOD_IMM16_ID16_IMM8: {
                methodId = jsPandaFile_->ResolveMethodIndex(method->GetMethodId(),
                    static_cast<uint16_t>(bcIns.GetId().AsRawValue())).GetOffset();
                CollectInnerMethods(method, methodId);
                break;
            }
            case BytecodeInstruction::Opcode::DEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8:{
                auto entityId = jsPandaFile_->ResolveMethodIndex(method->GetMethodId(),
                    (bcIns.GetId <BytecodeInstruction::Format::IMM8_ID16_ID16_IMM16_V8, 0>()).AsRawValue());
                classConstructIndexes.emplace_back(entityId);
                classDefBCIndexes_.insert(bcIndex);
                methodId = entityId.GetOffset();
                CollectInnerMethods(method, methodId, true);
                auto literalId = jsPandaFile_->ResolveMethodIndex(method->GetMethodId(),
                    (bcIns.GetId <BytecodeInstruction::Format::IMM8_ID16_ID16_IMM16_V8, 1>()).AsRawValue());
                CollectInnerMethodsFromNewLiteral(method, literalId);
                break;
            }
            case BytecodeInstruction::Opcode::DEFINECLASSWITHBUFFER_IMM16_ID16_ID16_IMM16_V8: {
                auto entityId = jsPandaFile_->ResolveMethodIndex(method->GetMethodId(),
                    (bcIns.GetId <BytecodeInstruction::Format::IMM16_ID16_ID16_IMM16_V8, 0>()).AsRawValue());
                classConstructIndexes.emplace_back(entityId);
                classDefBCIndexes_.insert(bcIndex);
                methodId = entityId.GetOffset();
                CollectInnerMethods(method, methodId, true);
                auto literalId = jsPandaFile_->ResolveMethodIndex(method->GetMethodId(),
                    (bcIns.GetId <BytecodeInstruction::Format::IMM16_ID16_ID16_IMM16_V8, 1>()).AsRawValue());
                CollectInnerMethodsFromNewLiteral(method, literalId);
                break;
            }
            case BytecodeInstruction::Opcode::CREATEARRAYWITHBUFFER_IMM8_ID16:
            case BytecodeInstruction::Opcode::CREATEARRAYWITHBUFFER_IMM16_ID16: {
                auto literalId = jsPandaFile_->ResolveMethodIndex(method->GetMethodId(),
                    static_cast<uint16_t>(bcIns.GetId().AsRawValue()));
                CollectInnerMethodsFromNewLiteral(method, literalId);
                break;
            }
            case BytecodeInstruction::Opcode::DEPRECATED_CREATEARRAYWITHBUFFER_PREF_IMM16: {
                auto imm = bcIns.GetImm<BytecodeInstruction::Format::PREF_IMM16>();
                CollectInnerMethodsFromLiteral(method, imm);
                break;
            }
            case BytecodeInstruction::Opcode::CREATEOBJECTWITHBUFFER_IMM8_ID16:
            case BytecodeInstruction::Opcode::CREATEOBJECTWITHBUFFER_IMM16_ID16: {
                auto literalId = jsPandaFile_->ResolveMethodIndex(method->GetMethodId(),
                    static_cast<uint16_t>(bcIns.GetId().AsRawValue()));
                CollectInnerMethodsFromNewLiteral(method, literalId);
                break;
            }
            case BytecodeInstruction::Opcode::DEPRECATED_CREATEOBJECTWITHBUFFER_PREF_IMM16: {
                auto imm = bcIns.GetImm<BytecodeInstruction::Format::PREF_IMM16>();
                CollectInnerMethodsFromLiteral(method, imm);
                break;
            }
            case EcmaOpcode::RESUMEGENERATOR:
            case EcmaOpcode::SUSPENDGENERATOR_V8:
            case EcmaOpcode::SUPERCALLTHISRANGE_IMM8_IMM8_V8:
            case EcmaOpcode::WIDE_SUPERCALLTHISRANGE_PREF_IMM16_V8:
            case EcmaOpcode::SUPERCALLARROWRANGE_IMM8_IMM8_V8:
            case EcmaOpcode::WIDE_SUPERCALLARROWRANGE_PREF_IMM16_V8:
            case EcmaOpcode::SUPERCALLSPREAD_IMM8_V8:
            case EcmaOpcode::GETUNMAPPEDARGS:
            case EcmaOpcode::COPYRESTARGS_IMM8:
            case EcmaOpcode::WIDE_COPYRESTARGS_PREF_IMM16: {
                *canFastCall = false;
                return;
            }
            default:
                break;
        }
    }
}

void BytecodeInfoCollector::RearrangeInnerMethods()
{
    auto &methodList = bytecodeInfo_.GetMethodList();
    for (auto &it : methodList) {
        MethodInfo &methodInfo = it.second;
        methodInfo.RearrangeInnerMethods();
    }
}
}  // namespace panda::ecmascript::kungfu
