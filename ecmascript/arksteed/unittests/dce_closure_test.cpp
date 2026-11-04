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

#include "ecmascript/arksteed/unittests/dce_test_fixture.h"

namespace panda::ecmascript::arksteed {


// =============================================================================
// ArkSteedDce::Run - root marking, dependency closure and filtering
//
// Closure scenarios use real Graph/BB/Vertex::New fixtures and the production
// verifier. Most committed graphs also reach the four production pre-regalloc
// consumers, exposing stale references through production code. Consumer runs
// follow repeat-DCE checks because they install register-allocation metadata.
// =============================================================================

/**
 * @tc.name: ArkSteedDceTest.ClosureRemovesDeadChainAndRetainsSharedLiveProducer
 * @tc.desc: `Run` roots the retained controls and `Required` operations, drains
 *           the forward dependency closure and filters the unmarked
 *           `RemovableValue` values and Phis: the dead chain, the dead consumer
 *           and the rootless dead Phi disappear, while the producer shared with
 *           the live Phi and the indexed parameter descriptor keep their
 *           identity, order and structure. A second `Run` on the filtered graph
 *           returns `Unchanged` with an equal structure snapshot, and the result
 *           is consumable by all four production pre-regalloc consumers.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureRemovesDeadChainAndRetainsSharedLiveProducer)
{
    EditorFixture fixture = BuildEditorFixture();
    ASSERT_NE(fixture.graph, nullptr) << "editor fixture construction failed";
    RunVerifier(fixture.graph);

    const std::string before = SnapshotGraphText(fixture.graph);
    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Changed) <<
        "the dead chain, the exposed dead consumer and the dead Phi must be removed";

    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    const std::vector<NonControlVertex *> expectedEntry {fixture.liveProducer, fixture.parameter};
    EXPECT_EQ(entryAfter, expectedEntry) << "survivor identity and relative order must be preserved";
    const std::vector<PhiVertex *> phisAfter(fixture.merge->GetPhis().begin(), fixture.merge->GetPhis().end());
    EXPECT_EQ(phisAfter, (std::vector<PhiVertex *> {fixture.livePhi}));
    EXPECT_EQ(fixture.mergeReturn->GetInput(0), fixture.livePhi) << "a surviving control reference is untouched";
    EXPECT_EQ(fixture.deadSink->GetInput(0), fixture.liveProducer) <<
        "the removed object is not rewritten, it is only detached";
    EXPECT_FALSE(fixture.liveProducer->HasId()) << "no vertex id may be allocated for the transform";
    EXPECT_EQ(fixture.liveProducer->GetRegallocInfo(), nullptr);
    RunVerifier(fixture.graph);

    const std::string committed = SnapshotGraphText(fixture.graph);
    EXPECT_NE(committed, before) << "a successful removal must change the executable structure";
    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Unchanged) <<
        "repeating core DCE on the filtered graph must be a no-op";
    EXPECT_EQ(SnapshotGraphText(fixture.graph), committed) << "idempotency must not change the structure";
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.ClosureRemovesRootlessPhiCycleAndRetainsRootedLoopPhi
 * @tc.desc: A mutual Phi cycle with no root is unmarked and removed as a unit;
 *           the same shape rooted by a control consumer survives. A production-
 *           shaped loop keeps its header Phi and its back-edge producer because
 *           the header branch roots the Phi and the Phi roots its incoming value.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureRemovesRootlessPhiCycleAndRetainsRootedLoopPhi)
{
    ASSERT_NO_FATAL_FAILURE(CheckRootlessPhiCycle());
    ASSERT_NO_FATAL_FAILURE(CheckRootedPhiCycle());
    ASSERT_NO_FATAL_FAILURE(CheckRootedLoopPhi());
}

// A mutual Phi cycle without a control consumer has no live root.
void ArkSteedDceTest::CheckRootlessPhiCycle()
{
    Graph *graph = Graph::New(&chunk_);
    BB *entry = BB::New(&chunk_);
    BB *merge = BB::New(&chunk_);
    BB *exit = BB::New(&chunk_);
    ASSERT_NE(graph, nullptr) << "cycle fixture construction failed";
    ASSERT_NE(entry, nullptr) << "cycle fixture construction failed";
    ASSERT_NE(merge, nullptr) << "cycle fixture construction failed";
    ASSERT_NE(exit, nullptr) << "cycle fixture construction failed";
    graph->Add(entry);
    graph->Add(merge);
    graph->Add(exit);

    ValueVertex *constant = graph->GetInt32Constant(5);
    ASSERT_NE(constant, nullptr) << "cycle fixture construction failed";
    PhiVertex *phiA = NewVertex<PhiVertex>({constant}, VirtualRegister(0));
    PhiVertex *phiB = NewVertex<PhiVertex>({constant}, VirtualRegister(1));
    ASSERT_NE(phiA, nullptr) << "cycle fixture construction failed";
    ASSERT_NE(phiB, nullptr) << "cycle fixture construction failed";
    phiA->SetInput(0, phiB);
    phiB->SetInput(0, phiA);
    phiA->SetOwner(merge);
    phiB->SetOwner(merge);
    merge->AddPhiVertex(phiA);
    merge->AddPhiVertex(phiB);

    JumpVertex *entryJump = NewVertex<JumpVertex>({}, merge);
    JumpVertex *mergeJump = NewVertex<JumpVertex>({}, exit);
    ReturnVertex *exitReturn = NewVertex<ReturnVertex>({constant});
    ASSERT_NE(entryJump, nullptr) << "cycle fixture construction failed";
    ASSERT_NE(mergeJump, nullptr) << "cycle fixture construction failed";
    ASSERT_NE(exitReturn, nullptr) << "cycle fixture construction failed";
    SetControl(entry, entryJump);
    SetControl(merge, mergeJump);
    SetControl(exit, exitReturn);
    entryJump->SetPredecessorId(merge->PredecessorCount());
    merge->AddPredecessor(entry);
    mergeJump->SetPredecessorId(exit->PredecessorCount());
    exit->AddPredecessor(merge);
    RunVerifier(graph);

    EXPECT_EQ(ArkSteedDce::Run(graph), DceResult::Changed) <<
        "a rootless Phi cycle must be removed by the closure";
    EXPECT_TRUE(merge->GetPhis().empty()) << "both cycle Phis must be removed";
    EXPECT_EQ(exitReturn->GetInput(0), constant) << "the surviving control reference is untouched";
    RunVerifier(graph);
    EXPECT_EQ(ArkSteedDce::Run(graph), DceResult::Unchanged) <<
        "repeating core DCE on the filtered cycle graph must be a no-op";
    RunPreRegallocConsumers(graph);
}

// The exit return roots phiA, and phiA's input must reach phiB as well.
// This is the dependency the historical PhiIsMarked shortcut missed.
void ArkSteedDceTest::CheckRootedPhiCycle()
{
    Graph *graph = Graph::New(&chunk_);
    BB *entry = BB::New(&chunk_);
    BB *merge = BB::New(&chunk_);
    BB *exit = BB::New(&chunk_);
    ASSERT_NE(graph, nullptr) << "rooted cycle fixture construction failed";
    ASSERT_NE(entry, nullptr) << "rooted cycle fixture construction failed";
    ASSERT_NE(merge, nullptr) << "rooted cycle fixture construction failed";
    ASSERT_NE(exit, nullptr) << "rooted cycle fixture construction failed";
    graph->Add(entry);
    graph->Add(merge);
    graph->Add(exit);
    ValueVertex *constant = graph->GetInt32Constant(5);
    ASSERT_NE(constant, nullptr) << "rooted cycle fixture construction failed";
    PhiVertex *phiA = NewVertex<PhiVertex>({constant}, VirtualRegister(0));
    PhiVertex *phiB = NewVertex<PhiVertex>({constant}, VirtualRegister(1));
    ASSERT_NE(phiA, nullptr) << "rooted cycle fixture construction failed";
    ASSERT_NE(phiB, nullptr) << "rooted cycle fixture construction failed";
    phiA->SetInput(0, phiB);
    phiB->SetInput(0, phiA);
    phiA->SetOwner(merge);
    phiB->SetOwner(merge);
    merge->AddPhiVertex(phiA);
    merge->AddPhiVertex(phiB);
    JumpVertex *entryJump = NewVertex<JumpVertex>({}, merge);
    JumpVertex *mergeJump = NewVertex<JumpVertex>({}, exit);
    ReturnVertex *exitReturn = NewVertex<ReturnVertex>({phiA});
    ASSERT_NE(entryJump, nullptr) << "rooted cycle fixture construction failed";
    ASSERT_NE(mergeJump, nullptr) << "rooted cycle fixture construction failed";
    ASSERT_NE(exitReturn, nullptr) << "rooted cycle fixture construction failed";
    SetControl(entry, entryJump);
    SetControl(merge, mergeJump);
    SetControl(exit, exitReturn);
    entryJump->SetPredecessorId(merge->PredecessorCount());
    merge->AddPredecessor(entry);
    mergeJump->SetPredecessorId(exit->PredecessorCount());
    exit->AddPredecessor(merge);
    RunVerifier(graph);
    CheckRootedPhiCycleSurvivors(graph, merge, phiA, phiB, exitReturn);
}

void ArkSteedDceTest::CheckRootedPhiCycleSurvivors(Graph *graph, BB *merge, PhiVertex *phiA, PhiVertex *phiB,
                                                   ReturnVertex *exitReturn)
{
    EXPECT_EQ(ArkSteedDce::Run(graph), DceResult::Unchanged) <<
        "a mutual Phi cycle consumed from one live root must survive as a unit";
    const std::vector<PhiVertex *> phisAfter(merge->GetPhis().begin(), merge->GetPhis().end());
    EXPECT_EQ(phisAfter, (std::vector<PhiVertex *> {phiA, phiB})) <<
        "both members of the rooted mutual cycle must survive the closure";
    EXPECT_NE(std::find(merge->GetPhis().begin(), merge->GetPhis().end(), phiB), merge->GetPhis().end()) <<
        "the cycle member reached only through the other member's input must survive";
    EXPECT_EQ(phiA->GetInput(0), static_cast<ValueVertex *>(phiB));
    EXPECT_EQ(phiB->GetInput(0), static_cast<ValueVertex *>(phiA));
    EXPECT_EQ(exitReturn->GetInput(0), static_cast<ValueVertex *>(phiA));
    RunVerifier(graph);
}

// The header branch roots the Phi, which roots the back-edge producer.
void ArkSteedDceTest::CheckRootedLoopPhi()
{
    LoopFixture fixture = BuildLoopFixture();
    ASSERT_NE(fixture.graph, nullptr) << "loop fixture construction failed";
    RunVerifier(fixture.graph);
    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Unchanged) <<
        "a rooted loop Phi and its back-edge producer must survive the closure";
    EXPECT_NE(std::find(fixture.header->GetPhis().begin(), fixture.header->GetPhis().end(), fixture.headerPhi),
              fixture.header->GetPhis().end());
    EXPECT_NE(std::find(fixture.body->GetVertices().begin(), fixture.body->GetVertices().end(),
                        static_cast<NonControlVertex *>(fixture.backedgeValue)),
              fixture.body->GetVertices().end()) <<
        "the back-edge producer is a live Phi input and must survive";
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.ClosureRetainsHiddenOnlyEagerLazyAndCatchProducers
 * @tc.desc: A value referenced only through a hidden channel of a `Required`
 *           consumer is a real dependency of that consumer and must survive the
 *           closure: the eager frame value, the lazy frame value and the catch
 *           Phi incoming value are retained with their consumer, while the
 *           genuinely unreferenced value in the same block is removed. The
 *           hidden references stay readable and exact.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureRetainsHiddenOnlyEagerLazyAndCatchProducers)
{
    HiddenOnlyFixture fixture = BuildHiddenOnlyFixture();
    ASSERT_NE(fixture.graph, nullptr) << "hidden-only fixture construction failed";
    RunVerifier(fixture.graph);

    EXPECT_EQ(static_cast<uint32_t>(DceRemovalPolicy::Classify(fixture.deadPlain)),
              static_cast<uint32_t>(DceRemovalClass::RemovableValue)) <<
        "the removed control value must be a removable value, not a Required operation";

    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Changed) <<
        "the unreferenced value must be removed while the hidden-only producers stay";
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    EXPECT_EQ(std::find(entryAfter.begin(), entryAfter.end(), static_cast<NonControlVertex *>(fixture.deadPlain)),
              entryAfter.end()) <<
        "the unreferenced value must be absent";
    for (NonControlVertex *survivor :
         std::vector<NonControlVertex *> {fixture.eagerProducer, fixture.lazyProducer, fixture.catchProducer}) {
        EXPECT_NE(std::find_if(entryAfter.begin(), entryAfter.end(),
                               [survivor](NonControlVertex *vertex) { return vertex == survivor; }),
                  entryAfter.end()) <<
            "a hidden-only producer must survive the closure: " << OpcodeToString(survivor->GetOpcode());
    }
    EXPECT_EQ(fixture.catchBlock->GetPhis().size(), 1U) << "the live catch Phi must survive the closure";
    EXPECT_EQ(fixture.catchBlock->GetPhis()[0], fixture.catchPhi);

    // The retained hidden references are still exact.
    EXPECT_EQ(fixture.eagerConsumer->GetDeoptFrameValueCount(), 1U);
    EXPECT_EQ(fixture.eagerConsumer->GetDeoptFrameValue(0), fixture.eagerProducer);
    EXPECT_TRUE(fixture.lazyConsumer->HasLazyDeoptFrameState());
    EXPECT_EQ(fixture.lazyConsumer->GetDeoptFrameValue(0), fixture.lazyProducer);
    EXPECT_EQ(fixture.catchPhi->GetInput(0), fixture.catchProducer);
    EXPECT_EQ(fixture.catchThrowable->GetCatchPredecessorIndex(), 0U);
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.ClosureRetainsEveryRequiredOperationClass
 * @tc.desc: The removal policy is authoritative and conservative: a call, an
 *           allocation, a tagged-field write, an observable read, an eager-deopt
 *           guard, a trap-sensitive `I32Div` and the request-controlled
 *           `CallRuntime PrintMethodName` observation have no consumer and are
 *           still `Required` roots, so the closure removes nothing. A policy that
 *           admitted an unaudited opcode would delete one of them and break this
 *           case.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureRetainsEveryRequiredOperationClass)
{
    AllocationFixture fixture = BuildAllocationFixture();
    Graph *graph = fixture.graph;
    BB *merge = fixture.merge;
    ValueVertex *int32Constant = graph->GetInt32Constant(LOOP_ENTRY_VALUE);
    ValueVertex *taggedConstant = fixture.taggedConstant;
    auto *call =
        NewVertex<CallRuntimeVertex>({taggedConstant}, RTSTUB_ID(NewTaggedArray), SideEffectKind::SAFE_CALL);
    auto *marker =
        NewVertex<CallRuntimeVertex>({taggedConstant}, RTSTUB_ID(PrintMethodName), SideEffectKind::SAFE_CALL);
    auto *store = NewVertex<StoreTaggedFieldVertex>({taggedConstant, taggedConstant}, 32);
    auto *read = NewVertex<LoadTaggedFieldVertex>({taggedConstant}, 32);
    auto *divide = NewVertex<I32DivVertex>({int32Constant, int32Constant});
    auto *guard = NewVertex<DeoptIfTaggedConditionVertex>(
        {taggedConstant, taggedConstant}, &chunk_, 0, Condition::EQUAL, kungfu::DeoptType::INSUFFICIENTPROFILE);
    const std::vector<NonControlVertex *> required {call, marker, store, read, divide, guard};
    for (NonControlVertex *vertex : required) {
        ASSERT_NE(vertex, nullptr) << "required-op fixture construction failed";
        EXPECT_EQ(static_cast<uint32_t>(DceRemovalPolicy::Classify(vertex)),
                  static_cast<uint32_t>(DceRemovalClass::Required)) <<
            "the retention control must be a Required opcode: " << OpcodeToString(vertex->GetOpcode());
        vertex->SetOwner(merge);
        merge->AddVertex(vertex);
    }
    RunVerifier(graph);
    const std::string before = SnapshotGraphText(graph);
    EXPECT_EQ(ArkSteedDce::Run(graph), DceResult::Unchanged) <<
        "no Required operation may become a removal candidate";
    EXPECT_EQ(SnapshotGraphText(graph), before) <<
        "unused allocation, initializer and other Required operations must keep their identity and order";
    RunVerifier(graph);
    RunPreRegallocConsumers(graph);
}

/**
 * @tc.name: ArkSteedDceTest.ClosureComposesPolicyDependencyAndEditorSurfaces
 * @tc.desc: Combined check: one real graph drives `DceRemovalPolicy`,
 *           `DceDependencies`, `DceGraphEditor` and `ArkSteedDce::Run` together
 *           and then the production pre-regalloc consumers. The removed value is
 *           removable by the policy, its producer is enumerated by the
 *           dependency surface, the editor performs the filtering, and a
 *           `Required` trap-sensitive value with no consumer is retained. If any
 *           of those surfaces regresses or stops being consulted, this case
 *           fails.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureComposesPolicyDependencyAndEditorSurfaces)
{
    HiddenOnlyFixture fixture = BuildHiddenOnlyFixture();
    ASSERT_NE(fixture.graph, nullptr) << "hidden-only fixture construction failed";
    I32DivVertex *divide =
        NewVertex<I32DivVertex>({fixture.graph->GetInt32Constant(11), fixture.graph->GetInt32Constant(11)});
    ASSERT_NE(divide, nullptr) << "composition fixture construction failed";
    divide->SetOwner(fixture.entry);
    fixture.entry->AddVertex(divide);
    RunVerifier(fixture.graph);

    // Policy surface: the removed value is removable, the trap-sensitive value is not.
    EXPECT_EQ(static_cast<uint32_t>(DceRemovalPolicy::Classify(fixture.deadPlain)),
              static_cast<uint32_t>(DceRemovalClass::RemovableValue));
    EXPECT_EQ(static_cast<uint32_t>(DceRemovalPolicy::Classify(divide)),
              static_cast<uint32_t>(DceRemovalClass::Required));

    // Dependency surface: the hidden consumer enumerates its hidden producer.
    std::vector<ValueVertex *> eagerDependencies;
    DceDependencies::Visit(fixture.eagerConsumer, [&eagerDependencies](ValueVertex *value) {
        eagerDependencies.push_back(value);
    });
    EXPECT_NE(std::find(eagerDependencies.begin(), eagerDependencies.end(),
                        static_cast<ValueVertex *>(fixture.eagerProducer)),
              eagerDependencies.end()) <<
        "the eager frame value must be enumerated as a dependency";

    // Closure surface + editor: the dead value is filtered, the Required trap
    // operation and every hidden-only producer survive.
    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Changed);
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    EXPECT_NE(std::find(entryAfter.begin(), entryAfter.end(), static_cast<NonControlVertex *>(divide)),
              entryAfter.end());
    EXPECT_EQ(std::find(entryAfter.begin(), entryAfter.end(), static_cast<NonControlVertex *>(fixture.deadPlain)),
              entryAfter.end());
    EXPECT_EQ(fixture.eagerConsumer->GetDeoptFrameValue(0), fixture.eagerProducer);
    EXPECT_EQ(fixture.lazyConsumer->GetDeoptFrameValue(0), fixture.lazyProducer);
    EXPECT_EQ(fixture.catchPhi->GetInput(0), fixture.catchProducer);
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.ClosureNoOpFastPathIsStable
 * @tc.desc: `Run` on a graph with no dead removable value must leave the graph
 *           structurally identical, must not leak its transient liveness mark
 *           into a later run, and must be consumable by the production verifier
 *           and the pre-regalloc consumers afterwards.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureNoOpFastPathIsStable)
{
    LoopFixture fixture = BuildLoopFixture();
    ASSERT_NE(fixture.graph, nullptr) << "loop fixture construction failed";
    RunVerifier(fixture.graph);
    const std::string before = SnapshotMembershipText(fixture.graph);

    // Every removable value of this graph is live, so no removal is possible.
    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Unchanged) <<
        "a graph without a dead removable value must not be modified";
    EXPECT_EQ(SnapshotMembershipText(fixture.graph), before) <<
        "the no-op fast path must not change the executable structure";

    // The transient mark must not survive the run: a second run sees the same
    // state and reports the same result.
    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Unchanged) <<
        "the transient liveness mark must not leak into a later run";
    EXPECT_EQ(SnapshotMembershipText(fixture.graph), before);
    EXPECT_FALSE(static_cast<const Vertex *>(fixture.headerPhi)->IsDceMarked()) <<
        "the transient liveness mark must be cleared before Run returns";
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.ClosureRemovesDeadCatchPhiChainAndKeepsLiveCatchPhi
 * @tc.desc: A catch Phi participates in liveness only through a real consumer. The
 *           closure removes the unrooted catch-Phi chain
 *           (`deadHeadProducer -> deadHead -> deadTail`) together with the
 *           producer that only it referenced, even though both throwables naming
 *           the catch block stay live; the catch Phi the handler return consumes
 *           survives with every one of its own inputs and its producer, and the
 *           retained throwables keep their exceptional slots. The committed graph
 *           reaches the production verifier and the pre-regalloc consumers.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureRemovesDeadCatchPhiChainAndKeepsLiveCatchPhi)
{
    CatchClosureFixture fixture = BuildCatchClosureFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch-closure fixture construction failed";
    RunVerifier(fixture.graph);

    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Changed) <<
        "the unrooted catch-Phi chain must be removed while the live catch Phi keeps its producers";

    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    for (NonControlVertex *survivor : std::vector<NonControlVertex *> {fixture.intProducer, fixture.shared,
                                                                      fixture.liveOnly, fixture.firstThrowable,
                                                                      fixture.secondThrowable}) {
        EXPECT_NE(std::find(entryAfter.begin(), entryAfter.end(), survivor), entryAfter.end()) <<
            "a live catch-Phi dependency must survive: " << OpcodeToString(survivor->GetOpcode());
    }
    EXPECT_EQ(std::find(entryAfter.begin(), entryAfter.end(),
                        static_cast<NonControlVertex *>(fixture.deadHeadProducer)),
              entryAfter.end()) <<
        "the exclusive producer of the removed catch-Phi chain must be removed";

    const std::vector<PhiVertex *> phisAfter(fixture.catchBlock->GetPhis().begin(),
                                             fixture.catchBlock->GetPhis().end());
    EXPECT_EQ(phisAfter, (std::vector<PhiVertex *> {fixture.livePhi})) <<
        "only the rooted catch Phi may survive, in survivor order";
    EXPECT_EQ(fixture.livePhi->GetInput(0), static_cast<ValueVertex *>(fixture.liveOnly)) <<
        "every input of the surviving catch Phi must be retained";
    EXPECT_EQ(fixture.livePhi->GetInput(1), static_cast<ValueVertex *>(fixture.shared));
    EXPECT_EQ(fixture.handlerReturn->GetInput(0), static_cast<ValueVertex *>(fixture.livePhi));
    EXPECT_EQ(fixture.firstThrowable->GetCatchPredecessorIndex(), 0U);
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U);
    EXPECT_EQ(fixture.deadHeadProducer->GetInput(0), static_cast<ValueVertex *>(fixture.shared)) <<
        "the removed object is detached, not rewritten";

    // The transient marks never escape the pass.
    for (Vertex *vertex : std::vector<Vertex *> {fixture.intProducer, fixture.shared, fixture.liveOnly,
                                                fixture.livePhi}) {
        EXPECT_FALSE(vertex->IsDceMarked());
        EXPECT_FALSE(vertex->IsDceMember());
        EXPECT_FALSE(vertex->IsDceRegistry());
    }
    RunVerifier(fixture.graph);
    // The idempotency check precedes the pre-regalloc consumers, which install
    // register-allocation metadata and leave the pre-allocation boundary.
    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Unchanged) <<
        "repeating the closure on the committed graph must be a no-op";
    EXPECT_EQ(fixture.catchBlock->GetPhis().size(), 1U);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.ClosureRemovesRootlessCatchPhiCycle
 * @tc.desc: A mutual catch-Phi cycle with no real consumer is unrooted: neither
 *           Phi is a dependency of a live vertex, so the cycle and the producers
 *           that only it referenced are removed as a unit. The same shape rooted
 *           by the handler return keeps both Phis and both producers. Before the
 *           fix the throwables' installed catch-Phi callbacks marked each cycle
 *           participant through the other one, so the unrooted cycle survived.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureRemovesRootlessCatchPhiCycle)
{
    ASSERT_NO_FATAL_FAILURE(CheckRootlessCatchPhiCycle());
    ASSERT_NO_FATAL_FAILURE(CheckRootedCatchPhiCycle());
}

// Without a handler consumer, both catch Phis and their exclusive producers die.
void ArkSteedDceTest::CheckRootlessCatchPhiCycle()
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";
    ValueVertex *producerA = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(11)});
    ValueVertex *producerB = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(13)});
    ASSERT_NE(producerA, nullptr) << "catch-cycle fixture construction failed";
    ASSERT_NE(producerB, nullptr) << "catch-cycle fixture construction failed";
    producerA->SetOwner(fixture.entry);
    producerB->SetOwner(fixture.entry);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), producerB);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), producerA);
    PhiVertex *phiA = AddCatchPhi(fixture, {producerA, fixture.second});
    PhiVertex *phiB = AddCatchPhi(fixture, {producerB, fixture.first});
    ASSERT_NE(phiA, nullptr) << "catch-cycle fixture construction failed";
    ASSERT_NE(phiB, nullptr) << "catch-cycle fixture construction failed";
    phiA->SetInput(1, phiB);
    phiB->SetInput(1, phiA);
    RunVerifier(fixture.graph);

    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Changed) <<
        "an unrooted catch-Phi cycle must be removed as a unit";
    EXPECT_TRUE(fixture.catchBlock->GetPhis().empty()) << "both cycle Phis must be removed";
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    EXPECT_EQ(std::find(entryAfter.begin(), entryAfter.end(), static_cast<NonControlVertex *>(producerA)),
              entryAfter.end()) <<
        "the producer of the unrooted cycle must be removed with it";
    EXPECT_EQ(std::find(entryAfter.begin(), entryAfter.end(), static_cast<NonControlVertex *>(producerB)),
              entryAfter.end());
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

// A handler return roots the same cycle and preserves both producers.
void ArkSteedDceTest::CheckRootedCatchPhiCycle()
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "rooted catch-cycle fixture construction failed";
    ValueVertex *producerA = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(11)});
    ValueVertex *producerB = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(13)});
    ASSERT_NE(producerA, nullptr) << "rooted catch-cycle fixture construction failed";
    ASSERT_NE(producerB, nullptr) << "rooted catch-cycle fixture construction failed";
    producerA->SetOwner(fixture.entry);
    producerB->SetOwner(fixture.entry);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), producerB);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), producerA);
    PhiVertex *phiA = AddCatchPhi(fixture, {producerA, fixture.second});
    PhiVertex *phiB = AddCatchPhi(fixture, {producerB, fixture.first});
    ASSERT_NE(phiA, nullptr) << "rooted catch-cycle fixture construction failed";
    ASSERT_NE(phiB, nullptr) << "rooted catch-cycle fixture construction failed";
    phiA->SetInput(1, phiB);
    phiB->SetInput(1, phiA);
    fixture.handlerReturn->SetInput(0, phiA);
    RunVerifier(fixture.graph);

    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Unchanged) <<
        "a catch-Phi cycle rooted by the handler return must survive";
    const std::vector<PhiVertex *> phisAfter(fixture.catchBlock->GetPhis().begin(),
                                             fixture.catchBlock->GetPhis().end());
    EXPECT_EQ(phisAfter, (std::vector<PhiVertex *> {phiA, phiB}));
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.ClosureCatchPhiOutcomeIsOrderIndependent
 * @tc.desc: The closure result is a set, not a traversal artifact: the same
 *           logical catch graph built with the throwables and the catch-Phi list
 *           in the opposite order commits the same removal selection, expressed
 *           on each build's own vertex identities. Both builds reach the
 *           production verifier and the pre-regalloc consumers.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureCatchPhiOutcomeIsOrderIndependent)
{
    for (bool reverseOrder : {false, true}) {
        SCOPED_TRACE(::testing::Message() << "reverseOrder=" << reverseOrder);
        CatchClosureFixture fixture = BuildCatchClosureFixture(reverseOrder);
        ASSERT_NE(fixture.graph, nullptr) << "catch-closure fixture construction failed";
        RunVerifier(fixture.graph);

        EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Changed);
        const std::vector<PhiVertex *> phisAfter(fixture.catchBlock->GetPhis().begin(),
                                                 fixture.catchBlock->GetPhis().end());
        EXPECT_EQ(phisAfter, (std::vector<PhiVertex *> {fixture.livePhi}));
        const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                         fixture.entry->GetVertices().end());
        EXPECT_EQ(std::find(entryAfter.begin(), entryAfter.end(),
                            static_cast<NonControlVertex *>(fixture.deadHeadProducer)),
                  entryAfter.end());
        for (NonControlVertex *survivor : std::vector<NonControlVertex *> {fixture.intProducer, fixture.shared,
                                                                          fixture.liveOnly,
                                                                          fixture.firstThrowable,
                                                                          fixture.secondThrowable}) {
            EXPECT_NE(std::find(entryAfter.begin(), entryAfter.end(), survivor), entryAfter.end()) <<
                "the ordered build must keep the same survivor: " << OpcodeToString(survivor->GetOpcode());
        }
        EXPECT_EQ(fixture.livePhi->GetInput(0), static_cast<ValueVertex *>(fixture.liveOnly));
        EXPECT_EQ(fixture.livePhi->GetInput(1), static_cast<ValueVertex *>(fixture.shared));
        EXPECT_EQ(fixture.handlerReturn->GetInput(0), static_cast<ValueVertex *>(fixture.livePhi));
        RunVerifier(fixture.graph);
        RunPreRegallocConsumers(fixture.graph);
    }
}

// =============================================================================
// DceLiveSet - null handling, duplicate marks and iteration
// =============================================================================

/**
 * @tc.name: ArkSteedDceTest.LiveSetNullAndDuplicateMarking
 * @tc.desc: The public `DceLiveSet` contract is unchanged by the storage
 *           migration: a null vertex is never a member and reports `false`, a
 *           duplicate mark reports `false` and does not grow the set,
 *           `IsLive`/`MarkedCount` agree with the marks, and iteration reports
 *           each marked pointer exactly once.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, LiveSetNullAndDuplicateMarking)
{
    EditorFixture fixture = BuildEditorFixture();
    ASSERT_NE(fixture.graph, nullptr) << "editor fixture construction failed";
    DceLiveSet live(&chunk_);

    EXPECT_EQ(live.MarkedCount(), 0U);
    EXPECT_FALSE(live.IsLive(nullptr)) << "a null vertex is never a member";
    EXPECT_FALSE(live.MarkLive(nullptr)) << "a null vertex never becomes live";
    EXPECT_EQ(live.MarkedCount(), 0U) << "a null mark must not grow the set";

    EXPECT_FALSE(live.IsLive(fixture.liveProducer));
    EXPECT_TRUE(live.MarkLive(fixture.liveProducer)) << "the first mark reports the transition";
    EXPECT_FALSE(live.MarkLive(fixture.liveProducer)) << "a duplicate mark reports no transition";
    EXPECT_TRUE(live.IsLive(fixture.liveProducer));
    EXPECT_EQ(live.MarkedCount(), 1U) << "a duplicate mark must not grow the set";
    EXPECT_TRUE(live.MarkLive(fixture.deadPhi));
    EXPECT_EQ(live.MarkedCount(), 2U);

    size_t visited = 0;
    for (const Vertex *marked : live) {
        EXPECT_NE(marked, nullptr);
        EXPECT_TRUE(marked == fixture.liveProducer || marked == static_cast<const Vertex *>(fixture.deadPhi));
        ++visited;
    }
    EXPECT_EQ(visited, 2U) << "iteration must report each marked pointer exactly once";
}

// =============================================================================
// ArkSteedDce::Run - catch-Phi removal and activation order
// =============================================================================

/**
 * @tc.name: ArkSteedDceTest.ClosureRemovesDeadCatchPhiWithItsExclusiveProducerAndRepeatsUnchanged
 * @tc.desc: The supported catch shape of `BuildCatchFixture`/
 *           `EditorRemovesDeadCatchPhiWithItsExclusiveProducer` driven through
 *           the ordinary `Run`: a catch Phi no live vertex uses and the removable
 *           producer used only by that Phi's incoming slot are both unmarked and
 *           removed together in one Run, while the `Required` throwables, their
 *           exact exceptional slots, the CFG and the registries survive. The
 *           immediate repeat `Run` on the filtered pre-allocation graph (no
 *           allocation metadata exists yet) is `Unchanged` with equal retained
 *           structure, and the filtered graph is accepted by the production
 *           verifier and the real pre-regalloc consumers. Expected membership is
 *           stated independently of the production live set.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureRemovesDeadCatchPhiWithItsExclusiveProducerAndRepeatsUnchanged)
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";
    ValueVertex *producer = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(11)});
    ASSERT_NE(producer, nullptr) << "catch fixture construction failed";
    producer->SetOwner(fixture.entry);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), producer);
    PhiVertex *deadCatchPhi = AddCatchPhi(fixture, {producer, fixture.second});
    ASSERT_NE(deadCatchPhi, nullptr) << "catch fixture construction failed";
    ASSERT_EQ(deadCatchPhi->GetInputCount(), 2U);
    ASSERT_EQ(fixture.catchBlock->PredecessorCount(), 0U);
    ASSERT_EQ(fixture.entry->GetVertices().size(), 3U);
    RunVerifier(fixture.graph);

    const std::string registriesBefore = SnapshotRegistriesText(fixture.graph);

    // Independent expectation: only the two `Required` throwables of the entry
    // block survive; the exclusive producer and the dead catch Phi do not.
    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Changed) <<
        "a dead catch Phi and its exclusive removable producer must be removed in one Run";
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    const std::vector<NonControlVertex *> expectedEntry {fixture.firstThrowable, fixture.secondThrowable};
    EXPECT_EQ(entryAfter, expectedEntry) <<
        "the exclusive producer must be absent and the Required throwables must keep their order";
    EXPECT_TRUE(fixture.catchBlock->GetPhis().empty()) << "the dead catch Phi must be removed";
    EXPECT_EQ(fixture.firstThrowable->GetCatchPredecessorIndex(), 0U);
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U) <<
        "the exceptional domain must not be renumbered";
    EXPECT_EQ(fixture.entry->GetControlVertex(), static_cast<ControlVertex *>(fixture.entryReturn));
    EXPECT_EQ(fixture.catchBlock->GetControlVertex(), static_cast<ControlVertex *>(fixture.catchJump));
    EXPECT_EQ(fixture.handler->GetControlVertex(), static_cast<ControlVertex *>(fixture.handlerReturn));
    EXPECT_EQ(SnapshotRegistriesText(fixture.graph), registriesBefore) <<
        "the constant maps and parameter bindings must be unchanged";

    // The invocation owner must not leave a transient bit behind, including on the
    // removed objects.
    EXPECT_FALSE(producer->IsDceMarked());
    EXPECT_FALSE(producer->IsDceMember());
    EXPECT_FALSE(producer->IsDceRegistry());
    EXPECT_FALSE(deadCatchPhi->IsDceMarked());
    EXPECT_FALSE(deadCatchPhi->IsDceMember());
    EXPECT_FALSE(fixture.firstThrowable->IsDceMarked());
    EXPECT_FALSE(fixture.firstThrowable->IsDceMember());

    // Immediate repeat Run before any allocation metadata is installed.
    const std::string committed = SnapshotGraphText(fixture.graph);
    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Unchanged) <<
        "the repeat Run on the filtered pre-allocation graph must be a no-op";
    EXPECT_EQ(SnapshotGraphText(fixture.graph), committed) << "the repeat must not change the structure";

    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.ClosureRetainsLiveCatchPhiProducerInBothActivationOrders
 * @tc.desc: The private catch guard must not lose a dependency of a live catch
 *           Phi, whatever the worklist order is. In the first shape the catch Phi
 *           is already live when its throwable is processed, because a `Required`
 *           direct dependency recorded ahead of the throwables in the entry block
 *           uses it; the producer is referenced only by the catch Phi incoming
 *           slot and has no ordinary consumer at all. In the second shape the
 *           same Phi becomes live only after its throwable was processed, because
 *           the handler control reaches it through a `RemovableValue`
 *           intermediate. Both runs are `Unchanged` and keep the hidden-only
 *           producer, the live Phi and the exact exceptional slots. A third shape
 *           shows the dead direction: a dead catch Phi and its exclusive producer
 *           chain are removed together by the same single closure. Expected
 *           membership is stated independently of the production live set.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureRetainsLiveCatchPhiProducerInBothActivationOrders)
{
    ASSERT_NO_FATAL_FAILURE(CheckEarlyCatchPhiActivation());
    ASSERT_NO_FATAL_FAILURE(CheckLateCatchPhiActivation());
    ASSERT_NO_FATAL_FAILURE(CheckDeadCatchPhiProducerChain());
}

// A Required use is seeded before the throwables, so the Phi is already live
// when firstThrowable is processed and the guarded catch emission fires.
void ArkSteedDceTest::CheckEarlyCatchPhiActivation()
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";
    ValueVertex *producer = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(11)});
    ASSERT_NE(producer, nullptr) << "catch fixture construction failed";
    producer->SetOwner(fixture.entry);
    PhiVertex *catchPhi = AddCatchPhi(fixture, {producer, fixture.second});
    ASSERT_NE(catchPhi, nullptr) << "catch fixture construction failed";
    CallRuntimeVertex *directUse =
        NewVertex<CallRuntimeVertex>({catchPhi}, RTSTUB_ID(NewTaggedArray), SideEffectKind::SAFE_CALL);
    ASSERT_NE(directUse, nullptr) << "catch fixture construction failed";
    directUse->SetOwner(fixture.entry);
    // Production order: the direct use is recorded before the throwables, so
    // the closure processes it first and the Phi is live at the throwable.
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), producer);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), directUse);
    RunVerifier(fixture.graph);

    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Unchanged) <<
        "a live catch Phi keeps the hidden-only producer of its incoming slot";
    EXPECT_NE(std::find(fixture.entry->GetVertices().begin(), fixture.entry->GetVertices().end(),
                        static_cast<NonControlVertex *>(producer)),
              fixture.entry->GetVertices().end());
    ASSERT_EQ(fixture.catchBlock->GetPhis().size(), 1U);
    EXPECT_EQ(fixture.catchBlock->GetPhis()[0], catchPhi);
    EXPECT_EQ(catchPhi->GetInput(0), producer);
    EXPECT_EQ(fixture.firstThrowable->GetCatchPredecessorIndex(), 0U);
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U);
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

// The handler control reaches the Phi through a removable intermediate only
// after its throwable has been processed.
void ArkSteedDceTest::CheckLateCatchPhiActivation()
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";
    ValueVertex *producer = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(11)});
    ASSERT_NE(producer, nullptr) << "catch fixture construction failed";
    producer->SetOwner(fixture.entry);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), producer);
    PhiVertex *catchPhi = AddCatchPhi(fixture, {producer, fixture.second});
    ASSERT_NE(catchPhi, nullptr) << "catch fixture construction failed";
    ConvertHoleToUndefinedVertex *intermediate = NewVertex<ConvertHoleToUndefinedVertex>({catchPhi});
    ASSERT_NE(intermediate, nullptr) << "catch fixture construction failed";
    intermediate->SetOwner(fixture.entry);
    fixture.entry->AddVertex(intermediate);
    ReturnVertex *handlerUse = NewVertex<ReturnVertex>({intermediate});
    ASSERT_NE(handlerUse, nullptr) << "catch fixture construction failed";
    handlerUse->SetOwner(fixture.handler);
    SetControl(fixture.handler, handlerUse);
    RunVerifier(fixture.graph);

    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Unchanged) <<
        "a catch Phi that becomes live after its throwable keeps every dependency";
    EXPECT_NE(std::find(fixture.entry->GetVertices().begin(), fixture.entry->GetVertices().end(),
                        static_cast<NonControlVertex *>(producer)),
              fixture.entry->GetVertices().end());
    EXPECT_NE(std::find(fixture.entry->GetVertices().begin(), fixture.entry->GetVertices().end(),
                        static_cast<NonControlVertex *>(intermediate)),
              fixture.entry->GetVertices().end());
    ASSERT_EQ(fixture.catchBlock->GetPhis().size(), 1U);
    EXPECT_EQ(fixture.catchBlock->GetPhis()[0], catchPhi);
    EXPECT_EQ(catchPhi->GetInput(0), producer);
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

// A dead catch Phi and its exclusive chain disappear in the same closure.
void ArkSteedDceTest::CheckDeadCatchPhiProducerChain()
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";
    ValueVertex *chainHead = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(11)});
    ValueVertex *chainTail = NewVertex<ConvertHoleToUndefinedVertex>({chainHead});
    ASSERT_NE(chainHead, nullptr) << "catch fixture construction failed";
    ASSERT_NE(chainTail, nullptr) << "catch fixture construction failed";
    chainHead->SetOwner(fixture.entry);
    chainTail->SetOwner(fixture.entry);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), chainTail);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), chainHead);
    PhiVertex *deadCatchPhi = AddCatchPhi(fixture, {chainTail, fixture.second});
    ASSERT_NE(deadCatchPhi, nullptr) << "catch fixture construction failed";
    RunVerifier(fixture.graph);

    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Changed) <<
        "a dead catch Phi removes its exclusive producer chain in one Run";
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    const std::vector<NonControlVertex *> expectedEntry {fixture.firstThrowable, fixture.secondThrowable};
    EXPECT_EQ(entryAfter, expectedEntry) << "the dead chain must be absent";
    EXPECT_TRUE(fixture.catchBlock->GetPhis().empty());
    EXPECT_EQ(fixture.firstThrowable->GetCatchPredecessorIndex(), 0U);
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U);
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.ClosureRetainsSharedProducerOfLiveCatchPhiAndDirectUse
 * @tc.desc: The guarded catch rule must not over-prune: a producer consumed both
 *           by a live catch Phi's incoming slot and by a surviving direct
 *           dependency stays, while the dead catch Phi and the producer used only
 *           by that dead Phi are removed in the same Run. The retained catch Phi,
 *           the throwables, their exact exceptional slots and the remaining
 *           executable order are checked independently of the production live
 *           set, and the filtered graph is consumable by the production verifier
 *           and the pre-regalloc consumers.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClosureRetainsSharedProducerOfLiveCatchPhiAndDirectUse)
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";
    ValueVertex *sharedProducer = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(11)});
    ValueVertex *deadProducer = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(13)});
    ASSERT_NE(sharedProducer, nullptr) << "catch fixture construction failed";
    ASSERT_NE(deadProducer, nullptr) << "catch fixture construction failed";
    sharedProducer->SetOwner(fixture.entry);
    deadProducer->SetOwner(fixture.entry);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), deadProducer);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), sharedProducer);
    PhiVertex *liveCatchPhi = AddCatchPhi(fixture, {sharedProducer, fixture.second});
    PhiVertex *deadCatchPhi = AddCatchPhi(fixture, {deadProducer, fixture.second});
    ASSERT_NE(liveCatchPhi, nullptr) << "catch fixture construction failed";
    ASSERT_NE(deadCatchPhi, nullptr) << "catch fixture construction failed";
    // The live catch Phi is used by the handler body ...
    ReturnVertex *handlerUse = NewVertex<ReturnVertex>({liveCatchPhi});
    ASSERT_NE(handlerUse, nullptr) << "catch fixture construction failed";
    handlerUse->SetOwner(fixture.handler);
    SetControl(fixture.handler, handlerUse);
    // ... and the shared producer also has a surviving direct dependency.
    CallRuntimeVertex *directUse =
        NewVertex<CallRuntimeVertex>({sharedProducer}, RTSTUB_ID(NewTaggedArray), SideEffectKind::SAFE_CALL);
    ASSERT_NE(directUse, nullptr) << "catch fixture construction failed";
    directUse->SetOwner(fixture.entry);
    fixture.entry->AddVertex(directUse);
    RunVerifier(fixture.graph);

    const std::string registriesBefore = SnapshotRegistriesText(fixture.graph);
    EXPECT_EQ(ArkSteedDce::Run(fixture.graph), DceResult::Changed) <<
        "only the dead catch Phi and its exclusive producer are removable";
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    const std::vector<NonControlVertex *> expectedEntry {sharedProducer, fixture.firstThrowable,
                                                         fixture.secondThrowable, directUse};
    EXPECT_EQ(entryAfter, expectedEntry) <<
        "the shared producer and the surviving direct dependency must stay in order";
    ASSERT_EQ(fixture.catchBlock->GetPhis().size(), 1U);
    EXPECT_EQ(fixture.catchBlock->GetPhis()[0], liveCatchPhi) <<
        "only the live catch Phi may survive";
    EXPECT_EQ(liveCatchPhi->GetInput(0), sharedProducer);
    EXPECT_EQ(liveCatchPhi->GetInput(1), fixture.second);
    EXPECT_EQ(fixture.firstThrowable->GetCatchPredecessorIndex(), 0U);
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U);
    EXPECT_EQ(SnapshotRegistriesText(fixture.graph), registriesBefore);
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}
}  // namespace panda::ecmascript::arksteed
