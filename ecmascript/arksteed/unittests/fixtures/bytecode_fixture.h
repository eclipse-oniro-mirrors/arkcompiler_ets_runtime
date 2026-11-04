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

#ifndef ECMASCRIPT_ARKSTEED_UNITTESTS_FIXTURES_BYTECODE_FIXTURE_H
#define ECMASCRIPT_ARKSTEED_UNITTESTS_FIXTURES_BYTECODE_FIXTURE_H

/*
 * Public API of the ArkSteed bytecode fixture (domain
 * `arksteed-host-unit-bytecode-fixture`, page
 * `arksteed-host-unit-bytecode-fixture-mechanics`).
 *
 * The module supplies data and environment only. It states nothing about the
 * invariants its consumers check, it never marks a case as skipped, and it
 * never substitutes a reserved value for a missing one: when a link of the
 * initialization fails, the accessors below report empty values and
 * `FailedStage()`/`FailureDiagnostic()` name the link, so that the consumer
 * fails loudly through its own checks.
 *
 * The fixture bytecode is built from `fixtures/arksteed_bytecode_fixture.js`
 * (GN action `gen_arksteed_bytecode_fixture_abc`, template `es2abc_gen_abc`,
 * script mode). The abc directory and the entry point are published by the
 * target `arksteed_bytecode_fixture` in `ecmascript/arksteed/unittests/BUILD.gn`
 * through its `public_configs`, so a consumer target that lists
 * `:arksteed_bytecode_fixture` in `deps` receives `ARKSTEED_FIXTURE_ABC_DIR` and
 * `ARKSTEED_FIXTURE_ENTRY_POINT` in its own translation unit. The consumer
 * declares neither macro itself.
 *
 * The class below is the boundary of the domain: the mechanics that create the
 * host VM with its JIT options, execute the fixture abc, resolve the method
 * metadata, decode the bytecode, bind the JIT thread context and create the
 * compilation environment are implemented on these same names by the leaves of
 * the domain.
 *
 * Profile of the fixture methods. The warm-up at the bottom of the fixture
 * script runs the interpreter, which creates the `ProfileTypeInfo` of a method
 * and fills the IC slots of the executed sites. `SetUp` then reads the profile
 * of every profile kind, verifies the state the `Architecture` table of the
 * domain page requires and publishes it through `FixtureMethod::profile`; the
 * environment of the kind receives it with `SetProfileTypeInfo`, so a consumer
 * reads it through `ArkSteedHeapBroker::TryGetProfileTypeInfo()` inside a
 * `SerializingScope` (or through the `GetFeedbackFor*` wrappers) on
 * `CompilerThread()`, exactly as production reads the profile of a compilation
 * task. A state that does not match leaves the fixture failed at the `PROFILE`
 * stage and no slot is substituted.
 *
 * Constructed slots. Two states the interpreter cannot reach are written by
 * the fixture itself, through the public `ProfileTypeInfo::SetICSlot`, and only
 * these two:
 *   - the access slot of `NAMED_POLY_OVERFLOW`: a tagged array of
 *     `MAX_NAMED_IC_POLY_CASES + 1` case pairs, copied from the real pairs of
 *     the `NAMED_POLY_LIMIT` profile plus one duplicate pair, followed by
 *     `Hole` in the second access slot. The IC stops at the limit, so a case
 *     count above it is reachable only through this write.
 *   - the `OPERATION` sites whose hint cannot be sampled naturally: their slot
 *     receives the raw `pgo::PGOSampleType` value of that hint. `FindOperationSite`
 *     reports exactly these sites.
 * Every other slot of every other kind is left as the interpreter wrote it.
 * The hint of a naturally sampled `OPERATION` site is the public mapping the
 * `ArkSteedFeedbackReader` publishes as `OperationFeedback::hint`; the fixture
 * states that mapping itself because the production read path runs on the JIT
 * thread.
 */

#include "ecmascript/arksteed/arksteed_heap_broker.h"
#include "ecmascript/arksteed/arksteed_processed_feedback.h"
#include "ecmascript/compiler/bytecodes.h"
#include "ecmascript/compiler/jit_compilation_env.h"
#include "ecmascript/compiler/lazy_deopt_dependency.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/ic/profile_type_info.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/jspandafile/method_literal.h"

