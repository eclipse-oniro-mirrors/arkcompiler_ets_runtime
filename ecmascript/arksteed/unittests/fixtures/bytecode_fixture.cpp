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

#include "common_components/taskpool/task.h"
#include "ecmascript/arksteed/arksteed_vreg.h"
#include "ecmascript/base_env.h"
#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/ecma_string.h"
#include "ecmascript/global_env.h"
#include "ecmascript/ic/property_box.h"
#include "ecmascript/jit/jit.h"
#include "ecmascript/jit/jit_task.h"
#include "ecmascript/jit/jit_thread.h"
#include "ecmascript/js_object.h"
#include "ecmascript/method.h"
#include "ecmascript/napi/include/jsnapi.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/pgo_profiler/pgo_profiler.h"
#include "ecmascript/pgo_profiler/types/pgo_profiler_type.h"
#include "ecmascript/tagged_array.h"
#include "ecmascript/tests/test_helper.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>

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

// Text of an observed profile slot value for a diagnostic. The weak and the
// heap-object cases are named explicitly, so a mismatch between a required IC
// state and the observed one is readable without a debugger.
std::string ObservedSlotName(JSTaggedValue value)
{
    if (value.IsHole()) {
        return "hole";
    }
    if (value.IsWeak()) {
        return value.GetWeakReferentUnChecked() == nullptr ? "cleared weak reference" : "weak reference";
    }
    if (value.IsPropertyBox()) {
        return "PropertyBox";
    }
    if (value.IsProfileTypeInfo()) {
        return "ProfileTypeInfo";
    }
    if (value.IsTaggedArray()) {
        return "tagged array of length " + std::to_string(TaggedArray::Cast(value.GetTaggedObject())->GetLength());
    }
    if (value.IsInt()) {
        return "int " + std::to_string(value.GetInt());
    }
    return ObservedValueName(value);
}

// Access bytecode of a profile kind: the bytecode whose first operand is the IC
// slot observed for that kind. Every one of these kinds carries exactly one
// such site.
bool TryGetProfileAccessOpcode(FixtureMethodKind kind, kungfu::EcmaOpcode *opcode)
{
    switch (kind) {
        case FixtureMethodKind::NAMED_MONO:
        case FixtureMethodKind::NAMED_POLY_LIMIT:
        case FixtureMethodKind::NAMED_MEGA:
        case FixtureMethodKind::NAMED_POLY_OVERFLOW:
        case FixtureMethodKind::EMPTY_SLOT:
            *opcode = kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16;
            return true;
        case FixtureMethodKind::ELEMENT_MONO:
        case FixtureMethodKind::ELEMENT_POLY_LIMIT:
            *opcode = kungfu::EcmaOpcode::LDOBJBYVALUE_IMM8_V8;
            return true;
        case FixtureMethodKind::GLOBAL_ACCESS:
            *opcode = kungfu::EcmaOpcode::LDGLOBALVAR_IMM16_ID16;
            return true;
        case FixtureMethodKind::CALL_JS:
        case FixtureMethodKind::CALL_BUILTIN:
            *opcode = kungfu::EcmaOpcode::CALLARG1_IMM8_V8;
            return true;
        default:
            return false;
    }
}

bool IsProfileKind(FixtureMethodKind kind)
{
    if (kind == FixtureMethodKind::OPERATION) {
        return true;
    }
    kungfu::EcmaOpcode opcode = kungfu::EcmaOpcode::NOP;
    return TryGetProfileAccessOpcode(kind, &opcode);
}

// IC slot of one decoded bytecode, following the production convention of
// `BytecodeInfo` and of `ArkSteedFeedbackReader::TryGetFeedbackSlotId`: the
// named, element and call bytecodes carry the slot in `slotId`, the global
// bytecodes carry it as their first `ICSlotId` input.
bool TryGetIcSlotId(const kungfu::BytecodeInfo &info, uint32_t *slotId)
{
    if (info.slotId.GetId() != kungfu::ICSlotId::INVALID_ID) {
        *slotId = static_cast<uint32_t>(info.slotId.GetId());
        return true;
    }
    for (const auto &input : info.inputs) {
        if (std::holds_alternative<kungfu::ICSlotId>(input)) {
            *slotId = static_cast<uint32_t>(std::get<kungfu::ICSlotId>(input).GetId());
            return true;
        }
    }
    return false;
}

// Every value of `ArkSteedOperationHint`, in declaration order.
constexpr ArkSteedOperationHint kOperationHints[] = {
    ArkSteedOperationHint::NONE,
    ArkSteedOperationHint::INT,
    ArkSteedOperationHint::NUMBER,
    ArkSteedOperationHint::STRING,
    ArkSteedOperationHint::NUMBER_OR_STRING,
    ArkSteedOperationHint::ANY,
    ArkSteedOperationHint::BOOLEAN,
    ArkSteedOperationHint::UNDEFINED_OR_NULL,
    ArkSteedOperationHint::BIG_INT,
};

// Hint published for a raw sampled type. `ArkSteedFeedbackReader` publishes
// `OperationFeedback::hint` with this mapping of the `pgo::PGOSampleType` value
// it reads from the slot; the fixture states the mapping here because the
// production read path itself runs on the JIT thread.
ArkSteedOperationHint HintOfRawSample(uint32_t raw)
{
    using Type = pgo::PGOSampleType::Type;
    switch (pgo::PGOSampleType(raw).GetPrimitiveType()) {
        case Type::NONE:
            return ArkSteedOperationHint::NONE;
        case Type::INT:
            return ArkSteedOperationHint::INT;
        case Type::INT_OVERFLOW:
        case Type::DOUBLE:
        case Type::NUMBER:
        case Type::NUMBER1:
            return ArkSteedOperationHint::NUMBER;
        case Type::STRING:
            return ArkSteedOperationHint::STRING;
        case Type::NUMBER_OR_STRING:
            return ArkSteedOperationHint::NUMBER_OR_STRING;
        case Type::BOOLEAN:
            return ArkSteedOperationHint::BOOLEAN;
        case Type::UNDEFINED_OR_NULL:
            return ArkSteedOperationHint::UNDEFINED_OR_NULL;
        case Type::BIG_INT:
            return ArkSteedOperationHint::BIG_INT;
        default:
            return ArkSteedOperationHint::ANY;
    }
}

