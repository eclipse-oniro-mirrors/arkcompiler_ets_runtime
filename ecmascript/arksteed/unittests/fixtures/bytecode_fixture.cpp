/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#include "ecmascript/arksteed/unittests/fixtures/bytecode_fixture.h"

#include "ecmascript/arksteed/arksteed_vreg.h"
#include "ecmascript/base_env.h"
#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/ecma_string.h"
#include "ecmascript/global_env.h"
#include "ecmascript/jit/jit.h"
#include "ecmascript/jit/jit_task.h"
#include "ecmascript/jit/jit_thread.h"
#include "ecmascript/js_object.h"
#include "ecmascript/method.h"
#include "ecmascript/napi/include/jsnapi.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/pgo_profiler/pgo_profiler.h"
#include "ecmascript/tests/test_helper.h"

#include <fstream>

namespace panda::ecmascript::arksteed {

namespace {

// Property of the global object the fixture script publishes its functions on.
constexpr const char *kFixturesObjectName = "__arksteedFixtures";
// Number of `FixtureMethodKind` enumerators, in declaration order.
constexpr uint8_t kFixtureMethodKindCount = 16;
static_assert(static_cast<uint8_t>(FixtureMethodKind::EMPTY_SLOT) + 1 == kFixtureMethodKindCount,
              "the method table must cover every FixtureMethodKind");

// Property name of a kind on the published object; identical to the enumerator
// name in the fixture script and in `FixtureMethodKind`.
const char *FixtureMethodKindName(FixtureMethodKind kind)
{
    switch (kind) {
        case FixtureMethodKind::LINEAR:
            return "LINEAR";
        case FixtureMethodKind::BRANCH:
            return "BRANCH";
        case FixtureMethodKind::LOOP:
            return "LOOP";
        case FixtureMethodKind::CRITICAL_EDGE:
            return "CRITICAL_EDGE";
        case FixtureMethodKind::TRY_CATCH:
            return "TRY_CATCH";
        case FixtureMethodKind::NAMED_MONO:
            return "NAMED_MONO";
        case FixtureMethodKind::NAMED_POLY_LIMIT:
            return "NAMED_POLY_LIMIT";
        case FixtureMethodKind::NAMED_MEGA:
            return "NAMED_MEGA";
        case FixtureMethodKind::NAMED_POLY_OVERFLOW:
            return "NAMED_POLY_OVERFLOW";
        case FixtureMethodKind::ELEMENT_MONO:
            return "ELEMENT_MONO";
        case FixtureMethodKind::ELEMENT_POLY_LIMIT:
            return "ELEMENT_POLY_LIMIT";
        case FixtureMethodKind::GLOBAL_ACCESS:
            return "GLOBAL_ACCESS";
        case FixtureMethodKind::CALL_JS:
            return "CALL_JS";
        case FixtureMethodKind::CALL_BUILTIN:
            return "CALL_BUILTIN";
        case FixtureMethodKind::OPERATION:
            return "OPERATION";
        case FixtureMethodKind::EMPTY_SLOT:
            return "EMPTY_SLOT";
    }
    return "UNKNOWN";
}

// Table of opcode metadata. It is pure data derived from the opcode encoding,
// so one instance per process is enough (the production preprocessor keeps its
// own file-scope instance the same way).
kungfu::Bytecodes &FixtureBytecodes()
{
    static kungfu::Bytecodes table;
    return table;
}

// Text of the observed value for a diagnostic, without substituting data.
const char *ObservedValueName(JSTaggedValue value)
{
    if (value.IsUndefined()) {
        return "undefined";
    }
    if (value.IsNull()) {
        return "null";
    }
    if (value.IsJSFunction()) {
        return "JSFunction";
    }
    if (value.IsJSObject()) {
        return "JSObject";
    }
    if (value.IsNumber()) {
        return "number";
    }
    if (value.IsString()) {
        return "string";
    }
    if (value.IsBoolean()) {
        return "boolean";
    }
    if (value.IsSymbol()) {
        return "symbol";
    }
    return "other";
}

}  // namespace

const char *FixtureStageName(FixtureStage stage)
{
    switch (stage) {
        case FixtureStage::NONE:
            return "NONE";
        case FixtureStage::ABC_EXECUTION:
            return "ABC_EXECUTION";
        case FixtureStage::FUNCTION_LOOKUP:
            return "FUNCTION_LOOKUP";
        case FixtureStage::METHOD_METADATA:
            return "METHOD_METADATA";
        case FixtureStage::PROFILE:
            return "PROFILE";
        case FixtureStage::COMPILER_VM:
            return "COMPILER_VM";
        case FixtureStage::COMPILE_TRIGGERED:
            return "COMPILE_TRIGGERED";
        case FixtureStage::JIT_THREAD_CONTEXT:
            return "JIT_THREAD_CONTEXT";
        case FixtureStage::COMPILATION_ENV:
            return "COMPILATION_ENV";
        case FixtureStage::BYTECODE_DECODE:
            return "BYTECODE_DECODE";
    }
    return "UNKNOWN";
}

const kungfu::BytecodeInfo *FixtureMethod::FindBytecode(kungfu::EcmaOpcode opcode, uint32_t occurrence) const
{
    uint32_t seen = 0;
    for (const kungfu::BytecodeInfo &info : bytecodes_) {
        if (info.GetOpcode() != opcode) {
            continue;
        }
        if (seen == occurrence) {
            return &info;
        }
        seen++;
    }
    // A missing bytecode is reported as the empty value; no table entry is
    // substituted for it.
    return nullptr;
}

FixtureStage ArkSteedBytecodeFixture::FailedStage() const
{
    return failedStage_;
}

const char *ArkSteedBytecodeFixture::FailureDiagnostic() const
{
    if (failedStage_ == FixtureStage::NONE) {
        return "ok";
    }
    return failureDiagnostic_.c_str();
}

void ArkSteedBytecodeFixture::Fail(FixtureStage stage, const std::string &diagnostic)
{
    // The first failed link is kept for the rest of the case: a later failure
    // must not replace the cause that stopped the initialization.
    if (failedStage_ != FixtureStage::NONE) {
        return;
    }
    failedStage_ = stage;
    failureDiagnostic_ = std::string(FixtureStageName(stage)) + ": " + diagnostic;
}

EcmaVM *ArkSteedBytecodeFixture::HostVm() const
{
    return environmentReady_ ? hostVm_ : nullptr;
}

JSThread *ArkSteedBytecodeFixture::HostThread() const
{
    return environmentReady_ ? hostThread_ : nullptr;
}

JSThread *ArkSteedBytecodeFixture::CompilerThread() const
{
    return environmentReady_ ? compilerThread_ : nullptr;
}

const FixtureMethod &ArkSteedBytecodeFixture::GetMethod(FixtureMethodKind kind)
{
    // The method table is filled by `SetUp`. On a fixture whose initialization
    // stopped at any link no entry is published: the accessor returns an empty
    // structure instead of a partially filled or substituted one.
    if (!environmentReady_) {
        static const FixtureMethod empty;
        return empty;
    }
    FixtureMethod &method = methods_[kind];
    if (method.env != nullptr) {
        // The profile of the kind, supplied by the profile leaf, is handed to
        // the environment once and stays bound. This path neither switches the
        // broker mode nor reads a profile slot.
        if (!method.profile.IsEmpty() && method.env->GetProfileTypeInfo().IsEmpty()) {
            method.env->SetProfileTypeInfo(method.profile);
        }
        return method;
    }
    // A link failed earlier in this case: no object is published for the kind,
    // and no later request re-runs the initialization.
    if (failedStage_ != FixtureStage::NONE) {
        return method;
    }
    PrepareCompilationEnv(kind, method);
    return method;
}

bool ArkSteedBytecodeFixture::EnsureJitThreadContext(FixtureMethodKind kind, FixtureMethod &method)
{
    if (jitTask_ != nullptr) {
        return true;
    }
    const char *name = FixtureMethodKindName(kind);
    if (method.function.IsEmpty()) {
        Fail(FixtureStage::JIT_THREAD_CONTEXT, std::string(name) + ": the method has no JSFunction");
        return false;
    }
    if (compilerVm_ == nullptr || compilerThread_ == nullptr || !compilerThread_->IsJitThread() ||
        hostVm_ == nullptr || hostThread_ == nullptr) {
        Fail(FixtureStage::JIT_THREAD_CONTEXT, "the JIT compiler VM or the host VM is not ready");
        return false;
    }

    // The public constructor is the production constructor for the compilation
    // of this function. The task is never scheduled and never compiled: it only
    // owns the sustaining handle storage the JIT thread hands out for the heap
    // references of the profile.
    CString methodName(FixtureMethodKindName(kind));
    jitTask_ = new JitTask(hostThread_, compilerThread_, Jit::GetInstance(), method.function,
                           CompilerTier::Tier::FAST, methodName, MachineCode::INVALID_OSR_OFFSET,
                           JitCompileMode::Mode::SYNC);

    // Same bind order as `JitAsyncTaskRunScope`, which production uses around a
    // compilation task. `JitLockHolder`/`ArkSteedHeapBroker::SerializingScope`
    // need both of them: the host thread for the JIT lock and the current task
    // for every `env_->NewJSHandle`.
    static_cast<JitVM *>(compilerVm_)->SetHostVM(hostThread_);
    static_cast<JitThread *>(compilerThread_)->SetCurrentTask(jitTask_);

    if (static_cast<JitThread *>(compilerThread_)->GetHostThread() != hostThread_ ||
        static_cast<JitThread *>(compilerThread_)->GetCurrentTask() != jitTask_) {
        Fail(FixtureStage::JIT_THREAD_CONTEXT, "the compiler VM did not accept the host VM binding");
        return false;
    }
    return true;
}

bool ArkSteedBytecodeFixture::PrepareCompilationEnv(FixtureMethodKind kind, FixtureMethod &method)
{
    const char *name = FixtureMethodKindName(kind);
    if (!EnsureJitThreadContext(kind, method)) {
        return false;
    }

    // Preconditions of the production `JitCompilationEnv` constructor. Each one
    // is checked before the constructor runs, so a violated precondition is
    // reported as COMPILATION_ENV instead of tripping a production `ASSERT`
    // whose message carries no stage.
    if (method.methodLiteral == nullptr || method.bytecodeStart == nullptr || method.bytecodeSize == 0) {
        Fail(FixtureStage::COMPILATION_ENV, std::string(name) + ": the method has no complete metadata");
        return false;
    }
    JSTaggedValue lexicalEnv = method.function->GetLexicalEnv(hostThread_);
    if (!(lexicalEnv.IsLexicalEnv() || lexicalEnv.IsJSGlobalEnv() || lexicalEnv.IsSFunctionEnv())) {
        Fail(FixtureStage::COMPILATION_ENV,
             std::string(name) + ": the lexical environment of the function is not a base env (observed: " +
                 ObservedValueName(lexicalEnv) + ")");
        return false;
    }
    JSTaggedValue globalEnvValue = BaseEnv::Cast(lexicalEnv.GetTaggedObject())->GetGlobalEnv(hostThread_);
    if (!globalEnvValue.IsJSGlobalEnv()) {
        Fail(FixtureStage::COMPILATION_ENV,
             std::string(name) + ": the lexical environment does not resolve to a JSGlobalEnv (observed: " +
                 ObservedValueName(globalEnvValue) + ")");
        return false;
    }

    // Production creates exactly these two objects per compilation task; the
    // fixture creates them lazily per requested kind and keeps them until
    // `TearDown`. The environment takes the host-side handle of the function,
    // as the production task does with its host-side `jsFunction_`.
    method.dependencies = new kungfu::LazyDeoptAllDependencies();
    method.env = new JitCompilationEnv(compilerVm_, hostVm_, method.function, method.dependencies);
    if (method.env->GetHostThread() != hostThread_ || method.env->GetJSThread() != compilerThread_) {
        Fail(FixtureStage::COMPILATION_ENV,
             std::string(name) + ": the compilation environment is bound to another host VM or thread");
        delete method.env;
        method.env = nullptr;
        delete method.dependencies;
        method.dependencies = nullptr;
        return false;
    }

    // The profile of the kind is supplied by the profile leaf. Until it exists
    // the handle is empty and the environment is left without a profile, so
    // `ArkSteedHeapBroker::TryGetProfileTypeInfo()` answers false inside a
    // serializing scope instead of reading a substituted object.
    if (!method.profile.IsEmpty()) {
        method.env->SetProfileTypeInfo(method.profile);
    }

    method.broker = new ArkSteedHeapBroker(compilerThread_, method.env);
    return true;
}

const kungfu::BytecodeInfo *ArkSteedBytecodeFixture::FindOperationSite(ArkSteedOperationHint) const
{
    // The operation sites belong to the profile leaf of the domain.
    return nullptr;
}

bool ArkSteedBytecodeFixture::ExecuteFixtureScript(JSHandle<JSTaggedValue> &fixtures)
{
    // The abc file name is derived from both published values: the directory
    // comes from `ARKSTEED_FIXTURE_ABC_DIR` and the stem from
    // `ARKSTEED_FIXTURE_ENTRY_POINT`, so no relative runtime lookup is needed.
    const std::string abcFile =
        std::string(ARKSTEED_FIXTURE_ABC_DIR) + "/" + ARKSTEED_FIXTURE_ENTRY_POINT + ".abc";

    // The production loader aborts the process (`LOG_FULL(FATAL)`) when the
    // asset cannot be opened, so the fixture checks the asset before the
    // production call: a missing or unreadable abc is reported as the failed
    // link with a stage, instead of a crash that carries no stage.
    std::ifstream abcStream(abcFile, std::ios::binary);
    if (!abcStream.is_open()) {
        Fail(FixtureStage::ABC_EXECUTION, "cannot open the fixture abc '" + abcFile + "'");
        return false;
    }
    abcStream.close();

    if (!JSNApi::Execute(hostVm_, abcFile, ARKSTEED_FIXTURE_ENTRY_POINT)) {
        Fail(FixtureStage::ABC_EXECUTION,
             "cannot execute '" + abcFile + "' with entry '" + ARKSTEED_FIXTURE_ENTRY_POINT + "'");
        return false;
    }

    // The script publishes its functions on the global object; the lookup goes
    // through the same global env the running script wrote to.
    JSHandle<GlobalEnv> globalEnv = hostVm_->GetGlobalEnv();
    if (globalEnv->GetGlobalObject().IsUndefined()) {
        Fail(FixtureStage::FUNCTION_LOOKUP, "the host VM has no global object after executing the script");
        return false;
    }
    JSHandle<EcmaString> keyString = hostVm_->GetFactory()->NewFromUtf8(kFixturesObjectName);
    JSHandle<JSTaggedValue> key(keyString);
    OperationResult result = JSObject::GetPropertyFromGlobal(hostThread_, key);
    JSHandle<JSTaggedValue> value = result.GetValue();
    if (!value->IsJSObject()) {
        Fail(FixtureStage::FUNCTION_LOOKUP,
             std::string("global ") + kFixturesObjectName + " is not an object (observed: " +
                 ObservedValueName(value.GetTaggedValue()) + ")");
        return false;
    }
    fixtures = value;
    return true;
}

bool ArkSteedBytecodeFixture::LoadMethod(FixtureMethodKind kind, const JSHandle<JSTaggedValue> &fixtures)
{
    const char *name = FixtureMethodKindName(kind);

    JSHandle<EcmaString> keyString = hostVm_->GetFactory()->NewFromUtf8(name);
    JSHandle<JSTaggedValue> key(keyString);
    OperationResult result = JSObject::GetProperty(hostThread_, fixtures, key);
    JSHandle<JSTaggedValue> value = result.GetValue();
    if (!value->IsJSFunction()) {
        Fail(FixtureStage::FUNCTION_LOOKUP,
             std::string(name) + ": the published property is not a JSFunction (observed: " +
                 ObservedValueName(value.GetTaggedValue()) + ")");
        return false;
    }

    FixtureMethod &method = methods_[kind];
    method.function = JSHandle<JSFunction>::Cast(value);

    // F-4: the fixture methods stay below the JIT hotness threshold, so none of
    // them may be compiling or compiled after the script ran. A triggered
    // compilation keeps this method invalid instead of hiding it.
    if (method.function->IsJitCompiling()) {
        Fail(FixtureStage::COMPILE_TRIGGERED, std::string(name) + ": JIT compilation is in progress");
        return false;
    }
    if (method.function->IsCompiledCode()) {
        Fail(FixtureStage::COMPILE_TRIGGERED, std::string(name) + ": the method is already compiled");
        return false;
    }

    JSTaggedValue methodValue = method.function->GetMethod(hostThread_);
    if (!methodValue.IsMethod()) {
        Fail(FixtureStage::METHOD_METADATA,
             std::string(name) + ": the function carries no Method (observed: " +
                 ObservedValueName(methodValue) + ")");
        return false;
    }
    Method *methodObject = Method::Cast(methodValue.GetTaggedObject());
    method.pandaFile = const_cast<JSPandaFile *>(methodObject->GetJSPandaFile(hostThread_));
    method.methodLiteral = methodObject->GetMethodLiteral(hostThread_);
    method.bytecodeStart = methodObject->GetBytecodeArray();
    if (method.pandaFile == nullptr) {
        Fail(FixtureStage::METHOD_METADATA, std::string(name) + ": the method has no JSPandaFile");
        return false;
    }
    if (method.methodLiteral == nullptr) {
        Fail(FixtureStage::METHOD_METADATA, std::string(name) + ": the method has no MethodLiteral");
        return false;
    }
    if (method.bytecodeStart == nullptr) {
        Fail(FixtureStage::METHOD_METADATA, std::string(name) + ": the method has no bytecode array");
        return false;
    }
    // Same code size as the production preprocessor takes for `bcSizeBytes_`.
    method.bytecodeSize = MethodLiteral::GetCodeSize(method.pandaFile, method.methodLiteral->GetMethodId());
    if (method.bytecodeSize == 0) {
        Fail(FixtureStage::METHOD_METADATA, std::string(name) + ": the method has empty bytecode");
        return false;
    }
    return DecodeBytecode(kind, method);
}

bool ArkSteedBytecodeFixture::DecodeBytecode(FixtureMethodKind kind, FixtureMethod &method)
{
    const char *name = FixtureMethodKindName(kind);
    const uint8_t *startPc = method.bytecodeStart;
    const uint32_t bytecodeSize = method.bytecodeSize;
    // Production convention of `BytecodePreprocessor::CollectBytecodeInfo`:
    // the lexical-env virtual register index is derived from the method
    // literal, and an offset is counted from `GetMethodPcStart()`, which is the
    // bytecode array of the method.
    const VRegIDType envVRegIndex = VRegOfLexicalEnv(
        method.methodLiteral->GetNumVregsWithCallField(), method.methodLiteral->GetNumArgsForArkSteed());
    kungfu::Bytecodes &table = FixtureBytecodes();

    method.bytecodes_.clear();
    method.bytecodes_.reserve(bytecodeSize);

    BytecodeInstruction instruction(startPc);
    const BytecodeInstruction last = instruction.JumpTo(static_cast<int32_t>(bytecodeSize));
    uint32_t offset = 0;
    while (instruction.GetAddress() != last.GetAddress()) {
        const uint8_t *pc = instruction.GetAddress();
        kungfu::BytecodeInfo info;
        info.SetMetaData(table.GetBytecodeMetaData(pc));
        const size_t instructionSize = info.GetSize();
        if (instructionSize == 0 || instructionSize > bytecodeSize - offset) {
            method.bytecodes_.clear();
            Fail(FixtureStage::BYTECODE_DECODE,
                 std::string(name) + ": instruction at offset " + std::to_string(offset) + " has size " +
                     std::to_string(instructionSize) + ", remaining " + std::to_string(bytecodeSize - offset));
            return false;
        }
        // Jump instructions carry only metadata in production as well; their
        // target is resolved by the preprocessor.
        if (!info.GetMetaData().IsJump()) {
            kungfu::BytecodeInfo::InitBytecodeInfo(info, pc, offset, envVRegIndex);
        }
        method.bytecodes_.push_back(info);
        offset += static_cast<uint32_t>(instructionSize);
        instruction = instruction.JumpTo(static_cast<int32_t>(instructionSize));
    }
    if (offset != bytecodeSize) {
        method.bytecodes_.clear();
        Fail(FixtureStage::BYTECODE_DECODE,
             std::string(name) + ": decoded " + std::to_string(offset) + " of " + std::to_string(bytecodeSize) +
                 " bytecode bytes");
        return false;
    }
    return true;
}

void ArkSteedBytecodeFixture::SetUp()
{
    // Exactly one host VM per case, created through the project test template
    // (`ecmascript/tests/test_helper.h`). The template sets force GC and, with
    // `enableFastJit`, JIT on the options it passes to `JSNApi::CreateEcmaVM`,
    // and opens the single handle scope of the case. It is the first VM of the
    // binary in the fixture usage, which is what runs
    // `JitResources::InitJitEnv` for the JIT options below.
    test::TestHelper::CreateEcmaVMWithScope(
        hostVm_, hostThread_, scope_, false, false, true, /*enableFastJit=*/true);

    // Option set of the fixture VM. The template builds its options locally, so
    // the remaining values are pinned on the VM options before any fixture
    // method exists: the JIT hotness threshold is read from these options when
    // a method profile is created, which keeps the fixture methods below the
    // compilation trigger; `SetEnableJITPGO` is what makes the JIT profile,
    // read through `JitCompilationEnv::GetPGOProfiler() -> GetJITProfile()`,
    // exist. `SetEnablePGOProfiler` is deliberately left at its default
    // `false`, so no PGO dump path is configured for the fixture.
    JSRuntimeOptions &options = hostVm_->GetJSOptions();
    options.SetEnableForceGC(true);
    options.SetEnableJIT(true);
    options.SetEnableJITPGO(true);
    options.SetCompilerEnableLiteCG(true);
    options.SetJitHotnessThreshold(JIT_HOTNESS_THRESHOLD);

    // Readiness of the compiler VM. `WaitForJitTaskPoolReady()` has no timeout,
    // so it is only awaited after the fast-JIT flag proves that `InitJitEnv`
    // already ran: without fast JIT the wait is skipped and the environment
    // stays unusable, instead of hanging the run on a pool that never becomes
    // ready.
    if (!Jit::GetInstance()->IsEnableFastJit()) {
        Fail(FixtureStage::COMPILER_VM, "fast JIT is not enabled for the fixture VM");
        return;
    }
    {
        ThreadNativeScope nativeScope(hostThread_);
        JitTaskpool::GetCurrentTaskpool()->WaitForJitTaskPoolReady();
        compilerVm_ = JitTaskpool::GetCurrentTaskpool()->GetCompilerVm();
    }
    if (compilerVm_ == nullptr) {
        Fail(FixtureStage::COMPILER_VM, "the JIT taskpool has no compiler VM");
        return;
    }
    compilerThread_ = compilerVm_->GetJSThread();
    if (compilerThread_ == nullptr || !compilerThread_->IsJitThread()) {
        Fail(FixtureStage::COMPILER_VM, "the compiler VM has no JIT thread");
        compilerThread_ = nullptr;
        return;
    }

    // Contract point (1) of the parent page: the host VM carries a profile, and
    // the JIT profile read by `JitCompilationEnv::GetPGOProfiler()` exists.
    // Neither is substituted when it is missing.
    auto pgoProfiler = hostVm_->GetPGOProfiler();
    if (pgoProfiler == nullptr || pgoProfiler->GetJITProfile() == nullptr) {
        Fail(FixtureStage::COMPILER_VM, "the fixture VM has no JIT profile");
        return;
    }

    // Data of the fixture methods. The script is executed exactly once, then
    // every published function is resolved together with its method metadata
    // and its decoded bytecode. A failure of any link leaves the whole table
    // unpublished.
    JSHandle<JSTaggedValue> fixtures;
    if (!ExecuteFixtureScript(fixtures)) {
        return;
    }
    for (uint8_t index = 0; index < kFixtureMethodKindCount; index++) {
        if (!LoadMethod(static_cast<FixtureMethodKind>(index), fixtures)) {
            return;
        }
    }

    environmentReady_ = true;
}

void ArkSteedBytecodeFixture::TearDown()
{
    // Release order of the fixture-owned resources. Every step is guarded, so a
    // `SetUp` that stopped at any link releases only what it created, in the
    // same order.

    // (1) The compilation objects the fixture owns per requested method, and
    // the decoded bytecode tables they carry.
    for (auto &entry : methods_) {
        delete entry.second.broker;
        entry.second.broker = nullptr;
        delete entry.second.env;
        entry.second.env = nullptr;
        delete entry.second.dependencies;
        entry.second.dependencies = nullptr;
    }
    methods_.clear();

    // (2) The JIT thread context: the current task of the JIT thread.
    if (compilerThread_ != nullptr) {
        static_cast<JitThread *>(compilerThread_)->SetCurrentTask(nullptr);
    }

    // (3) The back pointer of the compiler VM to the host VM.
    if (compilerVm_ != nullptr) {
        static_cast<JitVM *>(compilerVm_)->ReSetHostVM();
    }

    // (4) The task owns the sustaining handles registered in the host VM, so it
    // must not outlive it.
    delete jitTask_;
    jitTask_ = nullptr;

    // (5) The host VM with its handle scope.
    if (hostVm_ != nullptr) {
        test::TestHelper::DestroyEcmaVMWithScope(hostVm_, scope_);
    }
    hostVm_ = nullptr;
    hostThread_ = nullptr;
    compilerVm_ = nullptr;
    compilerThread_ = nullptr;
    environmentReady_ = false;
}

}  // namespace panda::ecmascript::arksteed