#include "gtest/gtest.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace panda::ecmascript {
class JitTask;
}  // namespace panda::ecmascript

namespace panda::ecmascript::arksteed {

/*
 * Kind of a fixture method. Every kind owns one function of the fixture script
 * (`globalThis.__arksteedFixtures` carries one property per enumerator, named
 * after it) with exactly one access site, so a profile state is addressed and
 * damaged independently of the other states.
 */
enum class FixtureMethodKind : uint8_t {
    LINEAR,
    BRANCH,
    LOOP,
    CRITICAL_EDGE,
    TRY_CATCH,
    NAMED_MONO,
    NAMED_POLY_LIMIT,
    NAMED_MEGA,
    NAMED_POLY_OVERFLOW,
    ELEMENT_MONO,
    ELEMENT_POLY_LIMIT,
    GLOBAL_ACCESS,
    CALL_JS,
    CALL_BUILTIN,
    OPERATION,
    EMPTY_SLOT,
};

/*
 * Link of the fixture initialization that failed first. `NONE` means that no
 * link is left failed, not that a consumer's invariant holds: the fixture
 * states nothing about the invariants its consumers check, and a case is green
 * only through the assertions the consumer makes on the data of the fixture.
 *
 * One link produces one stage. Every nonzero enumerator is emitted by exactly
 * one link of the initialization and is named exactly once in the translation
 * unit of the fixture, inside the emitter that this link owns
 * (`FixtureStageName` renders the names from an ordered table and emits
 * nothing); the first failed link is kept for the rest of the case and no path
 * clears it. The deliberate precondition break of each stage is listed below
 * together with the value the stage is then observed with:
 *
 *   ABC_EXECUTION      Link: the fixture abc is opened and executed
 *                      (`ExecuteFixtureScript`). Break: point
 *                      `ARKSTEED_FIXTURE_ENTRY_POINT` at a stem that does not
 *                      name the generated asset (the file name of the abc is
 *                      derived from both macros), or damage the asset.
 *                      Observed: `cannot open the fixture abc '<path>'` from
 *                      the asset check, which runs before the loader (an
 *                      unreadable asset aborts the process there), or
 *                      `JSNApi::Execute('<path>', entry '<stem>') returned
 *                      false` when the execution itself fails.
 *   FUNCTION_LOOKUP    Link: the published object `globalThis.__arksteedFixtures`
 *                      and the published function of a kind
 *                      (`ExecuteFixtureScript`, `LoadMethod`). Break: remove
 *                      the publication from the fixture script, or publish a
 *                      non-function. Observed: `global __arksteedFixtures is
 *                      not an object; observed: <type>` with the scope `case`,
 *                      or `the published property is not a JSFunction;
 *                      observed: <type>` with the kind of the property.
 *   METHOD_METADATA    Link: the `Method`, its `MethodLiteral`, the bytecode
 *                      array and the code size of the published function
 *                      (`LoadMethod`). Break: hand the link a function without a
 *                      `Method` (a native builtin published in place of a
 *                      fixture function), or a method whose metadata is
 *                      incomplete. Observed: `the function carries no Method;
 *                      observed: <type>`, `Method::GetJSPandaFile()=nullptr`,
 *                      `Method::GetMethodLiteral()=nullptr`,
 *                      `Method::GetBytecodeArray()=nullptr` or
 *                      `MethodLiteral::GetCodeSize()=0`.
 *   BYTECODE_DECODE    Link: the bytecode stream is walked with the production
 *                      conventions (`DecodeBytecode`). Break: damage the walk,
 *                      for example by passing a code size above the real one.
 *                      Observed: `the instruction does not fit the bytecode
 *                      (offset=<n>, size=<n>, remaining=<n>)` or `the decoded
 *                      instructions do not cover the bytecode (decoded=<n> of
 *                      <n> bytes)`.
 *   PROFILE            Link: the profile the warm-up left on a kind, the two
 *                      constructed states and the required slot state
 *                      (`LoadProfile`, `ConstructProfileStates`,
 *                      `CheckProfileState`). Break: drop the warm-up of a
 *                      profile kind, or feed it receiver shapes that do not
 *                      give the required state. Observed: `the warm-up left no
 *                      ProfileTypeInfo on the function; observed: <type>` or
 *                      `the access slot is not a mono case; observed: <slot> /
 *                      <slot>` for the state of a kind.
 *   COMPILER_VM        Link: the compiler VM of the JIT taskpool and the JIT
 *                      profile of the host VM (`SetUp`). Break: create the host
 *                      VM with fast JIT disabled, or with a taskpool that has
 *                      no compiler VM or whose current worker never publishes
 *                      one. Observed: `fast JIT is not enabled for
 *                      the fixture VM; observed:
 *                      Jit::IsEnableFastJit()=false`, `the JIT worker did not
 *                      complete the readiness handshake; observed: ...` (the
 *                      current worker never ran the handshake task; the pool is
 *                      stopped before teardown, so the outstanding handshake
 *                      task cannot reach host-VM cleanup),
 *                      `JitTaskpool::GetCompilerVm()=nullptr`, `the compiler VM
 *                      has no JIT thread; observed: ...` or `the fixture VM has
 *                      no JIT profile; observed: ...`.
 *   COMPILE_TRIGGERED  Link: the fixture methods stay below the compilation
 *                      trigger of the fixture VM (`LoadMethod`). Break: set the
 *                      JIT hotness threshold to a value the warm-up of a kind
 *                      reaches. Observed: `JIT compilation is in progress;
 *                      observed: JSFunction::IsJitCompiling()=true`
 *                      or `the method is already compiled; observed:
 *                      JSFunction::IsCompiledCode()=true`.
 *   JIT_THREAD_CONTEXT Link: the host VM and the current `JitTask` are bound on
 *                      the compiler VM and its JIT thread
 *                      (`EnsureJitThreadContext`). Break: skip or damage the
 *                      binding. Observed: `the JIT compiler VM or the host VM
 *                      is not ready; observed: <pointers>` or `the compiler VM
 *                      did not accept the host VM binding; observed:
 *                      JitThread::GetHostThread()=<p>, JitThread::GetCurrentTask()=<p>`.
 *   COMPILATION_ENV    Link: `kungfu::LazyDeoptAllDependencies` and
 *                      `JitCompilationEnv` are built for a kind
 *                      (`PrepareCompilationEnv`). Break: hand the link a method
 *                      without complete metadata, or without a base lexical
 *                      environment that resolves to a `JSGlobalEnv`. Observed:
 *                      `the method has no complete metadata; observed:
 *                      methodLiteral=<p>, bytecodeStart=<p>, bytecodeSize=<n>` or
 *                      `the lexical environment of the function is not a base
 *                      env; observed: <type>`.
 *
 * Every diagnostic is rendered by one formatter and has the form
 * `<STAGE>: method=<KIND>:` followed by the reason and by `; observed: <value>`,
 * where the scope is the `FixtureMethodKind` of the affected method or `case`
 * for a link that runs before the method table exists (the abc, the published
 * object, the compiler VM).
 */
enum class FixtureStage : uint8_t {
    NONE,
    ABC_EXECUTION,
    FUNCTION_LOOKUP,
    METHOD_METADATA,
    PROFILE,
    COMPILER_VM,
    COMPILE_TRIGGERED,
    JIT_THREAD_CONTEXT,
    COMPILATION_ENV,
    BYTECODE_DECODE,
};

// Text of a stage, `"UNKNOWN"` for a value outside the enumeration.
const char *FixtureStageName(FixtureStage stage);

/*
 * Data of one fixture method. All fields stay empty until the corresponding
 * link of the initialization filled them; `env`, `dependencies` and `broker`
 * belong to the fixture and are destroyed by its `TearDown`.
 */
struct FixtureMethod {
    JSHandle<JSFunction> function {};
    MethodLiteral *methodLiteral {nullptr};
    const uint8_t *bytecodeStart {nullptr};
    uint32_t bytecodeSize {0};
    JSPandaFile *pandaFile {nullptr};
    JSHandle<ProfileTypeInfo> profile {};
    JitCompilationEnv *env {nullptr};
    kungfu::LazyDeoptAllDependencies *dependencies {nullptr};
    ArkSteedHeapBroker *broker {nullptr};

