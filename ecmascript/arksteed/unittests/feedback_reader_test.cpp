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

/*
 * Unit domain closing matrix row G2.3u (contract C3 of
 * `arksteed-cpp-unit-test-domains`): the local invariants of
 * `ArkSteedFeedbackReader` — the IC slot a feedback read addresses, the processed
 * shape it publishes for a named, value, element, global, call and operation source,
 * and the states it refuses.
 *
 * The reader reaches the profile of a method through `ArkSteedHeapBroker`, and the
 * serializing scope of the broker takes `Jit::JitLockHolder`. That holder manages the
 * managed-code state of `env->GetJSThread()` — the JIT thread, which is where
 * production runs a compilation — and its Debug checks require that managed thread to
 * be the calling thread. Every read of this domain therefore runs the production path
 * on the JIT taskpool worker the fixture bound its environment to, exactly as a
 * compilation task does; the case thread only posts the work and waits for it. The
 * posted work is a pool-owned `JitTask::AsyncTask`, and the case waits until the pool
 * released that task again, not only until the body finished: the task walk of the VM
 * destructor (see `ReaderCallTaskAdapter`) downcasts every task the pool still
 * references, so teardown must not run while a dispatch is still owned by the pool.
 * While it waits the case leaves managed code (`ThreadNativeScope`), because a GC that has
 * to suspend all threads would otherwise wait forever for a thread blocked in
 * managed state.
 *
 * The environment and the profile come from `ArkSteedBytecodeFixture`
 * (`fixtures/bytecode_fixture.h`, domain `arksteed-host-unit-bytecode-fixture`): one
 * fixture method per profile state, decoded `kungfu::BytecodeInfo` of every site, the
 * `ProfileTypeInfo` of the method and its `ArkSteedHeapBroker`. Expectations are not
 * taken from the reader (D2): the fixture states the profile state of each kind (a
 * weak reference plus a handler, a polymorphic array of four case pairs, an array
 * above the IC limit, hole, an unexecuted slot, a `PropertyBox`, a `JSFunction`, a
 * negative builtin id, sampled integers), so the test compares the published fields
 * against that state, against the IC slot of the decoded site and against the
 * documented bit layout of a raw `pgo::PGOSampleType` value. The HClass reference and
 * the handler of every named, value and element case, the box of a global slot and the
 * target of a call are compared as exact values: the pair captured from the prepared
 * profile (rooted in handles on the host thread) is resolved against the reference the
 * reader published through the public `ArkSteedHeapBroker::TryResolveRef` path, so a
 * reader that repeats one case for every index or publishes a different valid object
 * is rejected. The comparison itself runs inside a live `ArkSteedHeapBroker`
 * serializing scope: resolution, the dereference of the rooted expectation, every
 * per-case comparison of a repeated read and the failure diagnostics share one scope,
 * so a collection cannot relocate a raw value between the resolution that produced it
 * and the comparison that consumes it. Only the identity of the resolved property name
 * is not compared (`ArkSteedHeapRef::operator==` is deleted); its presence is checked
 * instead.
 */

#include "ecmascript/arksteed/arksteed_feedback_reader.h"
#include "ecmascript/arksteed/unittests/fixtures/bytecode_fixture.h"

#include "common_components/taskpool/task.h"
#include "common_components/taskpool/taskpool.h"
#include "ecmascript/arksteed/arksteed_heap_broker.h"
#include "ecmascript/arksteed/arksteed_pgo_access_info.h"
#include "ecmascript/arksteed/arksteed_processed_feedback.h"
#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/compiler/bytecodes.h"
#include "ecmascript/ic/profile_type_info.h"
#include "ecmascript/jit/jit_task.h"
#include "ecmascript/pgo_profiler/types/pgo_profiler_type.h"
#include "ecmascript/tagged_array.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <variant>
#include <vector>