// Raw sample type written for a hint that natural sampling does not produce.
// The values are the `pgo::PGOSampleType` constants `HintOfRawSample` maps back
// to that hint.
uint32_t RawSampleOfHint(ArkSteedOperationHint hint)
{
    using Type = pgo::PGOSampleType::Type;
    switch (hint) {
        case ArkSteedOperationHint::NONE:
            return static_cast<uint32_t>(Type::NONE);
        case ArkSteedOperationHint::INT:
            return static_cast<uint32_t>(Type::INT);
        case ArkSteedOperationHint::NUMBER:
            return static_cast<uint32_t>(Type::NUMBER);
        case ArkSteedOperationHint::STRING:
            return static_cast<uint32_t>(Type::STRING);
        case ArkSteedOperationHint::NUMBER_OR_STRING:
            return static_cast<uint32_t>(Type::NUMBER_OR_STRING);
        case ArkSteedOperationHint::ANY:
            return static_cast<uint32_t>(Type::ANY);
        case ArkSteedOperationHint::BOOLEAN:
            return static_cast<uint32_t>(Type::BOOLEAN);
        case ArkSteedOperationHint::UNDEFINED_OR_NULL:
            return static_cast<uint32_t>(Type::UNDEFINED_OR_NULL);
        case ArkSteedOperationHint::BIG_INT:
            return static_cast<uint32_t>(Type::BIG_INT);
    }
    return static_cast<uint32_t>(Type::ANY);
}

// The handler a mono case or a polymorphic case pair carries in its second
// slot: the IC writes a packed handler word (a primitive) for a fast field or
// element, or a handler object for a prototype or transition case. It is never
// hole (that second slot belongs to a polymorphic case), never undefined (the
// uninitialized state of the slot) and never a weak reference.
bool IsHandlerValue(JSTaggedValue value)
{
    return !value.IsHole() && !value.IsUndefined() && !value.IsWeak();
}

// Slot id of the single access site of `kind` in `method`. The site is the
// bytecode whose profile slot carries the state of that kind.
bool TryFindAccessSlot(const FixtureMethod &method, FixtureMethodKind kind, uint32_t *slotId)
{
    kungfu::EcmaOpcode opcode = kungfu::EcmaOpcode::NOP;
    if (!TryGetProfileAccessOpcode(kind, &opcode)) {
        return false;
    }
    const kungfu::BytecodeInfo *info = method.FindBytecode(opcode);
    return info != nullptr && TryGetIcSlotId(*info, slotId);
}

// Names of the stages of `FixtureStage` in the declaration order of the
// enumeration. It is a table rather than a switch on purpose: every nonzero
// enumerator is named exactly once in this translation unit, inside the emitter
// of the link that produces it, and this table only renders the name of an
// already recorded failure.
constexpr uint8_t kFixtureStageCount = 10;
constexpr const char *kFixtureStageNames[kFixtureStageCount] = {
    "NONE",
    "ABC_EXECUTION",
    "FUNCTION_LOOKUP",
    "METHOD_METADATA",
    "PROFILE",
    "COMPILER_VM",
    "COMPILE_TRIGGERED",
    "JIT_THREAD_CONTEXT",
    "COMPILATION_ENV",
    "BYTECODE_DECODE",
};

// Scope of a diagnostic of a link that runs before the method table exists.
constexpr const char *kCaseScope = "case";

// Printed form of an observed pointer, for a diagnostic that reports what a
// link saw instead of substituting a value for it.
std::string PointerText(const void *pointer)
{
    char buffer[32] = {0};  // 32: capacity for a printed pointer
    if (EOK != snprintf_s(buffer, sizeof(buffer), sizeof(buffer), "%p", pointer)) {
        return "<FORMAT-ERROR>";
    }
    return std::string(buffer);
}

// `true`/`false` of an observed condition, for the same reason.
const char *BoolText(bool value)
{
    return value ? "true" : "false";
}

// Synchronization of one lifecycle handshake.
struct JitWorkerHandshake {
    std::mutex mutex;
    std::condition_variable cv;
    bool ran {false};
};

// Test-only task of the lifecycle handshake. It observes only its own execution
// on the JIT worker and touches no VM state. The pool runs the prologue hook of
// the current worker -- the hook that publishes the compiler VM -- before it
// pops any task, so this task having run proves that the compiler VM pointer
// read afterwards belongs to the current worker and not to a destroyed one.
class JitWorkerReadyTask final : public common::Task {
public:
    explicit JitWorkerReadyTask(std::shared_ptr<JitWorkerHandshake> handshake)
        : common::Task(common::GLOBAL_TASK_ID), handshake_(std::move(handshake))
    {
    }

    bool Run([[maybe_unused]] uint32_t threadIndex) override
    {
        std::lock_guard<std::mutex> guard(handshake_->mutex);
        handshake_->ran = true;
        handshake_->cv.notify_all();
        return true;
    }

private:
    std::shared_ptr<JitWorkerHandshake> handshake_;
};

}  // namespace