    // Bytecode of this method with the requested opcode; `occurrence` counts
    // from zero in increasing offset order. `nullptr` means that the method
    // carries no such bytecode (or that its bytecode was not decoded).
    const kungfu::BytecodeInfo *FindBytecode(kungfu::EcmaOpcode opcode, uint32_t occurrence = 0) const;

private:
    // Decoded bytecode of this method in increasing offset order, filled by the
    // fixture from `bytecodeStart`/`bytecodeSize`. `FindBytecode` returns
    // pointers into it, so the table is not reachable for mutation through the
    // public API; the fixture owns it until `TearDown`.
    std::vector<kungfu::BytecodeInfo> bytecodes_ {};
    friend class ArkSteedBytecodeFixture;
};

class ArkSteedBytecodeFixture : public testing::Test {
public:
    // Threshold above the warm-up of the fixture script: the fixture methods
    // must stay below it, so that no compilation is triggered for them.
    static constexpr uint32_t JIT_HOTNESS_THRESHOLD = 30000;
    static constexpr uint32_t MAX_WARMUP_CALLS_PER_METHOD = 64;

    // Budget of one wait of the lifecycle handshake with the JIT worker. A
    // healthy worker publishes its compiler VM and retires the handshake task
    // within milliseconds; the budget turns a broken pool into a red case with
    // a diagnostic instead of a hang. `SetUp` stops the pool before it returns
    // on a failure, so the budget never leaves a task of the fixture in the
    // pool of a case that is about to tear its host VM down.
    static constexpr int64_t READINESS_BUDGET_MILLIS = 60 * 1000;