namespace panda::ecmascript::arksteed {

// Keep raw-value comparisons inside the broker's private read scope without
// exposing that scope to production callers.
class ArkSteedHeapBrokerTestAccess {
public:
    using SerializingScope = ArkSteedHeapBroker::SerializingScope;
};

namespace {

// Poly limit of the named and element inline caches, stated independently of the
// constant the reader compares against: the runtime IC caches at most four case
// pairs, and the domain contract C3 requires the limit itself to be checked.
constexpr uint32_t kExpectedPolyCases = 4;

// Bit layout of a raw `pgo::PGOSampleType` value: the primitive type occupies the low
// ten bits (`PGOSampleTemplate::Type::ANY` == 1023), the false weight starts at bit 10
// and the true weight at bit 21 (`WEIGHT_START_BIT` == 10, `WEIGHT_BITS` == 11).
constexpr uint32_t kSampleTypeMask = 0x3FF;
constexpr uint32_t kSampleFalseWeightShift = 10;
constexpr uint32_t kSampleTrueWeightShift = 21;
constexpr uint32_t kSampleWeightMask = 0x7FF;

// First integer literal of `fixtureLinear` (`value = value * 2`) — the first `ldai`
// of that fixture method, and the value the immediate form of the slot accessor has
// to publish.
constexpr uint32_t kLinearFirstImmediate = 2;

// Messages of the serializing scopes: the lock holder of the broker reports its
// message in a diagnostic, so every scope names the case that took it.
constexpr const char *kMonoScope = "ArkSteedFeedbackReaderTest.NamedAccessMonoSlotPublishesOneCase";
constexpr const char *kPolyScope = "ArkSteedFeedbackReaderTest.NamedAccessPolySlotPublishesEveryCaseAtTheLimit";
constexpr const char *kRefusedScope =
    "ArkSteedFeedbackReaderTest.NamedAccessRefusesOverflowMegaAndUnmaterializedSlots";
constexpr const char *kValueAndElementScope = "ArkSteedFeedbackReaderTest.ValueAndElementAccessStates";
constexpr const char *kGlobalAndCallScope = "ArkSteedFeedbackReaderTest.GlobalAccessAndCallFeedbackStates";
constexpr const char *kOperationScope = "ArkSteedFeedbackReaderTest.OperationFeedbackPublishesEveryHint";
constexpr const char *kRepeatedScope = "ArkSteedFeedbackReaderTest.RepeatedReadsAndImmediateLiteralAreStable";
constexpr const char *kDispatchScope =
    "ArkSteedFeedbackReaderTest.PoolDispatchReleasesTaskOwnershipBeforeTeardown";
constexpr const char *kMutationScope =
    "ArkSteedFeedbackReaderTest.MutationControlsRejectShiftedExpectationsAndSubstitutedNameInput";
// The comparison scopes: the comparisons that resolve a published reference and read
// the rooted expectation are serializing scopes of their own, so they are named here
// too. `TryResolveRef` opens and closes its own scope, so without an enclosing scope
// the raw value it returns would be compared after protection has ended.
constexpr const char *kResolveScope = "ArkSteedFeedbackReaderTest.ResolvesToProfileValue";
constexpr const char *kPolyProfileScope = "ArkSteedFeedbackReaderTest.MatchesPolyProfileCases";

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

// IC slot of a decoded access bytecode. `BytecodeInfo::InitBytecodeInfo` gives the
// access bytecodes one of two layouts: the named, element and call bytecodes carry
// the slot in `BytecodeInfo::slotId`, while the global bytecode carries it as its
// first `ICSlotId` input (`ecmascript/compiler/bytecodes.cpp`).
uint32_t SiteSlot(const kungfu::BytecodeInfo &info)
{
    if (info.slotId.GetId() != kungfu::ICSlotId::INVALID_ID) {
        return static_cast<uint32_t>(info.slotId.GetId());
    }
    for (const auto &input : info.inputs) {
        if (std::holds_alternative<kungfu::ICSlotId>(input)) {
            return static_cast<uint32_t>(std::get<kungfu::ICSlotId>(input).GetId());
        }
    }
    return static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID);
}

// Printable form of a profile slot value, so a mismatch between the state the fixture
// states and the state a case observes is readable without a debugger.
const char *SlotText(JSTaggedValue value)
{
    if (value.IsHole()) {
        return "hole";
    }
    if (value.IsWeak()) {
        return "weak reference";
    }
    if (value.IsTaggedArray()) {
        return "tagged array";
    }
    if (value.IsPropertyBox()) {
        return "PropertyBox";
    }
    if (value.IsJSFunction()) {
        return "JSFunction";
    }
    if (value.IsInt()) {
        return "int";
    }
    if (value.IsUndefined()) {
        return "undefined";
    }
    return "other";
}

/*
 * One case pair of an access IC as the prepared profile holds it: the weak HClass
 * reference the IC cached for the case and its handler. A pair is captured on the
 * host thread from the profile object of the fixture — never from the reader — and
 * rooted in handles of the case scope, so the comparison on the compilation thread
 * is made against the values the profile itself holds and no collected or relocated
 * object is compared.
 */
struct ProfileCasePair {
    JSHandle<JSTaggedValue> expectedHClass {};
    JSHandle<JSTaggedValue> handler {};
};

// Rooted copy of a value the prepared profile holds. The handle lives in the handle
// scope of the case (the host thread opened it in `SetUp`), so the value survives a
// collection between the capture and the comparison on the compilation thread.
JSHandle<JSTaggedValue> RootHostValue(JSThread *hostThread, JSTaggedValue value)
{
    return JSHandle<JSTaggedValue>(hostThread, value);
}

// The mono case pair of a prepared profile access slot: a weak HClass reference in
// the first slot and the handler in the second, which is the layout
// `ArkSteedFeedbackReader::TryGetICMonoSnapshot` reads.
ProfileCasePair CaptureMonoCasePair(JSThread *hostThread, ProfileTypeInfo *profile, uint32_t slot)
{
    JSTaggedValue weakHClass = profile->GetICSlot(hostThread, slot);
    return {RootHostValue(hostThread, JSTaggedValue(weakHClass.GetWeakReferent())),
            RootHostValue(hostThread, profile->GetICSlot(hostThread, slot + 1))};
}

// The case pairs of a polymorphic access slot of the prepared profile. `slot`
// addresses the tagged array whose pairs are a weak HClass reference and a handler
// (the layout `ArkSteedFeedbackReader::TryGetICPolyCase` reads).
std::vector<ProfileCasePair> CapturePolyCasePairs(JSThread *hostThread, ProfileTypeInfo *profile, uint32_t slot)
{
    JSTaggedValue first = profile->GetICSlot(hostThread, slot);
    auto *array = TaggedArray::Cast(first.GetTaggedObject());
    std::vector<ProfileCasePair> pairs;
    for (uint32_t index = 0; index < array->GetLength(); index += 2) {
        JSTaggedValue weakHClass = array->Get(hostThread, index);
        pairs.push_back({RootHostValue(hostThread, JSTaggedValue(weakHClass.GetWeakReferent())),
                         RootHostValue(hostThread, array->Get(hostThread, index + 1))});
    }
    return pairs;
}

// Resolves the reference the reader published through the public resolution path of
// the broker (`ArkSteedHeapBroker::TryResolveRef`, no friend and no export) and
// states whether it is the exact value the prepared profile holds. `what` names the
// compared field in the failure text. Resolution, the dereference of the rooted
// expectation and the failure diagnostic all run inside one live serializing scope:
// the scope of `TryResolveRef` is destroyed before it returns, so without this
// enclosing scope a collection could relocate the raw resolved value or the expected
// value between the resolution and the comparison. The nested scope of `TryResolveRef`
// finds the broker already serializing and therefore keeps this protection.
::testing::AssertionResult ResolvesToProfileValue(const ArkSteedHeapBroker *broker, const ArkSteedHeapRef &ref,
                                                  const JSHandle<JSTaggedValue> &expected, const char *what)
{
    ArkSteedHeapBrokerTestAccess::SerializingScope scope(broker, kResolveScope);
    JSTaggedValue resolved;
    if (!broker->TryResolveRef(ref, &resolved)) {
        return ::testing::AssertionFailure() << what << ": the published reference is not resolvable";
    }
    if (resolved != expected.GetTaggedValue()) {
        return ::testing::AssertionFailure()
               << what << ": the reader published " << SlotText(resolved) << " (" << resolved.GetRawData()
               << "), the profile holds " << SlotText(expected.GetTaggedValue()) << " ("
               << expected.GetTaggedValue().GetRawData() << ")";
    }
    return ::testing::AssertionSuccess();
}

// The detector of the polymorphic positive case: the conjunction of the fields that
// case asserts, with every published case reference and handler resolved through the
// public broker path and compared to the prepared profile. The sensitivity controls
// use the same detector, so a duplicated or substituted case cannot pass it. The whole
// conjunction runs inside one live serializing scope: every raw value a resolution
// returns stays protected until the comparison that consumes it, and the raw value of
// one case cannot be carried across the scope transitions of the next resolution.
bool MatchesPolyProfileCases(ArkSteedHeapBroker *broker, const NamedAccessFeedback &feedback, uint32_t expectedSlot,
                             const std::vector<ProfileCasePair> &expected)
{
    ArkSteedHeapBrokerTestAccess::SerializingScope scope(broker, kPolyProfileScope);
    if (feedback.base.kind != ProcessedFeedbackKind::NAMED_ACCESS ||
        feedback.base.source.slotId != expectedSlot || !feedback.base.source.isPoly || expected.empty() ||
        feedback.caseCount != expected.size() || !feedback.name.HasHandle()) {
        return false;
    }
    for (uint32_t index = 0; index < expected.size(); ++index) {
        JSTaggedValue hclass;
        JSTaggedValue handler;
        if (!broker->TryResolveRef(feedback.cases[index].expectedHClass, &hclass) ||
            !broker->TryResolveRef(feedback.cases[index].handler, &handler)) {
            return false;
        }
        if (hclass != expected[index].expectedHClass.GetTaggedValue() ||
            handler != expected[index].handler.GetTaggedValue()) {
            return false;
        }
    }
    return true;
}

// One operation site of the fixture: the bytecode the fixture published for a hint,
// the IC slot of that site and the fields the reader has to publish for the raw
// sample that site carries.
struct OperationExpectation {
    ArkSteedOperationHint hint {ArkSteedOperationHint::NONE};
    const kungfu::BytecodeInfo *site {nullptr};
    uint32_t slotId {0};
    uint32_t rawTypeBits {0};
    uint32_t trueWeight {0};
    uint32_t falseWeight {0};
};

/*
 * One body of a case, run on the JIT pool worker of the fixture. The serializing scope
 * of `ArkSteedHeapBroker` asserts in Debug that the managed thread of its compilation
 * environment is the calling thread, so a production feedback read cannot run on the
 * case thread; a compilation task has the same property. The body task is owned by the
 * case, while the pool owns the adapter that wraps it (`ReaderCallTaskAdapter`).
 */
class ReaderCallTask {
public:
    explicit ReaderCallTask(std::function<void()> body) : body_(std::move(body)) {}

    void Run()
    {
        JSThread *current = JSThread::GetCurrent();
        ranOnJitThread_ = current != nullptr && current->IsJitThread();
        if (ranOnJitThread_) {
            body_();
        }
    }

    bool RanOnJitThread() const
    {
        return ranOnJitThread_;
    }

private:
    std::function<void()> body_ {};
    bool ranOnJitThread_ {false};
};

/*
 * Release of the pool-owned adapter. The runner destroys the adapter after its `Run`
 * returned and after it cleared the running-task slot of the worker
 * (`common_components/taskpool/runner.cpp`), so this event is the moment the pool
 * holds no reference to the dispatch any more. The case waits for it instead of for
 * the body alone: a case that proceeded on the body would enter fixture teardown while
 * the worker still owns the adapter, and `Jit::ClearTaskWithVm` downcasts every task
 * the pool still references to `JitTask::AsyncTask` at VM destruction.
 */
class PoolDispatchCompletion {
public:
    void Release()
    {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

    void Wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return released_; });
    }

private:
    std::mutex mutex_ {};
    std::condition_variable condition_ {};
    bool released_ {false};
};

/*
 * Pool-owned adapter: the taskpool takes ownership of the posted task, the body task
 * stays owned by the case, so the case can still observe whether the body ran after the
 * pool released the adapter.
 *
 * The adapter derives from `JitTask::AsyncTask` because the JIT taskpool is shared with
 * the real JIT tasks and the VM destructor walks the pool and downcasts every task it
 * still references to that type (`Jit::ClearTaskWithVm` -> `Jit::ClearTask`). A task of
 * any other type would make that downcast read unrelated memory. `Run` is overridden, so
 * the base `Run` (a real optimization of the wrapped JIT task) never executes. The
 * wrapped task is the real `JitTask` of the fixture (`ArkSteedBytecodeFixture::PoolTask`),
 * which means `AsyncTask::GetHostVM()` resolves the host VM of this case as well. The
 * destructor releases the completion the case waits for; the runner calls it after `Run`
 * returned and after the worker cleared its running-task slot, that is, after every
 * reference of the pool to this dispatch is gone.
 */
