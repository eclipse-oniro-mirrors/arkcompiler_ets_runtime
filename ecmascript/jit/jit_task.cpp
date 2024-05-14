/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#include "ecmascript/jit/jit_task.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/global_env.h"
#include "ecmascript/compiler/aot_file/func_entry_des.h"
#include "ecmascript/compiler/pgo_type/pgo_type_manager.h"
#include "ecmascript/ic/profile_type_info.h"
#include "ecmascript/patch/patch_loader.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/dfx/vmstat/jit_warmup_profiler.h"

namespace panda::ecmascript {

JitTaskpool *JitTaskpool::GetCurrentTaskpool()
{
    static JitTaskpool *taskpool = new JitTaskpool();
    return taskpool;
}

uint32_t JitTaskpool::TheMostSuitableThreadNum([[maybe_unused]]uint32_t threadNum) const
{
    return 1;
}

JitTask::JitTask(JSThread *hostThread, JSThread *compilerThread, Jit *jit, JSHandle<JSFunction> &jsFunction,
    CompilerTier tier, CString &methodName, int32_t offset, uint32_t taskThreadId, JitCompileMode mode)
    : hostThread_(hostThread),
    compilerThread_(compilerThread),
    jit_(jit),
    jsFunction_(jsFunction),
    compilerTask_(nullptr),
    state_(CompileState::SUCCESS),
    compilerTier_(tier),
    methodName_(methodName),
    offset_(offset),
    taskThreadId_(taskThreadId),
    ecmaContext_(nullptr),
    jitCompileMode_(mode),
    runState_(RunState::INIT)
{
    ecmaContext_ = hostThread->GetCurrentEcmaContext();
    sustainingJSHandle_ = std::make_unique<SustainingJSHandle>(hostThread->GetEcmaVM());
}

void JitTask::PrepareCompile()
{
    CloneProfileTypeInfo();
    SustainingJSHandles();
    compilerTask_ = jit_->CreateJitCompilerTask(this);

    Method *method = Method::Cast(jsFunction_->GetMethod().GetTaggedObject());
    JSTaggedValue constpool = method->GetConstantPool();
    if (!ConstantPool::CheckUnsharedConstpool(constpool)) {
        hostThread_->GetCurrentEcmaContext()->FindOrCreateUnsharedConstpool(constpool);
    }

    SetRunState(RunState::INIT);
}

void JitTask::Optimize()
{
    bool res = jit_->JitCompile(compilerTask_, this);
    if (!res) {
        SetCompileFailed();
    }
}

void JitTask::Finalize()
{
    if (!IsCompileSuccess()) {
        return;
    }

    bool res = jit_->JitFinalize(compilerTask_, this);
    if (!res) {
        SetCompileFailed();
    }
}

void JitTask::InstallOsrCode(JSHandle<Method> &method, JSHandle<MachineCode> &codeObj)
{
    auto profile = jsFunction_->GetProfileTypeInfo();
    if (profile.IsUndefined()) {
        LOG_JIT(DEBUG) << "[OSR] Empty profile for installing code:" << GetMethodName();
        return;
    }
    FuncEntryDes *funcEntryDes = reinterpret_cast<FuncEntryDes*>(codeObj->GetFuncEntryDes());
    method->SetIsFastCall(funcEntryDes->isFastCall_);
    JSHandle<ProfileTypeInfo> profileInfoHandle =
        JSHandle<ProfileTypeInfo>::Cast(JSHandle<JSTaggedValue>(hostThread_, profile));
    uint32_t slotId = profileInfoHandle->GetCacheLength() - 1;  // 1 : get last slot
    auto profileData = profileInfoHandle->Get(slotId);
    auto factory = hostThread_->GetEcmaVM()->GetFactory();
    if (!profileData.IsTaggedArray()) {
        const uint32_t initLen = 1;
        JSHandle<TaggedArray> newArr = factory->NewTaggedArray(initLen);
        newArr->Set(hostThread_, 0, codeObj.GetTaggedValue());
        profileInfoHandle->Set(hostThread_, slotId, newArr.GetTaggedValue());
        LOG_JIT(DEBUG) << "[OSR] Install machine code:" << GetMethodName()
                       << ", code address: " << reinterpret_cast<void*>(codeObj->GetFuncAddr())
                       << ", index: " << newArr->GetLength() - 1;
        return;
    }
    JSHandle<TaggedArray> arr(hostThread_, profileData);
    JSHandle<TaggedArray> newArr = factory->NewTaggedArray(arr->GetLength() + 1);  // 1 : added for current codeObj
    uint32_t i = 0;
    for (; i < arr->GetLength(); i++) {
        newArr->Set(hostThread_, i, arr->Get(i));
    }
    newArr->Set(hostThread_, i, codeObj.GetTaggedValue());
    profileInfoHandle->Set(hostThread_, slotId, newArr.GetTaggedValue());
    LOG_JIT(DEBUG) << "[OSR] Install machine code:" << GetMethodName()
                   << ", code address: " << reinterpret_cast<void*>(codeObj->GetFuncAddr())
                   << ", index: " << newArr->GetLength() - 1;
    return;
}

static size_t ComputePayLoadSize(const MachineCodeDesc &codeDesc)
{
    if (codeDesc.codeType == MachineCodeType::BASELINE_CODE) {
        // only code section in BaselineCode
        return AlignUp(codeDesc.codeSize, MachineCode::DATA_ALIGN);
    }

    ASSERT(codeDesc.codeType == MachineCodeType::FAST_JIT_CODE);
    size_t funcEntryDesSizeAlign = AlignUp(codeDesc.funcEntryDesSize, MachineCode::TEXT_ALIGN);
    size_t rodataSizeBeforeTextAlign = AlignUp(codeDesc.rodataSizeBeforeText, MachineCode::TEXT_ALIGN);
    size_t codeSizeAlign = AlignUp(codeDesc.codeSize, MachineCode::DATA_ALIGN);
    size_t rodataSizeAfterTextAlign = AlignUp(codeDesc.rodataSizeAfterText, MachineCode::DATA_ALIGN);

    size_t stackMapSizeAlign = AlignUp(codeDesc.stackMapSize, MachineCode::DATA_ALIGN);

    return funcEntryDesSizeAlign + rodataSizeBeforeTextAlign + codeSizeAlign +
           rodataSizeAfterTextAlign + stackMapSizeAlign;
}

void JitTask::SetHClassInfoForPGO(JSHandle<Method> &methodHandle)
{
    auto ptManager = hostThread_->GetCurrentEcmaContext()->GetPTManager();
    auto info = ptManager->GenJITHClassInfo();
    auto ecmaContext = hostThread_->GetCurrentEcmaContext();
    auto unsharedConstantPool = ConstantPool::Cast(
        ecmaContext->FindUnsharedConstpool(methodHandle->GetConstantPool()).GetTaggedObject());
    unsharedConstantPool->SetAotHClassInfoWithBarrier(hostThread_, info.GetTaggedValue());
}

void JitTask::InstallCode()
{
    if (!IsCompileSuccess()) {
        return;
    }
    [[maybe_unused]] EcmaHandleScope handleScope(hostThread_);

    JSHandle<Method> methodHandle(hostThread_, Method::Cast(jsFunction_->GetMethod().GetTaggedObject()));
    if (GetHostVM()->GetJSOptions().IsEnableJITPGO()) {
        SetHClassInfoForPGO(methodHandle);
    }

    size_t size = ComputePayLoadSize(codeDesc_);

    JSHandle<Method> newMethodHandle = hostThread_->GetEcmaVM()->GetFactory()->CloneMethodTemporaryForJIT(methodHandle);
    jsFunction_->SetMethod(hostThread_, newMethodHandle);
    JSHandle<MachineCode> machineCodeObj =
        hostThread_->GetEcmaVM()->GetFactory()->NewMachineCodeObject(size, codeDesc_, newMethodHandle);
    machineCodeObj->SetOSROffset(offset_);

    if (hostThread_->HasPendingException()) {
        // check is oom exception
        hostThread_->SetMachineCodeLowMemory(true);
        hostThread_->ClearException();
    }

    if (IsOsrTask()) {
        InstallOsrCode(newMethodHandle, machineCodeObj);
        return;
    }

    uintptr_t codeAddr = machineCodeObj->GetFuncAddr();
    if (compilerTier_ == CompilerTier::FAST) {
        FuncEntryDes *funcEntryDes = reinterpret_cast<FuncEntryDes*>(machineCodeObj->GetFuncEntryDes());
        jsFunction_->SetCompiledFuncEntry(codeAddr, funcEntryDes->isFastCall_);
        newMethodHandle->SetDeoptThreshold(hostThread_->GetEcmaVM()->GetJSOptions().GetDeoptThreshold());
        jsFunction_->SetMachineCode(hostThread_, machineCodeObj);
        if (!hostThread_->IsMachineCodeLowMemory() && methodHandle->GetFunctionKind() == FunctionKind::ARROW_FUNCTION) {
            hostThread_->GetCurrentEcmaContext()->AddJitMachineCode(
                methodHandle->GetMethodId(),
                std::make_pair(methodHandle.GetTaggedType(), machineCodeObj.GetTaggedType()));
        }
        LOG_JIT(DEBUG) <<"Install fast jit machine code:" << GetMethodName();
#if ECMASCRIPT_ENABLE_JIT_WARMUP_PROFILER
        auto &profMap = JitWarmupProfiler::GetInstance()->profMap_;
        if (profMap.find(GetMethodName()) != profMap.end()) {
            profMap[GetMethodName()] = true;
        }
#endif
    } else {
        ASSERT(compilerTier_ == CompilerTier::BASELINE);
        newMethodHandle->SetDeoptThreshold(hostThread_->GetEcmaVM()->GetJSOptions().GetDeoptThreshold());
        jsFunction_->SetBaselineCode(hostThread_, machineCodeObj);
        LOG_BASELINEJIT(DEBUG) <<"Install baseline jit machine code:" << GetMethodName();
    }
}

void JitTask::SustainingJSHandles()
{
    // transfer to sustaining handle
    JSHandle<JSFunction> sustainingJsFunctionHandle = sustainingJSHandle_->NewHandle(jsFunction_);
    SetJsFunction(sustainingJsFunctionHandle);

    JSHandle<ProfileTypeInfo> profileTypeInfo = sustainingJSHandle_->NewHandle(profileTypeInfo_);
    SetProfileTypeInfo(profileTypeInfo);
}

void JitTask::ReleaseSustainingJSHandle()
{
    // in abort case, vm exit before task finish, release by explict
    sustainingJSHandle_ = nullptr;
}

void JitTask::CloneProfileTypeInfo()
{
    [[maybe_unused]] EcmaHandleScope handleScope(hostThread_);

    Method *method = Method::Cast(jsFunction_->GetMethod().GetTaggedObject());
    uint32_t slotSize = method->GetSlotSize();
    JSTaggedValue profileTypeInfoVal = jsFunction_->GetProfileTypeInfo();
    JSHandle<ProfileTypeInfo> newProfileTypeInfo;
    ObjectFactory *factory = hostThread_->GetEcmaVM()->GetFactory();
    if (profileTypeInfoVal.IsUndefined() || slotSize == 0) {
        slotSize = slotSize == 0 ? 1 : slotSize; // there's no profiletypeinfo, just generate a temp profiletypeinfo
        newProfileTypeInfo = factory->NewProfileTypeInfo(slotSize);
    } else {
        JSHandle<ProfileTypeInfo> profileTypeInfo(hostThread_,
            ProfileTypeInfo::Cast(profileTypeInfoVal.GetTaggedObject()));
        newProfileTypeInfo = factory->NewProfileTypeInfo(slotSize);
        for (uint32_t i = 0; i < slotSize; i++) {
            JSTaggedValue value = profileTypeInfo->Get(i);
            newProfileTypeInfo->Set(hostThread_, i, value);
        }
    }
    SetProfileTypeInfo(newProfileTypeInfo);
}

JitTask::~JitTask()
{
    ReleaseSustainingJSHandle();
    jit_->DeleteJitCompile(compilerTask_);
}

void JitTask::WaitFinish()
{
    LockHolder lock(runStateMutex_);
    if (!IsFinish()) {
        runStateCondition_.Wait(&runStateMutex_);
    }
}

bool JitTask::AsyncTask::Run([[maybe_unused]] uint32_t threadIndex)
{
    if (IsTerminate()) {
        return false;
    }
    DISALLOW_HEAP_ACCESS;

    // JitCompileMode ASYNC
    // check init ok
    jitTask_->SetRunState(RunState::RUNNING);

    JSThread *compilerThread = jitTask_->GetCompilerThread();
    ASSERT(compilerThread->IsJitThread());
    JitThread *jitThread = static_cast<JitThread*>(compilerThread);
    JitVM *jitvm = jitThread->GetJitVM();
    jitvm->SetHostVM(jitTask_->GetHostThread());

    if (jitTask_->GetJsFunction().GetAddress() == 0) {
        // for unit test
    } else {
        CString info = "compile method:" + jitTask_->GetMethodName() + ", in jit thread";
        Jit::TimeScope scope(info, jitTask_->GetCompilerTier());

        jitTask_->Optimize();
        jitTask_->Finalize();

        if (jitTask_->IsAsyncTask()) {
            // info main thread compile complete
            jitTask_->jit_->RequestInstallCode(jitTask_);
        }
    }
    jitvm->ReSetHostVM();
    jitTask_->SetRunStateFinish();
    return true;
}
}  // namespace panda::ecmascript