    // The first failed link, `NONE` only when every link of `SetUp` succeeded
    // and no later request failed.
    FixtureStage FailedStage() const;

    // Never null. `"ok"` when no link failed; otherwise the diagnostic of the
    // first failure in the form `<STAGE>: method=<KIND|case>: <reason>;
    // observed: <value>`, where the observed value is the value the link
    // actually saw. No data is substituted for a missing value and a failure is
    // never hidden.
    const char *FailureDiagnostic() const;

    // The host VM with its JIT options and its `JSThread`, and the `JitThread`
    // of the JIT compiler VM. All three are published only when every link of
    // `SetUp` succeeded: before `SetUp`, after `TearDown` and on a fixture whose
    // initialization failed they are null, never a partially initialized or an
    // already released object. A later request that fails (a kind whose
    // environment cannot be built) reports the failure through `FailedStage()`
    // and empties the method accessors; it never withdraws or replaces an
    // environment that is already completely initialized.
    EcmaVM *HostVm() const;
    JSThread *HostThread() const;
    // The JIT thread of the compiler VM (`IsJitThread()`), and the thread the
    // production profile-read path is bound to: `JitLockHolder` inside
    // `ArkSteedHeapBroker::SerializingScope` requires the environment thread to
    // be the current JIT thread, so a consumer runs its reads on this thread,
    // exactly as a compilation task does (`CompilerThread()` is also what the
    // reader takes as its thread). The fixture binds the host VM and the
    // current `JitTask` on it, so those reads see the same context production
    // sets up for one compilation task.
    JSThread *CompilerThread() const;

    // The real `JitTask` the fixture created for the case and bound as the
    // current task of `CompilerThread()`, or `nullptr` while no context is bound
    // yet (the first `GetMethod` request of the case creates it). A consumer that
    // dispatches work to the JIT taskpool builds the pool-owned task as
    // `JitTask::AsyncTask` on this task: the task walk of the VM destructor
    // (`Jit::ClearTaskWithVm` -> `Jit::ClearTask`) downcasts every task the pool
    // still references to that type and resolves the host VM through the wrapped
    // task, so a pool task of another type, or one without a task, would be an
    // invalid downcast. The fixture keeps ownership: the task outlives every
    // dispatch whose completion the case waited for and is deleted by `TearDown`
    // before the host VM.
    JitTask *PoolTask() const;

    // Data of one fixture method; the first request creates the objects the
    // fixture owns for that kind and caches them until `TearDown`. The empty
    // structure is returned instead of a partially filled one when the request
    // failed: on a case whose `SetUp` failed at any stage, and for a kind whose
    // own environment could not be built (`env == nullptr` and the failure
    // recorded in `FailedStage()`).
    const FixtureMethod &GetMethod(FixtureMethodKind kind);