class ReaderCallTaskAdapter : public JitTask::AsyncTask {
public:
    ReaderCallTaskAdapter(std::shared_ptr<JitTask> jitTask, int32_t id, std::shared_ptr<ReaderCallTask> task,
                          std::shared_ptr<PoolDispatchCompletion> completion)
        : JitTask::AsyncTask(std::move(jitTask), id), task_(std::move(task)), completion_(std::move(completion))
    {}

    bool Run(uint32_t) override
    {
        task_->Run();
        return true;
    }

    ~ReaderCallTaskAdapter() override
    {
        completion_->Release();
    }

private:
    std::shared_ptr<ReaderCallTask> task_ {};
    std::shared_ptr<PoolDispatchCompletion> completion_ {};
};

}  // namespace

class ArkSteedFeedbackReaderTest : public ArkSteedBytecodeFixture {
protected:
    void SetUp() override
    {
        ArkSteedBytecodeFixture::SetUp();
        // The fixture publishes no data once a link of its initialization failed, so
        // its diagnostic is the only evidence a case of this domain can report: a
        // failed fixture makes every case red, never skipped (F3).
        ASSERT_EQ(FailedStage(), FixtureStage::NONE) << FailureDiagnostic();
    }

    // Runs `body` on the JIT pool worker of the compiler VM of the fixture and waits
    // until the pool released the dispatch again. Assertions inside `body` are reported
    // as failures of the current case.
    void RunOnJitThread(std::function<void()> body)
    {
        ASSERT_NE(HostThread(), nullptr) << FailureDiagnostic();
        ASSERT_NE(CompilerThread(), nullptr) << FailureDiagnostic();
        ASSERT_TRUE(CompilerThread()->IsJitThread()) << FailureDiagnostic();
        // The pool adapter wraps this task (`JitTask::AsyncTask::GetHostVM` resolves
        // the host VM through it); the fixture created and bound it with the first
        // `GetMethod` request of the case.
        ASSERT_NE(PoolTask(), nullptr) << "the fixture bound no JIT task: " << FailureDiagnostic();

        auto task = std::make_shared<ReaderCallTask>(std::move(body));
        auto completion = std::make_shared<PoolDispatchCompletion>();
        // The fixture owns the JIT task; the shared pointer only satisfies the adapter
        // constructor and must not delete it (the fixture deletes it in `TearDown`,
        // after every dispatch the case waited for has left the pool).
        std::shared_ptr<JitTask> jitTask(PoolTask(), [](JitTask *) {});
        JitTaskpool::GetCurrentTaskpool()->PostTask(std::make_unique<ReaderCallTaskAdapter>(
            std::move(jitTask), HostThread()->GetThreadId(), task, completion));
        {
            // The case thread must leave managed code while it waits: a GC that has to
            // suspend all threads would otherwise wait for a thread that is blocked in
            // managed state and never reaches a safepoint.
            ThreadNativeScope nativeScope(HostThread());
            // Wait for the release of the pool-owned adapter, not for the body: only
            // then is the dispatch no longer owned by the pool, so the fixture teardown
            // that follows cannot meet a task the pool still references (see
            // `ReaderCallTaskAdapter`).
            completion->Wait();
        }
        ASSERT_TRUE(task->RanOnJitThread())
            << "the feedback read did not run on the JIT pool worker: " << FailureDiagnostic();
    }

    // The profile object of a fixture method, after the case checked that the fixture
    // published one. Slots are read the way the fixture reads them, on the host
    // thread; the reader reads the same object on the compilation thread.
    static ProfileTypeInfo *ProfileOf(const FixtureMethod &method)
    {
        return *method.profile;
    }
};

