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
 * the domain; the profile slots are filled by the profile leaf.
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
 * link is left failed, not that a consumer's invariant holds.
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

    FixtureStage FailedStage() const;

    // Never null; the literal "ok" when no link failed.
    const char *FailureDiagnostic() const;

    // The host VM with its JIT options and its `JSThread`, and the `JitThread`
    // of the JIT compiler VM. All three are published only when every link of
    // `SetUp` succeeded: before `SetUp`, after `TearDown` and on a fixture whose
    // initialization failed they are null, never a partially initialized or an
    // already released object.
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

    // Data of one fixture method; the first request creates the objects the
    // fixture owns for that kind and caches them until `TearDown`.
    const FixtureMethod &GetMethod(FixtureMethodKind kind);

    // Bytecode of the `OPERATION` site whose profile slot carries `hint`, or
    // `nullptr` when that state is not established.
    const kungfu::BytecodeInfo *FindOperationSite(ArkSteedOperationHint hint) const;

protected:
    void SetUp() override;
    void TearDown() override;

private:
    // Records the first failed link of the initialization with a diagnostic
    // that names the link and the observed value. A later failure does not
    // overwrite it and no link ever clears it.
    void Fail(FixtureStage stage, const std::string &diagnostic);

    // Executes the fixture abc exactly once with the entry point and directory
    // of the published macros and resolves `globalThis.__arksteedFixtures`.
    bool ExecuteFixtureScript(JSHandle<JSTaggedValue> &fixtures);
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

    FixtureStage failedStage_ {FixtureStage::NONE};
    std::string failureDiagnostic_ {};
    // Owned by the fixture: the handle scope of the host VM.
    EcmaHandleScope *scope_ {nullptr};
    // Owned by the fixture: exactly one host VM per case, released last.
    EcmaVM *hostVm_ {nullptr};
    JSThread *hostThread_ {nullptr};
    // Owned by the JIT taskpool, not by the fixture: the compiler VM and the
    // JIT thread that belongs to it. The fixture only binds and unbinds the
    // host VM on them.
    EcmaVM *compilerVm_ {nullptr};
    JSThread *compilerThread_ {nullptr};
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
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_UNITTESTS_FIXTURES_BYTECODE_FIXTURE_H