    // Bytecode of the `OPERATION` site whose profile slot carries `hint`, or
    // `nullptr` when that state is not established: no site of that hint, or a
    // fixture whose initialization failed at any stage. The map is filled by
    // `SetUp` from the slots the warm-up sampled and from the slots written
    // with a raw `pgo::PGOSampleType` value; a failure never publishes it.
    const kungfu::BytecodeInfo *FindOperationSite(ArkSteedOperationHint hint) const;

    // Test-only control of the budget of the readiness handshake. The default
    // (`READINESS_BUDGET_MILLIS`) leaves a healthy worker far below the budget.
    // A zero budget cannot establish readiness: `SetUp` reports the bounded
    // failure and stops the pool, which the fixture-lifecycle case of
    // `feedback_reader_test.cpp` checks deterministically. The knob changes
    // nothing else of the fixture.
    void SetReadinessBudgetForTesting(std::chrono::milliseconds budget)
    {
        readinessBudget_ = budget;
    }

protected:
    void SetUp() override;
    void TearDown() override;

private:
    // The only place that assigns `failedStage_`, reached through the emitters
    // below. Renders `<STAGE>: method=<scope>: <reason>; observed: <observed>`,
    // keeps the first failed link for the rest of the case and returns false so
    // that a link reports through `return Fail<Link>(...)`.
    bool RecordFailure(FixtureStage stage, const std::string &scope, const std::string &reason,
                       const std::string &observed);

    /*
     * Emitters of the stages. Each one is the only place in the translation
     * unit that names its stage and the only caller of `RecordFailure` for it,
     * so one link produces one stage (see the comment above `FixtureStage` for
     * the link and the deliberate precondition break of each stage). A link
     * with several failure conditions calls its own emitter with the reason and
     * the observed value of each condition.
     */
    bool FailAbcExecution(const std::string &reason, const std::string &observed);
    bool FailFunctionLookup(const std::string &scope, const std::string &reason, const std::string &observed);
    bool FailMethodMetadata(const std::string &scope, const std::string &reason, const std::string &observed);
    bool FailBytecodeDecode(const std::string &scope, const std::string &reason, const std::string &observed);
    bool FailProfile(const std::string &scope, const std::string &reason, const std::string &observed);
    bool FailCompilerVm(const std::string &reason, const std::string &observed);
    bool FailCompileTriggered(const std::string &scope, const std::string &reason, const std::string &observed);
    bool FailJitThreadContext(const std::string &scope, const std::string &reason, const std::string &observed);
    bool FailCompilationEnv(const std::string &scope, const std::string &reason, const std::string &observed);

    // The empty value the accessors publish once a link has failed. No field of
    // it is ever filled, so a consumer that ignores the failure cannot read
    // substituted data.
    static const FixtureMethod &EmptyMethod();