// The monomorphic named-access state: the reader publishes the name it resolves
// through the const-data pool, the HClass reference and the handler of the single IC
// case, and it addresses the IC slot of the site. Slot access is refused outside the
// serializing mode of the broker, which is the only path production uses.
TEST_F(ArkSteedFeedbackReaderTest, NamedAccessMonoSlotPublishesOneCase)
{
    const FixtureMethod &method = GetMethod(FixtureMethodKind::NAMED_MONO);
    ASSERT_NE(method.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(method.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(method.env, nullptr) << FailureDiagnostic();
    ASSERT_NE(method.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *site = method.FindBytecode(kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16);
    ASSERT_NE(site, nullptr) << "the fixture method carries no named-access site: " << FailureDiagnostic();

    const uint32_t slot = SiteSlot(*site);
    ASSERT_NE(slot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    ProfileTypeInfo *profile = ProfileOf(method);
    ASSERT_LT(slot + 1, profile->GetICSlotLength());
    const JSTaggedValue first = profile->GetICSlot(HostThread(), slot);
    const JSTaggedValue second = profile->GetICSlot(HostThread(), slot + 1);
    ASSERT_TRUE(first.IsWeak()) << "the fixture slot is not a mono case: " << SlotText(first);
    EXPECT_FALSE(second.IsWeak());
    // The expected pair comes from the prepared profile, captured on the host
    // thread and rooted in handles (the case thread is the host thread).
    const ProfileCasePair expected = CaptureMonoCasePair(HostThread(), profile, slot);

    RunOnJitThread([this, &method, site, slot, expected] {
        ArkSteedFeedbackReader reader(CompilerThread(), *site, method.broker);

        // Outside the serializing mode the reader reaches no profile: the call is
        // refused and no part of the output structure is filled.
        ProfileTypeInfo *outsideProfile = nullptr;
        EXPECT_FALSE(method.broker->SerializingAllowed());
        EXPECT_FALSE(method.broker->TryGetProfileTypeInfo(&outsideProfile));
        EXPECT_EQ(outsideProfile, nullptr);
        NamedAccessFeedback outsideFeedback {};
        EXPECT_FALSE(reader.ReadNamedAccessFeedback(0, &outsideFeedback));
        EXPECT_EQ(outsideFeedback.caseCount, 0U);
        EXPECT_FALSE(outsideFeedback.name.HasHandle());
        EXPECT_TRUE(outsideFeedback.base.IsInsufficient());

        NamedAccessFeedback feedback {};
        bool read = false;
        {
            ArkSteedHeapBrokerTestAccess::SerializingScope scope(method.broker, kMonoScope);
            read = reader.ReadNamedAccessFeedback(0, &feedback);
        }
        EXPECT_TRUE(read) << "the reader refused the mono named-access slot";
        EXPECT_EQ(feedback.base.kind, ProcessedFeedbackKind::NAMED_ACCESS);
        EXPECT_FALSE(feedback.base.IsInsufficient());
        EXPECT_EQ(feedback.base.source.slotId, slot);
        EXPECT_FALSE(feedback.base.source.isPoly);
        EXPECT_EQ(feedback.base.source.slotKind, AccessFeedbackSlotKind::UNKNOWN);
        EXPECT_TRUE(feedback.name.HasHandle()) << "the resolved name carries no reference";
        EXPECT_EQ(feedback.caseCount, 1U);
        EXPECT_TRUE(feedback.cases[0].expectedHClass.IsSafeForCompile());
        EXPECT_TRUE(feedback.cases[0].handler.IsSafeForCompile());
        EXPECT_TRUE(ResolvesToProfileValue(method.broker, feedback.cases[0].expectedHClass, expected.expectedHClass,
                                           "named mono expected HClass"));
        EXPECT_TRUE(ResolvesToProfileValue(method.broker, feedback.cases[0].handler, expected.handler,
                                           "named mono handler"));

        // The slot accessors: the primary form addresses the slot of the site, the
        // form that allows an immediate returns the IC slot operand, and an input
        // that is neither is refused.
        uint32_t slotId = 0;
        EXPECT_TRUE(reader.TryGetFeedbackSlotId(&slotId));
        EXPECT_EQ(slotId, slot);
        EXPECT_TRUE(reader.TryGetFeedbackSlotId(0, true, &slotId));
        EXPECT_EQ(slotId, slot);
        EXPECT_FALSE(reader.TryGetFeedbackSlotId(1, true, &slotId));
        EXPECT_FALSE(reader.TryGetFeedbackSlotId(1, false, &slotId));
        EXPECT_FALSE(reader.TryGetFeedbackSlotId(-1, true, &slotId));
    });
}

// The polymorphic named-access state at the IC limit: every case of the profile array
// is published, with the count the profile array holds, and the read is marked
// polymorphic.
TEST_F(ArkSteedFeedbackReaderTest, NamedAccessPolySlotPublishesEveryCaseAtTheLimit)
{
    const FixtureMethod &method = GetMethod(FixtureMethodKind::NAMED_POLY_LIMIT);
    ASSERT_NE(method.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(method.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(method.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *site = method.FindBytecode(kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16);
    ASSERT_NE(site, nullptr) << "the fixture method carries no named-access site: " << FailureDiagnostic();

    const uint32_t slot = SiteSlot(*site);
    ASSERT_NE(slot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    ProfileTypeInfo *profile = ProfileOf(method);
    ASSERT_LT(slot + 1, profile->GetICSlotLength());
    const JSTaggedValue first = profile->GetICSlot(HostThread(), slot);
    const JSTaggedValue second = profile->GetICSlot(HostThread(), slot + 1);
    ASSERT_TRUE(first.IsTaggedArray()) << "the fixture slot is not a poly array: " << SlotText(first);
    ASSERT_TRUE(second.IsHole()) << "the second slot of a poly case is not hole";
    ASSERT_EQ(TaggedArray::Cast(first.GetTaggedObject())->GetLength(), kExpectedPolyCases * 2);
    const std::vector<ProfileCasePair> expected = CapturePolyCasePairs(HostThread(), profile, slot);
    ASSERT_EQ(expected.size(), kExpectedPolyCases);
    // The profile HClasses are distinct, so the per-index comparison below rejects a
    // reader that repeats one poly case for every published index.
    for (uint32_t left = 0; left < expected.size(); ++left) {
        for (uint32_t right = left + 1; right < expected.size(); ++right) {
            ASSERT_NE(expected[left].expectedHClass.GetTaggedValue(),
                      expected[right].expectedHClass.GetTaggedValue())
                << "the fixture poly cases are not distinct: " << FailureDiagnostic();
        }
    }

    RunOnJitThread([this, &method, site, slot, expected] {
        ArkSteedFeedbackReader reader(CompilerThread(), *site, method.broker);
        NamedAccessFeedback feedback {};
        bool read = false;
        {
            ArkSteedHeapBrokerTestAccess::SerializingScope scope(method.broker, kPolyScope);
            read = reader.ReadNamedAccessFeedback(0, &feedback);
        }
        EXPECT_TRUE(read) << "the reader refused the poly named-access slot at the limit";
        EXPECT_EQ(feedback.base.kind, ProcessedFeedbackKind::NAMED_ACCESS);
        EXPECT_EQ(feedback.base.source.slotId, slot);
        EXPECT_TRUE(feedback.base.source.isPoly);
        EXPECT_EQ(feedback.base.source.slotKind, AccessFeedbackSlotKind::UNKNOWN);
        EXPECT_TRUE(feedback.name.HasHandle());
        // The count is the number of case pairs the profile array holds; the reader
        // publishes every one of them.
        EXPECT_EQ(feedback.caseCount, kExpectedPolyCases);
        for (uint32_t index = 0; index < feedback.caseCount; ++index) {
            EXPECT_TRUE(feedback.cases[index].expectedHClass.IsSafeForCompile());
            EXPECT_TRUE(feedback.cases[index].handler.IsSafeForCompile());
            EXPECT_TRUE(ResolvesToProfileValue(method.broker, feedback.cases[index].expectedHClass,
                                               expected[index].expectedHClass, "named poly expected HClass"));
            EXPECT_TRUE(ResolvesToProfileValue(method.broker, feedback.cases[index].handler, expected[index].handler,
                                               "named poly handler"));
        }
    });
}

// The named-access states the reader refuses: a polymorphic array above the IC limit
// (which must not be truncated to the limit), the megamorphic hole pair, and a slot
// the warm-up never initialized. Every refusal leaves the output structure empty.
TEST_F(ArkSteedFeedbackReaderTest, NamedAccessRefusesOverflowMegaAndUnmaterializedSlots)
{
    struct RefusedState {
        FixtureMethodKind kind;
        const char *text;
    };
    const RefusedState states[] = {
        {FixtureMethodKind::NAMED_POLY_OVERFLOW, "poly array above the IC limit"},
        {FixtureMethodKind::NAMED_MEGA, "megamorphic hole pair"},
        {FixtureMethodKind::EMPTY_SLOT, "unmaterialized slot"},
    };

    for (const RefusedState &state : states) {
        const FixtureMethod &method = GetMethod(state.kind);
        ASSERT_NE(method.methodLiteral, nullptr) << FailureDiagnostic();
        ASSERT_FALSE(method.profile.IsEmpty()) << FailureDiagnostic();
        ASSERT_NE(method.broker, nullptr) << FailureDiagnostic();
        const kungfu::BytecodeInfo *site = method.FindBytecode(kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16);
        ASSERT_NE(site, nullptr) << "the fixture method carries no named-access site: " << FailureDiagnostic();
        const uint32_t slot = SiteSlot(*site);
        ASSERT_NE(slot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
        ProfileTypeInfo *profile = ProfileOf(method);
        ASSERT_LT(slot + 1, profile->GetICSlotLength());
        const JSTaggedValue first = profile->GetICSlot(HostThread(), slot);
        const JSTaggedValue second = profile->GetICSlot(HostThread(), slot + 1);
        if (state.kind == FixtureMethodKind::NAMED_POLY_OVERFLOW) {
            // Constructed state of the fixture: one case pair more than the IC limit.
            ASSERT_TRUE(first.IsTaggedArray()) << SlotText(first);
            ASSERT_EQ(TaggedArray::Cast(first.GetTaggedObject())->GetLength(),
                      (kExpectedPolyCases + 1) * 2);
        } else if (state.kind == FixtureMethodKind::NAMED_MEGA) {
            ASSERT_TRUE(first.IsHole()) << SlotText(first);
            ASSERT_TRUE(second.IsHole()) << SlotText(second);
        } else {
            ASSERT_TRUE(first.IsUndefined()) << SlotText(first);
        }

        RunOnJitThread([this, &method, site, slot, &state] {
            ArkSteedFeedbackReader reader(CompilerThread(), *site, method.broker);
            NamedAccessFeedback feedback {};
            bool read = true;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(method.broker, kRefusedScope);
                read = reader.ReadNamedAccessFeedback(0, &feedback);
            }
            EXPECT_FALSE(read) << "the reader accepted the " << state.text;
            EXPECT_EQ(feedback.caseCount, 0U) << "the reader published a truncated case set for the " << state.text;
            EXPECT_TRUE(feedback.base.IsInsufficient());
            EXPECT_EQ(feedback.base.source.slotId, 0U);
            EXPECT_FALSE(feedback.name.HasHandle());
            EXPECT_FALSE(feedback.cases[0].expectedHClass.IsSafeForCompile());
            EXPECT_FALSE(feedback.cases[0].handler.IsSafeForCompile());
        });
    }
}

// The value-access and element-access readers of the same IC shapes: a mono pair and
// a poly array at the limit, plus an unmaterialized slot, which both readers refuse.
TEST_F(ArkSteedFeedbackReaderTest, ValueAndElementAccessStates)
{
    const FixtureMethod &mono = GetMethod(FixtureMethodKind::ELEMENT_MONO);
    ASSERT_NE(mono.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(mono.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(mono.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *monoSite = mono.FindBytecode(kungfu::EcmaOpcode::LDOBJBYVALUE_IMM8_V8);
    ASSERT_NE(monoSite, nullptr) << "the fixture method carries no element site: " << FailureDiagnostic();
    const uint32_t monoSlot = SiteSlot(*monoSite);
    ASSERT_NE(monoSlot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    ASSERT_TRUE(ProfileOf(mono)->GetICSlot(HostThread(), monoSlot).IsWeak())
        << "the fixture slot is not an element mono case";
    const ProfileCasePair monoExpected = CaptureMonoCasePair(HostThread(), ProfileOf(mono), monoSlot);

    const FixtureMethod &poly = GetMethod(FixtureMethodKind::ELEMENT_POLY_LIMIT);
    ASSERT_NE(poly.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(poly.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(poly.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *polySite = poly.FindBytecode(kungfu::EcmaOpcode::LDOBJBYVALUE_IMM8_V8);
    ASSERT_NE(polySite, nullptr) << "the fixture method carries no element site: " << FailureDiagnostic();
    const uint32_t polySlot = SiteSlot(*polySite);
    ASSERT_NE(polySlot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    const JSTaggedValue polyFirst = ProfileOf(poly)->GetICSlot(HostThread(), polySlot);
    ASSERT_TRUE(polyFirst.IsTaggedArray()) << SlotText(polyFirst);
    ASSERT_EQ(TaggedArray::Cast(polyFirst.GetTaggedObject())->GetLength(), kExpectedPolyCases * 2);
    const std::vector<ProfileCasePair> polyExpected = CapturePolyCasePairs(HostThread(), ProfileOf(poly), polySlot);
    ASSERT_EQ(polyExpected.size(), kExpectedPolyCases);
    for (uint32_t left = 0; left < polyExpected.size(); ++left) {
        for (uint32_t right = left + 1; right < polyExpected.size(); ++right) {
            ASSERT_NE(polyExpected[left].expectedHClass.GetTaggedValue(),
                      polyExpected[right].expectedHClass.GetTaggedValue())
                << "the fixture element poly cases are not distinct: " << FailureDiagnostic();
        }
    }

    const FixtureMethod &empty = GetMethod(FixtureMethodKind::EMPTY_SLOT);
    ASSERT_NE(empty.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(empty.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(empty.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *emptySite = empty.FindBytecode(kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16);
    ASSERT_NE(emptySite, nullptr) << FailureDiagnostic();
    const uint32_t emptySlot = SiteSlot(*emptySite);
    ASSERT_NE(emptySlot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    ASSERT_TRUE(ProfileOf(empty)->GetICSlot(HostThread(), emptySlot).IsUndefined());

    RunOnJitThread([this, &mono, monoSite, monoSlot, monoExpected, &poly, polySite, polySlot, polyExpected, &empty,
                    emptySite, emptySlot] {
        // Element access reader on the mono pair.
        {
            ArkSteedFeedbackReader reader(CompilerThread(), *monoSite, mono.broker);
            ElementAccessFeedback feedback {};
            bool read = false;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(mono.broker, kValueAndElementScope);
                read = reader.ReadElementAccessFeedback(0, &feedback);
            }
            EXPECT_TRUE(read);
            EXPECT_EQ(feedback.base.kind, ProcessedFeedbackKind::ELEMENT_ACCESS);
            EXPECT_EQ(feedback.base.source.slotId, monoSlot);
            EXPECT_FALSE(feedback.base.source.isPoly);
            EXPECT_EQ(feedback.base.source.slotKind, AccessFeedbackSlotKind::ELEMENT_STORE);
            EXPECT_EQ(feedback.caseCount, 1U);
            EXPECT_TRUE(feedback.cases[0].expectedHClass.IsSafeForCompile());
            EXPECT_TRUE(feedback.cases[0].handler.IsSafeForCompile());
            EXPECT_TRUE(ResolvesToProfileValue(mono.broker, feedback.cases[0].expectedHClass,
                                               monoExpected.expectedHClass, "element mono expected HClass"));
            EXPECT_TRUE(ResolvesToProfileValue(mono.broker, feedback.cases[0].handler, monoExpected.handler,
                                               "element mono handler"));
        }
        // Value access reader on the same mono pair: the element layout.
        {
            ArkSteedFeedbackReader reader(CompilerThread(), *monoSite, mono.broker);
            ValueAccessFeedback feedback {};
            bool read = false;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(mono.broker, kValueAndElementScope);
                read = reader.ReadValueAccessFeedback(&feedback);
            }
            EXPECT_TRUE(read);
            EXPECT_EQ(feedback.base.kind, ProcessedFeedbackKind::VALUE_ACCESS);
            EXPECT_EQ(feedback.kind, ValueAccessFeedbackKind::ELEMENT);
            EXPECT_EQ(feedback.base.source.slotId, monoSlot);
            EXPECT_FALSE(feedback.base.source.isPoly);
            EXPECT_EQ(feedback.base.source.slotKind, AccessFeedbackSlotKind::VALUE_LOAD);
            EXPECT_EQ(feedback.caseCount, 1U);
            EXPECT_TRUE(feedback.cases[0].expectedHClass.IsSafeForCompile());
            EXPECT_TRUE(feedback.cases[0].handler.IsSafeForCompile());
            EXPECT_TRUE(ResolvesToProfileValue(mono.broker, feedback.cases[0].expectedHClass,
                                               monoExpected.expectedHClass, "value mono expected HClass"));
            EXPECT_TRUE(ResolvesToProfileValue(mono.broker, feedback.cases[0].handler, monoExpected.handler,
                                               "value mono handler"));
        }
        // Element and value access on the poly array at the limit.
        {
            ArkSteedFeedbackReader reader(CompilerThread(), *polySite, poly.broker);
            ElementAccessFeedback elementFeedback {};
            ValueAccessFeedback valueFeedback {};
            bool elementRead = false;
            bool valueRead = false;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(poly.broker, kValueAndElementScope);
                elementRead = reader.ReadElementAccessFeedback(0, &elementFeedback);
                valueRead = reader.ReadValueAccessFeedback(&valueFeedback);
            }
            EXPECT_TRUE(elementRead);
            EXPECT_EQ(elementFeedback.base.source.slotId, polySlot);
            EXPECT_TRUE(elementFeedback.base.source.isPoly);
            EXPECT_EQ(elementFeedback.base.source.slotKind, AccessFeedbackSlotKind::ELEMENT_STORE);
            EXPECT_EQ(elementFeedback.caseCount, kExpectedPolyCases);
            EXPECT_TRUE(valueRead);
            EXPECT_EQ(valueFeedback.base.kind, ProcessedFeedbackKind::VALUE_ACCESS);
            EXPECT_EQ(valueFeedback.kind, ValueAccessFeedbackKind::ELEMENT);
            EXPECT_TRUE(valueFeedback.base.source.isPoly);
            EXPECT_EQ(valueFeedback.caseCount, kExpectedPolyCases);
            // Every published case of both readers is the exact pair of the profile,
            // index by index.
            for (uint32_t index = 0; index < polyExpected.size(); ++index) {
                EXPECT_TRUE(ResolvesToProfileValue(poly.broker, elementFeedback.cases[index].expectedHClass,
                                                   polyExpected[index].expectedHClass,
                                                   "element poly expected HClass"));
                EXPECT_TRUE(ResolvesToProfileValue(poly.broker, elementFeedback.cases[index].handler,
                                                   polyExpected[index].handler, "element poly handler"));
                EXPECT_TRUE(ResolvesToProfileValue(poly.broker, valueFeedback.cases[index].expectedHClass,
                                                   polyExpected[index].expectedHClass,
                                                   "value poly expected HClass"));
                EXPECT_TRUE(ResolvesToProfileValue(poly.broker, valueFeedback.cases[index].handler,
                                                   polyExpected[index].handler, "value poly handler"));
            }
        }
        // An unmaterialized slot: both readers refuse it and fill nothing.
        {
            ArkSteedFeedbackReader reader(CompilerThread(), *emptySite, empty.broker);
            ElementAccessFeedback elementFeedback {};
            ValueAccessFeedback valueFeedback {};
            bool elementRead = true;
            bool valueRead = true;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(empty.broker, kValueAndElementScope);
                elementRead = reader.ReadElementAccessFeedback(0, &elementFeedback);
                valueRead = reader.ReadValueAccessFeedback(&valueFeedback);
            }
            EXPECT_FALSE(elementRead);
            EXPECT_EQ(elementFeedback.caseCount, 0U);
            EXPECT_TRUE(elementFeedback.base.IsInsufficient());
            EXPECT_FALSE(valueRead);
            EXPECT_EQ(valueFeedback.caseCount, 0U);
            EXPECT_EQ(valueFeedback.kind, ValueAccessFeedbackKind::INSUFFICIENT);
            EXPECT_FALSE(valueFeedback.key.HasHandle());
        }
    });
}

// The global-access and call states: the reader publishes the box of a global IC slot,
// the JSFunction target of a call site, and the builtin id of a call site whose slot
// holds a negative builtin id. The same reader on an unmaterialized call slot reports
// an insufficient call instead of a target.
TEST_F(ArkSteedFeedbackReaderTest, GlobalAccessAndCallFeedbackStates)
{
    const FixtureMethod &global = GetMethod(FixtureMethodKind::GLOBAL_ACCESS);
    ASSERT_NE(global.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(global.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(global.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *globalSite = global.FindBytecode(kungfu::EcmaOpcode::LDGLOBALVAR_IMM16_ID16);
    ASSERT_NE(globalSite, nullptr) << "the fixture method carries no global site: " << FailureDiagnostic();
    const uint32_t globalSlot = SiteSlot(*globalSite);
    ASSERT_NE(globalSlot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    ASSERT_TRUE(ProfileOf(global)->GetICSlot(HostThread(), globalSlot).IsPropertyBox())
        << "the fixture slot is not a PropertyBox";
    const JSHandle<JSTaggedValue> expectedBox(HostThread(), ProfileOf(global)->GetICSlot(HostThread(), globalSlot));

    const FixtureMethod &js = GetMethod(FixtureMethodKind::CALL_JS);
    ASSERT_NE(js.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(js.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(js.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *jsSite = js.FindBytecode(kungfu::EcmaOpcode::CALLARG1_IMM8_V8);
    ASSERT_NE(jsSite, nullptr) << "the fixture method carries no call site: " << FailureDiagnostic();
    const uint32_t jsSlot = SiteSlot(*jsSite);
    ASSERT_NE(jsSlot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    ASSERT_TRUE(ProfileOf(js)->GetICSlot(HostThread(), jsSlot).IsJSFunction())
        << "the fixture call slot holds no JSFunction";
    const JSHandle<JSTaggedValue> expectedTarget(HostThread(), ProfileOf(js)->GetICSlot(HostThread(), jsSlot));
    // A second real JSFunction of the fixture, used by the sensitivity control below:
    // it is a valid target of a different call, so it must not pass as this one.
    const FixtureMethod &other = GetMethod(FixtureMethodKind::LINEAR);
    ASSERT_NE(other.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(other.function.IsEmpty()) << FailureDiagnostic();
    const JSHandle<JSTaggedValue> otherTarget(HostThread(), other.function.GetTaggedValue());
    ASSERT_NE(otherTarget.GetTaggedValue(), expectedTarget.GetTaggedValue())
        << "the fixture publishes the same function for two kinds";

    const FixtureMethod &builtin = GetMethod(FixtureMethodKind::CALL_BUILTIN);
    ASSERT_NE(builtin.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(builtin.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(builtin.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *builtinSite = builtin.FindBytecode(kungfu::EcmaOpcode::CALLARG1_IMM8_V8);
    ASSERT_NE(builtinSite, nullptr) << "the fixture method carries no call site: " << FailureDiagnostic();
    const uint32_t builtinSlot = SiteSlot(*builtinSite);
    ASSERT_NE(builtinSlot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    const JSTaggedValue builtinValue = ProfileOf(builtin)->GetICSlot(HostThread(), builtinSlot);
    ASSERT_TRUE(builtinValue.IsInt() && builtinValue.GetInt() < 0)
        << "the fixture call slot holds no negative builtin id: " << SlotText(builtinValue);
    const int64_t expectedBuiltinId = -static_cast<int64_t>(builtinValue.GetInt());

    const FixtureMethod &empty = GetMethod(FixtureMethodKind::EMPTY_SLOT);
    ASSERT_NE(empty.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(empty.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(empty.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *emptySite = empty.FindBytecode(kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16);
    ASSERT_NE(emptySite, nullptr) << FailureDiagnostic();
    const uint32_t emptySlot = SiteSlot(*emptySite);
    ASSERT_NE(emptySlot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    ASSERT_TRUE(ProfileOf(empty)->GetICSlot(HostThread(), emptySlot).IsUndefined());

    RunOnJitThread([this, &global, globalSite, globalSlot, expectedBox, &js, jsSite, jsSlot, expectedTarget,
                    otherTarget, expectedBuiltinId, &builtin, builtinSite, builtinSlot, &empty, emptySite,
                    emptySlot] {
        // Global access: the reader publishes the box of the slot.
        {
            ArkSteedFeedbackReader reader(CompilerThread(), *globalSite, global.broker);
            GlobalAccessFeedback feedback {};
            bool read = false;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(global.broker, kGlobalAndCallScope);
                read = reader.ReadGlobalAccessFeedback(&feedback);
            }
            EXPECT_TRUE(read);
            EXPECT_EQ(feedback.base.kind, ProcessedFeedbackKind::GLOBAL_ACCESS);
            EXPECT_FALSE(feedback.base.IsInsufficient());
            EXPECT_EQ(feedback.base.source.slotId, globalSlot);
            EXPECT_FALSE(feedback.base.source.isPoly);
            EXPECT_TRUE(feedback.box.IsSafeForCompile());
            EXPECT_TRUE(feedback.box.HasHandle());
            EXPECT_TRUE(ResolvesToProfileValue(global.broker, feedback.box, expectedBox, "global access box"));
            // Sensitivity control: another heap object does not pass as the profile's
            // PropertyBox, so a reader that publishes a different valid object is
            // rejected by the comparison above.
            ArkSteedObjectRef otherBox;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(global.broker, kGlobalAndCallScope);
                otherBox = global.broker->MakeObjectRef(otherTarget.GetTaggedValue());
            }
            ASSERT_TRUE(otherBox.IsSafeForCompile());
            EXPECT_FALSE(ResolvesToProfileValue(global.broker, otherBox, expectedBox, "global access box"));
            // The value-access reader of the same slot sees a PropertyBox, which is
            // neither an element nor a named value case, and refuses it.
            ValueAccessFeedback valueFeedback {};
            bool valueRead = true;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(global.broker, kGlobalAndCallScope);
                valueRead = reader.ReadValueAccessFeedback(&valueFeedback);
            }
            EXPECT_FALSE(valueRead);
            EXPECT_EQ(valueFeedback.caseCount, 0U);
            EXPECT_EQ(valueFeedback.kind, ValueAccessFeedbackKind::INSUFFICIENT);
        }
        // A call site that monomorphically targets a JS function.
        {
            ArkSteedFeedbackReader reader(CompilerThread(), *jsSite, js.broker);
            CallFeedback feedback {};
            bool read = false;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(js.broker, kGlobalAndCallScope);
                read = reader.ReadCallFeedback(&feedback);
            }
            EXPECT_TRUE(read);
            EXPECT_TRUE(feedback.IsMonomorphicJSFunction());
            EXPECT_EQ(feedback.slotId, jsSlot);
            EXPECT_TRUE(feedback.target.HasHandle());
            EXPECT_TRUE(feedback.target.IsSafeForCompile());
            EXPECT_EQ(feedback.builtinId, kungfu::BuiltinsStubCSigns::ID::NONE);
            EXPECT_TRUE(ResolvesToProfileValue(js.broker, feedback.target, expectedTarget, "call JS target"));
            // Sensitivity control: a different valid JSFunction is rejected.
            ArkSteedFunctionRef substitutedTarget;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(js.broker, kGlobalAndCallScope);
                substitutedTarget = js.broker->MakeFunctionRef(otherTarget.GetTaggedValue());
            }
            ASSERT_TRUE(substitutedTarget.IsSafeForCompile());
            EXPECT_FALSE(ResolvesToProfileValue(js.broker, substitutedTarget, expectedTarget, "call JS target"));
        }
        // A call site whose slot holds a negative builtin id, cross-checked against
        // the id stored in the profile slot.
        {
            ArkSteedFeedbackReader reader(CompilerThread(), *builtinSite, builtin.broker);
            CallFeedback feedback {};
            bool read = false;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(builtin.broker, kGlobalAndCallScope);
                read = reader.ReadCallFeedback(&feedback);
            }
            EXPECT_TRUE(read);
            EXPECT_TRUE(feedback.IsMonomorphicBuiltin());
            EXPECT_EQ(feedback.slotId, builtinSlot);
            EXPECT_FALSE(feedback.target.HasHandle());
            EXPECT_EQ(static_cast<int64_t>(feedback.builtinId), expectedBuiltinId);
            EXPECT_GT(feedback.builtinId, kungfu::BuiltinsStubCSigns::NONE);
            EXPECT_LT(feedback.builtinId, kungfu::BuiltinsStubCSigns::ID::NUM_OF_BUILTINS_ID);
        }
        // A call site that never ran: the slot is undefined, so the read reports an
        // insufficient call with the addressed slot, not a target.
        {
            ArkSteedFeedbackReader reader(CompilerThread(), *emptySite, empty.broker);
            CallFeedback feedback {};
            bool read = false;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(empty.broker, kValueAndElementScope);
                read = reader.ReadCallFeedback(&feedback);
            }
            EXPECT_TRUE(read);
            EXPECT_EQ(feedback.kind, CallFeedbackKind::INSUFFICIENT);
            EXPECT_EQ(feedback.slotId, emptySlot);
            EXPECT_FALSE(feedback.target.HasHandle());
        }
    });
}

// Every operation hint the fixture establishes: the reader publishes the hint of the
// raw sample the profile slot holds, the slot it read, and the weights encoded in that
// sample (documented `pgo::PGOSampleType` bit layout, not a value read back from the
// reader).
TEST_F(ArkSteedFeedbackReaderTest, OperationFeedbackPublishesEveryHint)
{
    const FixtureMethod &method = GetMethod(FixtureMethodKind::OPERATION);
    ASSERT_NE(method.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(method.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(method.broker, nullptr) << FailureDiagnostic();
    ProfileTypeInfo *profile = ProfileOf(method);

    std::vector<OperationExpectation> expectations;
    for (ArkSteedOperationHint hint : kOperationHints) {
        const kungfu::BytecodeInfo *site = FindOperationSite(hint);
        ASSERT_NE(site, nullptr) << "the fixture published no site for hint " << static_cast<int>(hint) << ": "
                                 << FailureDiagnostic();
        const uint32_t slot = SiteSlot(*site);
        ASSERT_NE(slot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
        ASSERT_LT(slot, profile->GetICSlotLength());
        const JSTaggedValue slotValue = profile->GetICSlot(HostThread(), slot);
        ASSERT_TRUE(slotValue.IsInt()) << "the operation slot carries no sampled integer: " << SlotText(slotValue);
        const uint32_t raw = static_cast<uint32_t>(slotValue.GetInt());
        expectations.push_back({hint, site, slot, raw & kSampleTypeMask, raw >> kSampleTrueWeightShift,
                                (raw >> kSampleFalseWeightShift) & kSampleWeightMask});
    }
    ASSERT_EQ(expectations.size(), sizeof(kOperationHints) / sizeof(kOperationHints[0]));

    RunOnJitThread([this, &method, &expectations] {
        for (const OperationExpectation &expectation : expectations) {
            ArkSteedFeedbackReader reader(CompilerThread(), *expectation.site, method.broker);
            OperationFeedback feedback {};
            bool read = false;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(method.broker, kOperationScope);
                read = reader.ReadOperationFeedback(&feedback);
            }
            EXPECT_TRUE(read) << "the reader refused the operation slot of hint "
                              << static_cast<int>(expectation.hint);
            EXPECT_EQ(feedback.hint, expectation.hint);
            EXPECT_EQ(feedback.slotId, expectation.slotId);
            EXPECT_EQ(feedback.rawTypeBits, expectation.rawTypeBits);
            EXPECT_EQ(feedback.trueWeight, expectation.trueWeight);
            EXPECT_EQ(feedback.falseWeight, expectation.falseWeight);
            EXPECT_EQ(feedback.IsInsufficient(), expectation.hint == ArkSteedOperationHint::NONE);
            uint32_t slotId = 0;
            EXPECT_TRUE(reader.TryGetFeedbackSlotId(&slotId));
            EXPECT_EQ(slotId, expectation.slotId);
        }
    });
}

// Determinism of the reader on an unchanged profile: a repeated read of the same slot
// publishes the same observable fields, and an immediate literal of a method without
// any profile site is still reachable through the immediate form of the slot accessor.
TEST_F(ArkSteedFeedbackReaderTest, RepeatedReadsAndImmediateLiteralAreStable)
{
    const FixtureMethod &method = GetMethod(FixtureMethodKind::NAMED_POLY_LIMIT);
    ASSERT_NE(method.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(method.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(method.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *site = method.FindBytecode(kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16);
    ASSERT_NE(site, nullptr) << "the fixture method carries no named-access site: " << FailureDiagnostic();
    const uint32_t slot = SiteSlot(*site);
    ASSERT_NE(slot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    ASSERT_LT(slot + 1, ProfileOf(method)->GetICSlotLength());
    const std::vector<ProfileCasePair> expected = CapturePolyCasePairs(HostThread(), ProfileOf(method), slot);
    ASSERT_EQ(expected.size(), kExpectedPolyCases);

    const FixtureMethod &linear = GetMethod(FixtureMethodKind::LINEAR);
    ASSERT_NE(linear.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_NE(linear.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *immediateSite =
        linear.FindBytecode(kungfu::EcmaOpcode::LDAI_IMM32, 0);
    ASSERT_NE(immediateSite, nullptr) << "the fixture method carries no immediate site: " << FailureDiagnostic();
    ASSERT_FALSE(immediateSite->inputs.empty());
    ASSERT_TRUE(std::holds_alternative<kungfu::Immediate>(immediateSite->inputs[0]))
        << "the first input of the fixture immediate site is not an immediate";

    RunOnJitThread([this, &method, site, slot, expected, &linear, immediateSite] {
        NamedAccessFeedback first {};
        NamedAccessFeedback second {};
        {
            // The two reads and every comparison of the values they publish run under
            // one live serializing scope. `TryResolveRef` opens and closes its own
            // scope, so the raw values compared below stay protected from a collection
            // between the resolution that produced them and the comparison that reads
            // them, and a value of the first read cannot be relocated while the second
            // read is resolved.
            ArkSteedHeapBrokerTestAccess::SerializingScope scope(method.broker, kRepeatedScope);
            ArkSteedFeedbackReader reader(CompilerThread(), *site, method.broker);
            EXPECT_TRUE(reader.ReadNamedAccessFeedback(0, &first));
            EXPECT_TRUE(reader.ReadNamedAccessFeedback(0, &second));

            EXPECT_EQ(second.base.kind, first.base.kind);
            EXPECT_EQ(second.base.source.slotId, first.base.source.slotId);
            EXPECT_EQ(second.base.source.isPoly, first.base.source.isPoly);
            EXPECT_EQ(second.base.source.slotKind, first.base.source.slotKind);
            ASSERT_EQ(first.caseCount, expected.size());
            EXPECT_EQ(second.caseCount, first.caseCount);
            ASSERT_TRUE(first.name.IsSafeForCompile());
            ASSERT_TRUE(second.name.IsSafeForCompile());
            JSTaggedValue firstName;
            JSTaggedValue secondName;
            ASSERT_TRUE(method.broker->TryResolveRef(first.name, &firstName));
            ASSERT_TRUE(method.broker->TryResolveRef(second.name, &secondName));
            EXPECT_EQ(secondName, firstName);
            // Both reads publish the exact profile case, and they publish the same
            // values as each other, not merely the same safety booleans.
            for (uint32_t index = 0; index < first.caseCount; ++index) {
                EXPECT_TRUE(ResolvesToProfileValue(method.broker, first.cases[index].expectedHClass,
                                                   expected[index].expectedHClass, "first read expected HClass"));
                EXPECT_TRUE(ResolvesToProfileValue(method.broker, second.cases[index].expectedHClass,
                                                   expected[index].expectedHClass, "second read expected HClass"));
                EXPECT_TRUE(ResolvesToProfileValue(method.broker, first.cases[index].handler, expected[index].handler,
                                                   "first read handler"));
                EXPECT_TRUE(ResolvesToProfileValue(method.broker, second.cases[index].handler, expected[index].handler,
                                                   "second read handler"));
                JSTaggedValue firstHClass;
                JSTaggedValue secondHClass;
                JSTaggedValue firstHandler;
                JSTaggedValue secondHandler;
                ASSERT_TRUE(method.broker->TryResolveRef(first.cases[index].expectedHClass, &firstHClass));
                ASSERT_TRUE(method.broker->TryResolveRef(second.cases[index].expectedHClass, &secondHClass));
                ASSERT_TRUE(method.broker->TryResolveRef(first.cases[index].handler, &firstHandler));
                ASSERT_TRUE(method.broker->TryResolveRef(second.cases[index].handler, &secondHandler));
                EXPECT_EQ(secondHClass, firstHClass);
                EXPECT_EQ(secondHandler, firstHandler);
            }
        }

        // The immediate form of the slot accessor reads the literal operand of the
        // bytecode; the fixture method `LINEAR` computes `value * 2` first, so the
        // first `ldai` of that method carries 2.
        ArkSteedFeedbackReader linearReader(CompilerThread(), *immediateSite, linear.broker);
        uint32_t immediateSlot = 0;
        EXPECT_TRUE(linearReader.TryGetFeedbackSlotId(0, true, &immediateSlot));
        EXPECT_EQ(immediateSlot, kLinearFirstImmediate);
        EXPECT_EQ(immediateSlot, static_cast<uint32_t>(std::get<kungfu::Immediate>(immediateSite->inputs[0])
                                                           .GetValue()));
        EXPECT_FALSE(linearReader.TryGetFeedbackSlotId(&immediateSlot));
    });
}

// Regression case for the dispatch lifecycle (pool ownership). Every dispatch enters the
// completion-to-teardown window once: the body finishes on the pool worker, the worker
// clears its running-task slot, and the pool releases the adapter. The case repeats the
// dispatch so that the window is entered many times before the fixture teardown that
// follows the last one, and `RunOnJitThread` waits for the release of the pool-owned
// adapter each time. A dispatch still owned by the pool when the case ended would leave
// a task in the pool whose memory the host VM destruction walks with the
// `JitTask::AsyncTask` downcast (`Jit::ClearTaskWithVm`).
TEST_F(ArkSteedFeedbackReaderTest, PoolDispatchReleasesTaskOwnershipBeforeTeardown)
{
    const FixtureMethod &method = GetMethod(FixtureMethodKind::NAMED_MONO);
    ASSERT_NE(method.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(method.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(method.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *site = method.FindBytecode(kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16);
    ASSERT_NE(site, nullptr) << "the fixture method carries no named-access site: " << FailureDiagnostic();

    // The dispatch count is high enough for a missing release to be observed rather than
    // remaining probabilistic; each dispatch is a full production read on the JIT worker.
    constexpr uint32_t kDispatches = 64;
    uint32_t completed = 0;
    for (uint32_t index = 0; index < kDispatches; ++index) {
        RunOnJitThread([this, &method, site, &completed] {
            ArkSteedFeedbackReader reader(CompilerThread(), *site, method.broker);
            NamedAccessFeedback feedback {};
            bool read = false;
            {
                ArkSteedHeapBrokerTestAccess::SerializingScope scope(method.broker, kDispatchScope);
                read = reader.ReadNamedAccessFeedback(0, &feedback);
            }
            EXPECT_TRUE(read) << "the reader refused the mono named-access slot on dispatch "
                              << completed;
            EXPECT_EQ(feedback.base.kind, ProcessedFeedbackKind::NAMED_ACCESS);
            EXPECT_EQ(feedback.caseCount, 1U);
            if (read) {
                ++completed;
            }
        });
    }
    // Every read ran on the JIT worker and completed; the case then returns and the
    // fixture teardown follows immediately, with no dispatch left in the pool.
    EXPECT_EQ(completed, kDispatches);
}

// Negative control of the domain (H6, P0). The expectations of the positive case are
// sensitive: a shifted case count, a shifted slot, a duplicated case, swapped cases, a
// substituted case reference, a substituted handler and a substituted name input are
// all rejected by the same detector the positive case uses, and the production call on
// the substituted input is refused with an empty structure.
TEST_F(ArkSteedFeedbackReaderTest, MutationControlsRejectShiftedExpectationsAndSubstitutedNameInput)
{
    const FixtureMethod &method = GetMethod(FixtureMethodKind::NAMED_POLY_LIMIT);
    ASSERT_NE(method.methodLiteral, nullptr) << FailureDiagnostic();
    ASSERT_FALSE(method.profile.IsEmpty()) << FailureDiagnostic();
    ASSERT_NE(method.broker, nullptr) << FailureDiagnostic();
    const kungfu::BytecodeInfo *site = method.FindBytecode(kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16);
    ASSERT_NE(site, nullptr) << "the fixture method carries no named-access site: " << FailureDiagnostic();
    const uint32_t slot = SiteSlot(*site);
    ASSERT_NE(slot, static_cast<uint32_t>(kungfu::ICSlotId::INVALID_ID));
    ASSERT_LT(slot + 1, ProfileOf(method)->GetICSlotLength());
    const std::vector<ProfileCasePair> expected = CapturePolyCasePairs(HostThread(), ProfileOf(method), slot);
    ASSERT_EQ(expected.size(), kExpectedPolyCases);
    // The profile HClasses are distinct, so the substitution and duplication controls
    // below change the value a case reference resolves to.
    for (uint32_t left = 0; left < expected.size(); ++left) {
        for (uint32_t right = left + 1; right < expected.size(); ++right) {
            ASSERT_NE(expected[left].expectedHClass.GetTaggedValue(),
                      expected[right].expectedHClass.GetTaggedValue())
                << "the fixture poly cases are not distinct: " << FailureDiagnostic();
        }
    }

    RunOnJitThread([this, &method, site, slot, expected] {
        NamedAccessFeedback feedback {};
        bool read = false;
        {
            ArkSteedHeapBrokerTestAccess::SerializingScope scope(method.broker, kMutationScope);
            ArkSteedFeedbackReader reader(CompilerThread(), *site, method.broker);
            read = reader.ReadNamedAccessFeedback(0, &feedback);
        }
        EXPECT_TRUE(read);
        EXPECT_TRUE(MatchesPolyProfileCases(method.broker, feedback, slot, expected));

        // Shifted expectations: a different case count and a different slot are
        // rejected by the same detector the positive case uses.
        std::vector<ProfileCasePair> fewerCases = expected;
        fewerCases.pop_back();
        EXPECT_FALSE(MatchesPolyProfileCases(method.broker, feedback, slot, fewerCases));
        EXPECT_FALSE(MatchesPolyProfileCases(method.broker, feedback, slot + 1, expected));

        // Duplicated case: a reader that publishes the first case for a later index is
        // rejected because the profile cases are distinct.
        NamedAccessFeedback duplicated = feedback;
        duplicated.cases[1] = duplicated.cases[0];
        EXPECT_FALSE(MatchesPolyProfileCases(method.broker, duplicated, slot, expected))
            << "the detector accepted a duplicated poly case";

        // Swapped cases: the order of the published cases is part of the contract.
        NamedAccessFeedback swapped = feedback;
        std::swap(swapped.cases[0], swapped.cases[1]);
        EXPECT_FALSE(MatchesPolyProfileCases(method.broker, swapped, slot, expected))
            << "the detector accepted swapped poly cases";

        // Substituted case reference and substituted handler: the detector inspects
        // both values of a pair, not only the case reference.
        NamedAccessFeedback substitutedClass = feedback;
        substitutedClass.cases[2].expectedHClass = feedback.cases[0].expectedHClass;
        EXPECT_FALSE(MatchesPolyProfileCases(method.broker, substitutedClass, slot, expected))
            << "the detector accepted a substituted poly HClass";
        NamedAccessFeedback substitutedHandler = feedback;
        substitutedHandler.cases[2].handler = feedback.cases[0].expectedHClass;
        EXPECT_FALSE(MatchesPolyProfileCases(method.broker, substitutedHandler, slot, expected))
            << "the detector accepted a substituted handler";

        // Substituted name input: the same production call on a copy of the site whose
        // second input is an IC slot id instead of the const-data id of the property
        // name. The reader must refuse it and fill nothing, while the same call on the
        // unchanged site reads true (checked above).
        kungfu::BytecodeInfo substituted = *site;
        ASSERT_GT(substituted.inputs.size(), 1U);
        substituted.inputs[1] = kungfu::ICSlotId(static_cast<uint16_t>(slot));
        NamedAccessFeedback substitutedFeedback {};
        bool substitutedRead = true;
        {
            ArkSteedHeapBrokerTestAccess::SerializingScope scope(method.broker, kMutationScope);
            ArkSteedFeedbackReader substitutedReader(CompilerThread(), substituted, method.broker);
            substitutedRead = substitutedReader.ReadNamedAccessFeedback(0, &substitutedFeedback);
        }
        EXPECT_FALSE(substitutedRead) << "the reader accepted a name input that is not a const-data id";
        EXPECT_EQ(substitutedFeedback.caseCount, 0U);
        EXPECT_FALSE(substitutedFeedback.name.HasHandle());
        EXPECT_TRUE(substitutedFeedback.base.IsInsufficient());
    });
}

/*
 * Lifecycle check of the bytecode fixture (domain
 * `arksteed-host-unit-bytecode-fixture`), independent of the feedback-reader domain:
 * it asserts no invariant of `BytecodePreprocessor`/`BytecodeAnalysis` and
 * `--gtest_filter=ArkSteedFeedbackReaderTest.*` does not select it.
 *
 * The readiness handshake of the fixture is bounded. This case reaches its
 * failure path deterministically with a zero budget and proves that the case
 * still tears down safely. The failure leaves the handshake task in the JIT
 * taskpool, and `Jit::ClearTaskWithVm` -- reached from `~EcmaVM` when the host
 * VM is destroyed -- casts every task it finds there to `JitTask::AsyncTask`,
 * which a plain `common::Task` is not. The fixture stops the pool before it
 * returns, so the task is drained and retired and host-VM teardown finds no
 * incompatible task. The case is green only when the failure is bounded, the
 * diagnostic names the failed link and the process survives the teardown; a
 * crash or a hang fails the target.
 */
class ArkSteedBytecodeFixtureLifecycleTest : public ArkSteedBytecodeFixture {
protected:
    void SetUp() override
    {
        // A zero budget cannot establish the readiness of the current JIT
        // worker; the failure path (and its teardown) is the production one.
        SetReadinessBudgetForTesting(std::chrono::milliseconds(0));
        ArkSteedBytecodeFixture::SetUp();
    }
};

TEST_F(ArkSteedBytecodeFixtureLifecycleTest, OutOfBudgetReadinessFailsBoundedAndTearsDown)
{
    EXPECT_EQ(FailedStage(), FixtureStage::COMPILER_VM) << FailureDiagnostic();
    // A failed fixture publishes no VM and no compiler thread; teardown must
    // not dereference anything it did not validate.
    EXPECT_EQ(HostVm(), nullptr) << FailureDiagnostic();
    EXPECT_EQ(HostThread(), nullptr) << FailureDiagnostic();
    EXPECT_EQ(CompilerThread(), nullptr) << FailureDiagnostic();
}

}  // namespace panda::ecmascript::arksteed