const char *FixtureStageName(FixtureStage stage)
{
    auto index = static_cast<uint8_t>(stage);
    if (index < kFixtureStageCount) {
        return kFixtureStageNames[index];
    }
    // A value outside the enumeration is reported as such instead of being
    // mapped onto a stage that exists.
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

bool ArkSteedBytecodeFixture::RecordFailure(FixtureStage stage, const std::string &scope,
                                            const std::string &reason, const std::string &observed)
{
    // The first failed link is kept for the rest of the case: a later failure
    // neither replaces it nor is cleared, and the diagnostic always names the
    // stage, the affected method (or the scope `case`) and the value the link
    // observed.
    if (failedStage_ == FixtureStage::NONE) {
        failedStage_ = stage;
        failureDiagnostic_ = std::string(FixtureStageName(stage)) + ": method=" + scope + ": " + reason +
                             "; observed: " + observed;
    }
    return false;
}

bool ArkSteedBytecodeFixture::FailAbcExecution(const std::string &reason, const std::string &observed)
{
    return RecordFailure(FixtureStage::ABC_EXECUTION, kCaseScope, reason, observed);
}

bool ArkSteedBytecodeFixture::FailFunctionLookup(const std::string &scope, const std::string &reason,
                                                 const std::string &observed)
{
    return RecordFailure(FixtureStage::FUNCTION_LOOKUP, scope, reason, observed);
}

bool ArkSteedBytecodeFixture::FailMethodMetadata(const std::string &scope, const std::string &reason,
                                                 const std::string &observed)
{
    return RecordFailure(FixtureStage::METHOD_METADATA, scope, reason, observed);
}

bool ArkSteedBytecodeFixture::FailBytecodeDecode(const std::string &scope, const std::string &reason,
                                                 const std::string &observed)
{
    return RecordFailure(FixtureStage::BYTECODE_DECODE, scope, reason, observed);
}

bool ArkSteedBytecodeFixture::FailProfile(const std::string &scope, const std::string &reason,
                                          const std::string &observed)
{
    return RecordFailure(FixtureStage::PROFILE, scope, reason, observed);
}

bool ArkSteedBytecodeFixture::FailCompilerVm(const std::string &reason, const std::string &observed)
{
    return RecordFailure(FixtureStage::COMPILER_VM, kCaseScope, reason, observed);
}

bool ArkSteedBytecodeFixture::FailCompileTriggered(const std::string &scope, const std::string &reason,
                                                   const std::string &observed)
{
    return RecordFailure(FixtureStage::COMPILE_TRIGGERED, scope, reason, observed);
}

bool ArkSteedBytecodeFixture::FailJitThreadContext(const std::string &scope, const std::string &reason,
                                                   const std::string &observed)
{
    return RecordFailure(FixtureStage::JIT_THREAD_CONTEXT, scope, reason, observed);
}

bool ArkSteedBytecodeFixture::FailCompilationEnv(const std::string &scope, const std::string &reason,
                                                 const std::string &observed)
{
    return RecordFailure(FixtureStage::COMPILATION_ENV, scope, reason, observed);
}

const FixtureMethod &ArkSteedBytecodeFixture::EmptyMethod()
{
    // The empty value every accessor publishes once a link has failed. No field
    // of it is ever filled, so no data is substituted for a failed link.
    static const FixtureMethod empty;
    return empty;
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

JitTask *ArkSteedBytecodeFixture::PoolTask() const
{
    // `jitTask_` is created by the first `GetMethod` request of the case
    // (`EnsureJitThreadContext`) and deleted by `TearDown` after every dispatch
    // the case waited for has left the taskpool.
    return environmentReady_ ? jitTask_ : nullptr;
}

const FixtureMethod &ArkSteedBytecodeFixture::GetMethod(FixtureMethodKind kind)
{
    // The method table is filled by `SetUp`. On a fixture whose initialization
    // stopped at any link no entry is published: the accessor returns the empty
    // structure instead of a partially filled or substituted one, for every
    // kind and for the rest of the case.
    if (!environmentReady_ || failedStage_ != FixtureStage::NONE) {
        return EmptyMethod();
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
    // The objects the fixture owns for this kind are created on the first
    // request and cached; a failure of this request is recorded by the link
    // itself and makes the accessor report the empty structure, so a consumer
    // that ignores the failure cannot read a half-built environment.
    if (!PrepareCompilationEnv(kind, method)) {
        return EmptyMethod();
    }
    return method;
}

bool ArkSteedBytecodeFixture::EnsureJitThreadContext(FixtureMethodKind kind, FixtureMethod &method)
{
    if (jitTask_ != nullptr) {
        return true;
    }
    const char *name = FixtureMethodKindName(kind);
    if (method.function.IsEmpty()) {
        return FailJitThreadContext(name, "the method has no JSFunction", "function handle is empty");
    }
    if (compilerVm_ == nullptr || compilerThread_ == nullptr || !compilerThread_->IsJitThread() ||
        hostVm_ == nullptr || hostThread_ == nullptr) {
        return FailJitThreadContext(name, "the JIT compiler VM or the host VM is not ready",
                                    "compilerVm=" + PointerText(compilerVm_) +
                                        ", compilerThread=" + PointerText(compilerThread_) +
                                        ", hostVm=" + PointerText(hostVm_) +
                                        ", hostThread=" + PointerText(hostThread_));
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
    // Recorded before the verification below, so a binding that fails its
    // verification is still unbound by `TearDown` without a second dereference
    // of an unverified context.
    jitContextBound_ = true;

    if (static_cast<JitThread *>(compilerThread_)->GetHostThread() != hostThread_ ||
        static_cast<JitThread *>(compilerThread_)->GetCurrentTask() != jitTask_) {
        return FailJitThreadContext(name, "the compiler VM did not accept the host VM binding",
                                    "JitThread::GetHostThread()=" +
                                        PointerText(static_cast<JitThread *>(compilerThread_)->GetHostThread()) +
                                        ", JitThread::GetCurrentTask()=" +
                                        PointerText(static_cast<JitThread *>(compilerThread_)->GetCurrentTask()) +
                                        ", expected hostThread=" + PointerText(hostThread_) +
                                        ", jitTask=" + PointerText(jitTask_));
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
    // reported as COMPILATION_ENV instead of tripping an assertion inside the
    // production constructor, whose message would carry no stage.
    if (method.methodLiteral == nullptr || method.bytecodeStart == nullptr || method.bytecodeSize == 0) {
        return FailCompilationEnv(name, "the method has no complete metadata",
                                  "methodLiteral=" + PointerText(method.methodLiteral) +
                                      ", bytecodeStart=" + PointerText(method.bytecodeStart) +
                                      ", bytecodeSize=" + std::to_string(method.bytecodeSize));
    }
    JSTaggedValue lexicalEnv = method.function->GetLexicalEnv(hostThread_);
    if (!(lexicalEnv.IsLexicalEnv() || lexicalEnv.IsJSGlobalEnv() || lexicalEnv.IsSFunctionEnv())) {
        return FailCompilationEnv(name, "the lexical environment of the function is not a base env",
                                  ObservedValueName(lexicalEnv));
    }
    JSTaggedValue globalEnvValue = BaseEnv::Cast(lexicalEnv.GetTaggedObject())->GetGlobalEnv(hostThread_);
    if (!globalEnvValue.IsJSGlobalEnv()) {
        return FailCompilationEnv(name, "the lexical environment does not resolve to a JSGlobalEnv",
                                  ObservedValueName(globalEnvValue));
    }

    // Production creates exactly these two objects per compilation task; the
    // fixture creates them lazily per requested kind and keeps them until
    // `TearDown`. The environment takes the host-side handle of the function,
    // as the production task does with its host-side `jsFunction_`.
    method.dependencies = new kungfu::LazyDeoptAllDependencies();
    method.env = new JitCompilationEnv(compilerVm_, hostVm_, method.function, method.dependencies);
    if (method.env->GetHostThread() != hostThread_ || method.env->GetJSThread() != compilerThread_) {
        const std::string observedEnv = "JitCompilationEnv::GetHostThread()=" +
                                        PointerText(method.env->GetHostThread()) +
                                        ", JitCompilationEnv::GetJSThread()=" +
                                        PointerText(method.env->GetJSThread()) +
                                        ", expected hostThread=" + PointerText(hostThread_) +
                                        ", compilerThread=" + PointerText(compilerThread_);
        delete method.env;
        method.env = nullptr;
        delete method.dependencies;
        method.dependencies = nullptr;
        return FailCompilationEnv(name, "the compilation environment is bound to another host VM or thread",
                                  observedEnv);
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

bool ArkSteedBytecodeFixture::LoadProfile(FixtureMethodKind kind, FixtureMethod &method)
{
    const char *name = FixtureMethodKindName(kind);
    if (method.function.IsEmpty()) {
        return FailProfile(name, "the method has no JSFunction", "function handle is empty");
    }
    // The interpreter creates the profile of a method once the hotness counter
    // of its function crossed zero (`RuntimeNotifyInlineCache`) and publishes it
    // on the function. The fixture only reads that object; it never creates or
    // substitutes one.
    JSHandle<JSTaggedValue> profileValue(hostThread_, method.function->GetProfileTypeInfo(hostThread_));
    if (!profileValue->IsProfileTypeInfo()) {
        return FailProfile(name, "the warm-up left no ProfileTypeInfo on the function",
                           ObservedValueName(profileValue.GetTaggedValue()));
    }
    method.profile = JSHandle<ProfileTypeInfo>::Cast(profileValue);
    return true;
}

bool ArkSteedBytecodeFixture::ConstructProfileStates()
{
    if (!ConstructNamedPolyOverflow()) {
        return false;
    }
    return ConstructOperationSites();
}

bool ArkSteedBytecodeFixture::ConstructNamedPolyOverflow()
{
    constexpr FixtureMethodKind kKind = FixtureMethodKind::NAMED_POLY_OVERFLOW;
    constexpr FixtureMethodKind kSourceKind = FixtureMethodKind::NAMED_POLY_LIMIT;
    constexpr uint32_t kLimit = MAX_NAMED_IC_POLY_CASES;
    const char *name = FixtureMethodKindName(kKind);
    FixtureMethod &target = methods_[kKind];
    FixtureMethod &source = methods_[kSourceKind];

    uint32_t targetSlot = 0;
    uint32_t sourceSlot = 0;
    if (!TryFindAccessSlot(target, kKind, &targetSlot)) {
        return FailProfile(name, "the method has no named access site",
                           "decoded bytecodes=" + std::to_string(target.bytecodes_.size()));
    }
    if (!TryFindAccessSlot(source, kSourceKind, &sourceSlot)) {
        return FailProfile(FixtureMethodKindName(kSourceKind), "the method has no named access site",
                           "decoded bytecodes=" + std::to_string(source.bytecodes_.size()));
    }
    if (source.profile.IsEmpty() || target.profile.IsEmpty()) {
        return FailProfile(name, "the source or the target kind has no profile",
                           std::string("source profile empty=") + BoolText(source.profile.IsEmpty()) +
                               ", target profile empty=" + BoolText(target.profile.IsEmpty()));
    }
    if (sourceSlot >= source.profile->GetICSlotLength() ||
        targetSlot + 1 >= target.profile->GetICSlotLength()) {
        return FailProfile(name, "the access site leaves no room for a case pair",
                           "sourceSlot=" + std::to_string(sourceSlot) + " of " +
                               std::to_string(source.profile->GetICSlotLength()) + ", targetSlot=" +
                               std::to_string(targetSlot) + " of " +
                               std::to_string(target.profile->GetICSlotLength()));
    }

    // The real case pairs of the method whose polymorphic array stops exactly at
    // the IC limit. They are the only source of the constructed array; no pair
    // is fabricated. The handle keeps the array valid across the allocation of
    // the constructed array below.
    JSTaggedValue sourceFirst = source.profile->GetICSlot(hostThread_, sourceSlot);
    if (!sourceFirst.IsTaggedArray()) {
        return FailProfile(FixtureMethodKindName(kSourceKind), "the access slot is not a polymorphic array",
                           ObservedSlotName(sourceFirst));
    }
    JSHandle<TaggedArray> sourceArray(hostThread_, sourceFirst.GetTaggedObject());
    if (sourceArray->GetLength() != kLimit * 2) {  // 2: values per case pair
        return FailProfile(FixtureMethodKindName(kSourceKind),
                           "the polymorphic array does not hold " + std::to_string(kLimit) + " case pairs",
                           "case pairs=" + std::to_string(sourceArray->GetLength() / 2));  // 2: values per case pair
    }

    // The constructed array carries the real pairs plus one duplicate of the
    // first pair, which puts the case count above the ArkSteed limit: that state
    // is unreachable through IC, which stops at the limit.
    // 2: values per case pair
    JSHandle<TaggedArray> constructed = hostVm_->GetFactory()->NewTaggedArray((kLimit + 1) * 2);
    for (uint32_t index = 0; index < kLimit; ++index) {
        // 2: stride of one case pair
        constructed->Set(hostThread_, index * 2, sourceArray->Get(hostThread_, index * 2));
        // 2: stride of one case pair
        constructed->Set(hostThread_, index * 2 + 1, sourceArray->Get(hostThread_, index * 2 + 1));
    }
    constructed->Set(hostThread_, kLimit * 2, sourceArray->Get(hostThread_, 0));  // 2: stride of one case pair
    constructed->Set(hostThread_, kLimit * 2 + 1, sourceArray->Get(hostThread_, 1));  // 2: stride of one case pair

    target.profile->SetICSlot(hostThread_, targetSlot, constructed);
    target.profile->SetICSlot(hostThread_, targetSlot + 1, JSTaggedValue::Hole());
    return true;
}

bool ArkSteedBytecodeFixture::ConstructOperationSites()
{
    constexpr FixtureMethodKind kKind = FixtureMethodKind::OPERATION;
    const char *name = FixtureMethodKindName(kKind);
    FixtureMethod &method = methods_[kKind];
    if (method.profile.IsEmpty()) {
        return FailProfile(name, "the method has no profile", "profile handle is empty");
    }

    // Candidate sites: every decoded bytecode of the method that carries an IC
    // slot, in increasing offset order.
    std::vector<const kungfu::BytecodeInfo *> candidates {};
    std::vector<uint32_t> slots {};
    for (const kungfu::BytecodeInfo &info : method.bytecodes_) {
        uint32_t slotId = 0;
        if (TryGetIcSlotId(info, &slotId)) {
            candidates.push_back(&info);
            slots.push_back(slotId);
        }
    }
    if (candidates.empty()) {
        return FailProfile(name, "the method carries no operation site",
                           "decoded bytecodes=" + std::to_string(method.bytecodes_.size()) +
                               ", sites with an IC slot=0");
    }

    // A slot the warm-up filled holds a sampled `pgo::PGOSampleType`; the hint
    // it publishes is the first site of that hint. A slot the warm-up left
    // uninitialized is a construction slot of the second pass.
    std::vector<bool> taken(candidates.size(), false);
    for (size_t index = 0; index < candidates.size(); ++index) {
        if (slots[index] >= method.profile->GetICSlotLength()) {
            continue;
        }
        JSTaggedValue value = method.profile->GetICSlot(hostThread_, slots[index]);
        if (!value.IsInt()) {
            continue;
        }
        ArkSteedOperationHint hint = HintOfRawSample(static_cast<uint32_t>(value.GetInt()));
        if (operationSites_.find(hint) != operationSites_.end()) {
            continue;
        }
        operationSites_[hint] = candidates[index];
        taken[index] = true;
    }

    // Hints the warm-up cannot sample get the raw `pgo::PGOSampleType` value of
    // the hint in an uninitialized site. No other slot is written.
    for (ArkSteedOperationHint hint : kOperationHints) {
        if (operationSites_.find(hint) != operationSites_.end()) {
            continue;
        }
        bool written = false;
        for (size_t index = 0; index < candidates.size() && !written; ++index) {
            if (taken[index] || slots[index] >= method.profile->GetICSlotLength()) {
                continue;
            }
            JSTaggedValue value = method.profile->GetICSlot(hostThread_, slots[index]);
            if (value.IsInt()) {
                continue;
            }
            method.profile->SetICSlot(hostThread_, slots[index],
                                      JSTaggedValue(static_cast<int32_t>(RawSampleOfHint(hint))));
            operationSites_[hint] = candidates[index];
            taken[index] = true;
            written = true;
        }
        if (!written) {
            return FailProfile(name,
                               "no uninitialized operation site is left for hint " +
                                   std::to_string(static_cast<int>(hint)),
                               "candidate sites=" + std::to_string(candidates.size()));
        }
    }
    return true;
}

bool ArkSteedBytecodeFixture::CheckProfileState(FixtureMethodKind kind, FixtureMethod &method)
{
    const char *name = FixtureMethodKindName(kind);
    if (method.profile.IsEmpty()) {
        return FailProfile(name, "the method has no profile", "profile handle is empty");
    }
    if (kind == FixtureMethodKind::OPERATION) {
        for (ArkSteedOperationHint hint : kOperationHints) {
            if (operationSites_.find(hint) == operationSites_.end()) {
                return FailProfile(name,
                                   "no site carries hint " + std::to_string(static_cast<int>(hint)),
                                   "operation sites=" + std::to_string(operationSites_.size()));
            }
        }
        return true;
    }

    uint32_t slotId = 0;
    if (!TryFindAccessSlot(method, kind, &slotId)) {
        return FailProfile(name, "the method has no access site",
                           "decoded bytecodes=" + std::to_string(method.bytecodes_.size()));
    }
    const uint32_t length = method.profile->GetICSlotLength();
    if (slotId >= length) {
        return FailProfile(name, "the access site is outside the profile",
                           "access slot=" + std::to_string(slotId) + ", profile slots=" +
                               std::to_string(length));
    }
    JSTaggedValue first = method.profile->GetICSlot(hostThread_, slotId);
    JSTaggedValue second = slotId + 1 < length ? method.profile->GetICSlot(hostThread_, slotId + 1)
                                               : JSTaggedValue::Undefined();
    const std::string observed = ObservedSlotName(first) + " / " + ObservedSlotName(second);

    // A polymorphic array of exactly `expectedPairs` case pairs of a weak HClass
    // reference and a handler.
    auto checkPolyArray = [&](uint32_t expectedPairs) -> bool {
        if (first.IsWeak() || !first.IsTaggedArray()) {
            return false;
        }
        auto *array = TaggedArray::Cast(first.GetTaggedObject());
        if (array->GetLength() != expectedPairs * 2) {  // 2: values per case pair
            return false;
        }
        for (uint32_t index = 0; index < array->GetLength(); index += 2) {  // 2: stride of one (hclass, handler) pair
            if (!array->Get(hostThread_, index).IsWeak() ||
                !IsHandlerValue(array->Get(hostThread_, index + 1))) {
                return false;
            }
        }
        return second.IsHole();
    };

    switch (kind) {
        case FixtureMethodKind::NAMED_MONO:
        case FixtureMethodKind::ELEMENT_MONO:
            if (!first.IsWeak() || !IsHandlerValue(second)) {
                return FailProfile(name, "the access slot is not a mono case", observed);
            }
            return true;
        case FixtureMethodKind::NAMED_POLY_LIMIT:
        case FixtureMethodKind::ELEMENT_POLY_LIMIT:
            if (!checkPolyArray(MAX_NAMED_IC_POLY_CASES)) {
                return FailProfile(name,
                                   "the access slot is not a poly array of exactly " +
                                       std::to_string(MAX_NAMED_IC_POLY_CASES) + " case pairs",
                                   observed);
            }
            return true;
        case FixtureMethodKind::NAMED_MEGA:
            if (!first.IsHole() || !second.IsHole()) {
                return FailProfile(name, "the access slot is not mega", observed);
            }
            return true;
        case FixtureMethodKind::NAMED_POLY_OVERFLOW:
            if (!checkPolyArray(MAX_NAMED_IC_POLY_CASES + 1)) {
                return FailProfile(name,
                                   "the constructed access slot is not a poly array of exactly " +
                                       std::to_string(MAX_NAMED_IC_POLY_CASES + 1) + " case pairs",
                                   observed);
            }
            return true;
        case FixtureMethodKind::GLOBAL_ACCESS: {
            if (!first.IsPropertyBox()) {
                return FailProfile(name, "the access slot is not a PropertyBox", observed);
            }
            auto *box = PropertyBox::Cast(first.GetTaggedObject());
            if (box->IsInvalid(hostThread_) || box->GetValue(hostThread_).IsAccessorData()) {
                return FailProfile(name, "the PropertyBox is invalid", observed);
            }
            return true;
        }
        case FixtureMethodKind::CALL_JS:
            if (!first.IsJSFunction()) {
                return FailProfile(name, "the call slot is not a JSFunction", observed);
            }
            return true;
        case FixtureMethodKind::CALL_BUILTIN:
            if (!first.IsInt() || first.GetInt() >= 0) {
                return FailProfile(name, "the call slot is not a negative builtin id", observed);
            }
            return true;
        case FixtureMethodKind::EMPTY_SLOT:
            if (!first.IsUndefined()) {
                return FailProfile(name, "the unexecuted access slot is not undefined", observed);
            }
            return true;
        default:
            return FailProfile(name, "the kind has no required profile state", observed);
    }
}

const kungfu::BytecodeInfo *ArkSteedBytecodeFixture::FindOperationSite(ArkSteedOperationHint hint) const
{
    // A fixture whose initialization failed publishes no site: the map is only
    // reachable when every link of `SetUp` succeeded and no later request
    // failed, so a consumer that ignores the failure gets `nullptr` instead of
    // a site of a broken case.
    if (!environmentReady_ || failedStage_ != FixtureStage::NONE) {
        return nullptr;
    }
    auto entry = operationSites_.find(hint);
    if (entry == operationSites_.end()) {
        return nullptr;
    }
    return entry->second;
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
        return FailAbcExecution("cannot open the fixture abc", "'" + abcFile + "'");
    }
    abcStream.close();

    // The entry point belongs to the same link: the file name of the abc is
    // derived from both published macros, so a stem that does not name the
    // generated asset is already caught by the asset check above. The execution
    // itself reports a failure (a damaged asset, a throw at the top of the
    // script) through its return value, and that value is observed instead of
    // being swallowed.
    if (!JSNApi::Execute(hostVm_, abcFile, ARKSTEED_FIXTURE_ENTRY_POINT)) {
        return FailAbcExecution("the fixture abc did not execute",
                                "JSNApi::Execute('" + abcFile + "', entry '" +
                                    ARKSTEED_FIXTURE_ENTRY_POINT + "') returned false");
    }

    // The script publishes its functions on the global object; the lookup goes
    // through the same global env the running script wrote to.
    JSHandle<GlobalEnv> globalEnv = hostVm_->GetGlobalEnv();
    if (globalEnv->GetGlobalObject().IsUndefined()) {
        return FailFunctionLookup(kCaseScope, "the host VM has no global object after executing the script",
                                  "GlobalEnv::GetGlobalObject() is undefined");
    }
    JSHandle<EcmaString> keyString = hostVm_->GetFactory()->NewFromUtf8(kFixturesObjectName);
    JSHandle<JSTaggedValue> key(keyString);
    OperationResult result = JSObject::GetPropertyFromGlobal(hostThread_, key);
    JSHandle<JSTaggedValue> value = result.GetValue();
    if (!value->IsJSObject()) {
        return FailFunctionLookup(kCaseScope, std::string("global ") + kFixturesObjectName + " is not an object",
                                  ObservedValueName(value.GetTaggedValue()));
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
        return FailFunctionLookup(name, "the published property is not a JSFunction",
                                  ObservedValueName(value.GetTaggedValue()));
    }

    FixtureMethod &method = methods_[kind];
    method.function = JSHandle<JSFunction>::Cast(value);

    // F-4: the fixture methods stay below the JIT hotness threshold, so none of
    // them may be compiling or compiled after the script ran. A triggered
    // compilation is reported instead of being hidden, so a case whose warm-up
    // crossed the threshold cannot look like a healthy fixture.
    if (method.function->IsJitCompiling()) {
        return FailCompileTriggered(name, "JIT compilation is in progress",
                                    "JSFunction::IsJitCompiling()=true");
    }
    if (method.function->IsCompiledCode()) {
        return FailCompileTriggered(name, "the method is already compiled",
                                    "JSFunction::IsCompiledCode()=true");
    }

    JSTaggedValue methodValue = method.function->GetMethod(hostThread_);
    if (!methodValue.IsMethod()) {
        return FailMethodMetadata(name, "the function carries no Method", ObservedValueName(methodValue));
    }
    Method *methodObject = Method::Cast(methodValue.GetTaggedObject());
    method.pandaFile = const_cast<JSPandaFile *>(methodObject->GetJSPandaFile(hostThread_));
    method.methodLiteral = methodObject->GetMethodLiteral(hostThread_);
    method.bytecodeStart = methodObject->GetBytecodeArray();
    if (method.pandaFile == nullptr) {
        return FailMethodMetadata(name, "the method has no JSPandaFile", "Method::GetJSPandaFile()=nullptr");
    }
    if (method.methodLiteral == nullptr) {
        return FailMethodMetadata(name, "the method has no MethodLiteral", "Method::GetMethodLiteral()=nullptr");
    }
    if (method.bytecodeStart == nullptr) {
        return FailMethodMetadata(name, "the method has no bytecode array",
                                  "Method::GetBytecodeArray()=nullptr");
    }
    // Same code size as the production preprocessor takes for `bcSizeBytes_`.
    method.bytecodeSize = MethodLiteral::GetCodeSize(method.pandaFile, method.methodLiteral->GetMethodId());
    if (method.bytecodeSize == 0) {
        return FailMethodMetadata(name, "the method has empty bytecode", "MethodLiteral::GetCodeSize()=0");
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
            return FailBytecodeDecode(name, "the instruction does not fit the bytecode",
                                      "offset=" + std::to_string(offset) + ", size=" +
                                          std::to_string(instructionSize) + ", remaining=" +
                                          std::to_string(bytecodeSize - offset));
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
        return FailBytecodeDecode(name, "the decoded instructions do not cover the bytecode",
                                  "decoded=" + std::to_string(offset) + " of " +
                                      std::to_string(bytecodeSize) + " bytes");
    }
    return true;
}

bool ArkSteedBytecodeFixture::AwaitCurrentJitWorker()
{
    auto handshake = std::make_shared<JitWorkerHandshake>();
    common::Task *handshakeTask = nullptr;
    {
        auto task = std::make_unique<JitWorkerReadyTask>(handshake);
        handshakeTask = task.get();
        JitTaskpool::GetCurrentTaskpool()->PostTask(std::move(task));
    }

    // (1) The current worker has executed the handshake task. `Runner::Run`
    // calls the prologue hook of the pool -- which publishes the compiler VM --
    // before it pops any task, so a task that has run proves that
    // `GetCompilerVm()` now names the VM of the current worker. A task that
    // never runs is a failure of the pool, not a reason to read the pointer.
    // A zero budget establishes nothing (it is the deterministic failure input
    // of the fixture-lifecycle case) and still leaves the task queued exactly
    // as a real timeout does, so both reach the same failure path below.
    bool ran = false;
    if (readinessBudget_.count() > 0) {
        std::unique_lock<std::mutex> lock(handshake->mutex);
        ran = handshake->cv.wait_for(lock, readinessBudget_, [&handshake] { return handshake->ran; });
    }

    // (2) The worker has retired the handshake task. `Jit::ClearTaskWithVm`
    // casts every task it finds in the pool to `JitTask::AsyncTask`, so a task
    // of the fixture must be neither queued nor running when the case destroys
    // its host VM. A task that has run stays in `runningTask_` until the worker
    // retires it, so its absence from `ForEachTask` is the retirement proof.
    if (ran) {
        const auto deadline = std::chrono::steady_clock::now() + readinessBudget_;
        do {
            bool retained = false;
            JitTaskpool::GetCurrentTaskpool()->ForEachTask(
                [&retained, handshakeTask](common::Task *task) { retained = retained || task == handshakeTask; });
            if (!retained) {
                return true;
            }
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
    }

    // The handshake did not prove that the compiler VM belongs to the current
    // worker, so `SetUp` fails and the case goes red. The handshake task may
    // still be queued or running; `Jit::ClearTaskWithVm`, reached from
    // `~EcmaVM` when the host VM is destroyed, casts every task it finds in the
    // pool to `JitTask::AsyncTask`, which a plain `common::Task` is not, so an
    // outstanding task would crash the consumer process instead of releasing
    // its resources.
    //
    // The pool is therefore stopped before it can be read by VM teardown.
    // `Taskpool::Destroy` brings the pool's initialization count to zero --
    // from which `ForEachTask`, the only pool reader that
    // `Jit::ClearTaskWithVm` drives, returns without touching a task -- and
    // `Runner::TerminateThread` joins the worker, whose pop loop returns every
    // queued task before it honours the termination flag. The task is thus
    // drained and retired in the same call, and host-VM destruction is left
    // with no incompatible task to cast. The drain is deliberately not
    // asserted here: the fixture contains no assertion and never aborts a
    // case, its failures are reported through `FixtureStage` and
    // `FailureDiagnostic()` (F-8), and the `COMPILER_VM` failure recorded by
    // `SetUp` already makes this path observable to the consumer.
    JitTaskpool::GetCurrentTaskpool()->Destroy();
    return false;
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

    // Readiness of the compiler VM. `Jit::IsEnableFastJit()` is checked first
    // because its truth proves that `JitResources::InitJitEnv` already ran for
    // this case. The pointer itself cannot be used as readiness proof: the JIT
    // taskpool is a process-lifetime singleton whose `compilerVm_` is not
    // cleared when the worker destroys the compiler VM, so after the previous
    // case a bare `WaitForJitTaskPoolReady()` returns immediately on the stale
    // pointer and the fixture would dereference a destroyed VM. Readiness of
    // the current worker is established by the lifecycle handshake instead, and
    // the pointer is published only after it passed every check below.
    if (!Jit::GetInstance()->IsEnableFastJit()) {
        FailCompilerVm("fast JIT is not enabled for the fixture VM", "Jit::IsEnableFastJit()=false");
        return;
    }
    bool workerReady = false;
    EcmaVM *currentCompilerVm = nullptr;
    {
        ThreadNativeScope nativeScope(hostThread_);
        workerReady = AwaitCurrentJitWorker();
        if (workerReady) {
            currentCompilerVm = JitTaskpool::GetCurrentTaskpool()->GetCompilerVm();
        }
    }
    if (!workerReady) {
        FailCompilerVm("the JIT worker did not complete the readiness handshake",
                       "the handshake task did not run and retire within " +
                           std::to_string(readinessBudget_.count()) + "ms; the JIT taskpool was stopped "
                           "before host-VM teardown, so no handshake task can reach it");
        return;
    }
    if (currentCompilerVm == nullptr) {
        FailCompilerVm("the JIT taskpool has no compiler VM",
                       "JitTaskpool::GetCurrentTaskpool()->GetCompilerVm()=nullptr");
        return;
    }
    JSThread *currentCompilerThread = currentCompilerVm->GetJSThread();
    if (currentCompilerThread == nullptr || !currentCompilerThread->IsJitThread()) {
        const std::string observed = "compilerThread=" + PointerText(currentCompilerThread) +
                                     ", compilerThread->IsJitThread()=" +
                                     BoolText(currentCompilerThread != nullptr &&
                                              currentCompilerThread->IsJitThread());
        FailCompilerVm("the compiler VM has no JIT thread", observed);
        return;
    }
    // Published only now, after the handshake and every check passed: `TearDown`
    // dereferences these members, so a stale or half-validated VM is never left
    // reachable from the fixture.
    compilerVm_ = currentCompilerVm;
    compilerThread_ = currentCompilerThread;

    // Contract point (1) of the parent page: the host VM carries a profile, and
    // the JIT profile read by `JitCompilationEnv::GetPGOProfiler()` exists.
    // Neither is substituted when it is missing.
    auto pgoProfiler = hostVm_->GetPGOProfiler();
    if (pgoProfiler == nullptr || pgoProfiler->GetJITProfile() == nullptr) {
        FailCompilerVm("the fixture VM has no JIT profile",
                       "EcmaVM::GetPGOProfiler()=" + PointerText(pgoProfiler.get()) +
                           ", PGOProfiler::GetJITProfile()=nullptr");
        return;
    }

    // Data of the fixture methods. The script is executed exactly once (the
    // warm-up it carries leaves the profile of every profile kind behind), then
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

    // Profile of the fixture methods. The profile object of every profile kind
    // comes from the interpreter through the warm-up; the fixture publishes it,
    // writes the two states IC cannot reach, then verifies the required state of
    // each kind. A mismatch stops the initialization at PROFILE with the
    // observed slot values, and no slot is substituted.
    for (uint8_t index = 0; index < kFixtureMethodKindCount; index++) {
        FixtureMethodKind kind = static_cast<FixtureMethodKind>(index);
        if (IsProfileKind(kind) && !LoadProfile(kind, methods_[kind])) {
            return;
        }
    }
    if (!ConstructProfileStates()) {
        return;
    }
    for (uint8_t index = 0; index < kFixtureMethodKindCount; index++) {
        FixtureMethodKind kind = static_cast<FixtureMethodKind>(index);
        if (IsProfileKind(kind) && !CheckProfileState(kind, methods_[kind])) {
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
    // The hint-to-site map holds pointers into the decoded bytecode tables of
    // the method table that was just cleared.
    operationSites_.clear();

    // (2) The JIT thread context: the current task of the JIT thread. Only a
    // context this case actually bound is unbound; `TearDown` never dereferences
    // a VM or a thread that `SetUp` did not validate.
    if (jitContextBound_ && compilerThread_ != nullptr) {
        static_cast<JitThread *>(compilerThread_)->SetCurrentTask(nullptr);
    }

    // (3) The back pointer of the compiler VM to the host VM.
    if (jitContextBound_ && compilerVm_ != nullptr) {
        static_cast<JitVM *>(compilerVm_)->ReSetHostVM();
    }
    jitContextBound_ = false;

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