    // Executes the fixture abc exactly once with the entry point and directory
    // of the published macros and resolves `globalThis.__arksteedFixtures`.
    bool ExecuteFixtureScript(JSHandle<JSTaggedValue> &fixtures);
    // Establishes that the compiler VM obtained afterwards belongs to the
    // current JIT worker. The JIT taskpool is a process-lifetime singleton whose
    // `compilerVm_` is not cleared when its worker destroys the compiler VM, so
    // a readiness wait in a later case would return on the stale pointer of the
    // previous case. The handshake posts a task to the pool and waits until the
    // current worker has run it (the worker runs its prologue, which publishes
    // the compiler VM, before it pops any task) and has retired it. Returns
    // false on timeout; no value is substituted. Before a false return the pool
    // is stopped, so a handshake task that is still queued or running can never
    // reach `Jit::ClearTaskWithVm` when the host VM is destroyed.
    bool AwaitCurrentJitWorker();
    // Resolves the published function of `kind`, reads the metadata of its
    // method and decodes its bytecode. Returns false after `Fail` and leaves
    // the entry of `kind` invalid.
    bool LoadMethod(FixtureMethodKind kind, const JSHandle<JSTaggedValue> &fixtures);
    // Decodes `method.bytecodeStart`/`method.bytecodeSize` with the production
    // conventions; returns false after `Fail` when the stream does not decode
    // into exactly `bytecodeSize` bytes.
    bool DecodeBytecode(FixtureMethodKind kind, FixtureMethod &method);
    // Binds the host VM and the current JIT task on the compiler VM and its JIT
    // thread the way production does for the duration of one compilation task
    // (`JitAsyncTaskRunScope`). The task is a real `JitTask` created once per
    // case through the public constructor for the function of the first
    // requested kind; it is never posted to the taskpool and never compiled.
    // A failure records JIT_THREAD_CONTEXT.
    bool EnsureJitThreadContext(FixtureMethodKind kind, FixtureMethod &method);
    // Creates and caches the objects `GetMethod` publishes for a kind:
    // `kungfu::LazyDeoptAllDependencies`, the `JitCompilationEnv` built like
    // production builds it (with the profile of the kind when one exists) and
    // the `ArkSteedHeapBroker` of that environment. Every precondition of the
    // environment is checked before the production constructor runs; a failure
    // records COMPILATION_ENV and leaves `env` null without a partial object.
    bool PrepareCompilationEnv(FixtureMethodKind kind, FixtureMethod &method);
    // Reads the `ProfileTypeInfo` the interpreter created for `kind` and
    // publishes it in `method.profile`. A kind whose profile is missing records
    // PROFILE; nothing is substituted for it.
    bool LoadProfile(FixtureMethodKind kind, FixtureMethod &method);
    // Writes the two constructed states of the domain (the polymorphic array
    // above the IC limit of `NAMED_POLY_OVERFLOW` and the raw sample types of
    // the `OPERATION` hints that natural sampling does not produce) and fills
    // the hint-to-site map of `FindOperationSite`. Records PROFILE when a state
    // cannot be established.
    bool ConstructProfileStates();
    bool ConstructNamedPolyOverflow();
    bool ConstructOperationSites();
    // Verifies the state of the access slot of `kind` against the `Architecture`
    // table of the domain page and records PROFILE with the observed value when
    // it does not match.
    bool CheckProfileState(FixtureMethodKind kind, FixtureMethod &method);

    FixtureStage failedStage_ {FixtureStage::NONE};
    std::string failureDiagnostic_ {};
    // Owned by the fixture: the handle scope of the host VM.
    EcmaHandleScope *scope_ {nullptr};
    // Owned by the fixture: exactly one host VM per case, released last.
    EcmaVM *hostVm_ {nullptr};
    JSThread *hostThread_ {nullptr};
    // Owned by the JIT taskpool, not by the fixture: the compiler VM and the
    // JIT thread that belongs to it. The fixture only binds and unbinds the
    // host VM on them. Both are published only after `SetUp` validated that the
    // pointer belongs to the current JIT worker, so `TearDown` never
    // dereferences a stale or half-validated VM.
    EcmaVM *compilerVm_ {nullptr};
    JSThread *compilerThread_ {nullptr};
    // Set by `EnsureJitThreadContext` when the host VM and the current task were
    // bound on the compiler VM and its JIT thread; only then does `TearDown`
    // unbind them. A binding caller that fails its verification still releases
    // the partial binding, because the flag is set before the verification.
    bool jitContextBound_ {false};
    // Budget of the readiness handshake of `SetUp`; the production value is
    // `READINESS_BUDGET_MILLIS`, a lifecycle case lowers it through
    // `SetReadinessBudgetForTesting`.
    std::chrono::milliseconds readinessBudget_ {READINESS_BUDGET_MILLIS};
    // Owned by the fixture: the real `JitTask` of the case, created by
    // `GetMethod` on its first request and released after the compiler VM has
    // been unbound from the host VM. Production binds exactly one task per
    // compilation; the fixture compiles nothing, so one task serves the case.
    JitTask *jitTask_ {nullptr};
    // The environment is handed out through the accessors only when every link
    // of `SetUp` succeeded; a partially created environment is released by
    // `TearDown` without ever becoming visible to the case.
    bool environmentReady_ {false};
    std::map<FixtureMethodKind, FixtureMethod> methods_ {};
    // Sites of the `OPERATION` kind per hint, filled by `SetUp`; the pointers
    // stay valid until `TearDown` clears the method table.
    std::map<ArkSteedOperationHint, const kungfu::BytecodeInfo *> operationSites_ {};
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_UNITTESTS_FIXTURES_BYTECODE_FIXTURE_H
