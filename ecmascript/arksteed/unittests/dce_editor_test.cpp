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


/**
 * @tc.name: ArkSteedDceTest.EditorRemovesDeadValuesAndPhisAndPreservesOrder
 * @tc.desc: On a graph the production verifier accepts before and after, the
 *           editor removes the marked values and the unmarked Phi from the
 *           executable lists, keeps the surviving order, keeps every reference
 *           valid and reports `Changed`. A repeated request reports `Unchanged`.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorRemovesDeadValuesAndPhisAndPreservesOrder)
{
    EditorFixture fixture = BuildEditorFixture();
    ASSERT_NE(fixture.graph, nullptr) << "editor fixture construction failed";
    RunVerifier(fixture.graph);

    const std::vector<BB *> predecessorsBefore(fixture.merge->GetPredecessors().begin(),
                                               fixture.merge->GetPredecessors().end());

    RemovalRequest request;
    MarkDeadEditorSelection(fixture, request);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, request), DceResult::Changed);

    CheckEditorOrder(fixture);
    ASSERT_FALSE(HasFatalFailure());
    // Retained predecessor order and Phi input order are untouched.
    const std::vector<BB *> predecessorsAfter(fixture.merge->GetPredecessors().begin(),
                                              fixture.merge->GetPredecessors().end());
    EXPECT_EQ(predecessorsAfter, predecessorsBefore);

    // The committed graph is still accepted by the production verifier, and the
    // shared validation reports no integrity failure.
    RunVerifier(fixture.graph);

    CheckRemovedStorage(fixture);

    // Idempotency: the same live set on the committed graph changes nothing.
    RemovalRequest repeated;
    MarkDeadEditorSelection(fixture, repeated);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, repeated), DceResult::Unchanged);
    const std::vector<NonControlVertex *> expectedEntry {fixture.liveProducer, fixture.parameter};
    const std::vector<NonControlVertex *> entryAfterRepeat(fixture.entry->GetVertices().begin(),
                                                           fixture.entry->GetVertices().end());
    EXPECT_EQ(entryAfterRepeat, expectedEntry);

    // The committed result must be consumable by the production pre-regalloc
    // consumers, not merely accepted by this domain's validator. Liveness indexes
    // the merge Phi inputs with `trueJump`/`falseJump`'s predecessor ids, so an
    // invalid edge-split shape or predecessor id fails here. It runs last because
    // it installs register-allocation metadata, which the pre-allocation boundary
    // of the filtering API rejects.
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorKeepsParameterDescriptorAndItsIndexedSlot
 * @tc.desc: A request that selects an indexed parameter descriptor cannot dangle the
 *           slot or renumber it: the descriptor vertex stays in its block and
 *           `Graph::GetParameter` answers the same vertex at the same index.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorKeepsParameterDescriptorAndItsIndexedSlot)
{
    EditorFixture fixture = BuildEditorFixture();
    ASSERT_NE(fixture.graph, nullptr) << "editor fixture construction failed";
    ASSERT_EQ(fixture.graph->GetParameterCount(), 1U);
    ASSERT_EQ(fixture.graph->GetParameter(0), fixture.parameter);

    RemovalRequest request;
    request.MarkRemoved(fixture.parameter);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, request), DceResult::Unchanged);
    EXPECT_EQ(fixture.graph->GetParameterCount(), 1U);
    EXPECT_EQ(fixture.graph->GetParameter(0), fixture.parameter);
    const std::vector<NonControlVertex *> entry(fixture.entry->GetVertices().begin(),
                                                fixture.entry->GetVertices().end());
    EXPECT_NE(std::find(entry.begin(), entry.end(), fixture.parameter), entry.end()) <<
        "the parameter descriptor must keep its vertex";
}

/**
 * @tc.name: ArkSteedDceTest.EditorRetainsUnmarkedControlVertex
 * @tc.desc: Core does not change the CFG and a control vertex is never a
 *           candidate removal: a live set that leaves the branch unmarked keeps
 *           the control in place and changes nothing.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorRetainsUnmarkedControlVertex)
{
    EditorFixture fixture = BuildEditorFixture();
    ASSERT_NE(fixture.graph, nullptr) << "editor fixture construction failed";
    RunVerifier(fixture.graph);
    const std::vector<NonControlVertex *> entryBefore(fixture.entry->GetVertices().begin(),
                                                      fixture.entry->GetVertices().end());
    RemovalRequest request;
    request.MarkRemoved(fixture.branch);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, request), DceResult::Unchanged) <<
        "an unmarked control vertex is retained, not refused or removed";
    EXPECT_EQ(fixture.entry->GetControlVertex(), static_cast<ControlVertex *>(fixture.branch));
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    EXPECT_EQ(entryAfter, entryBefore);
    RunVerifier(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorRetainsUnmarkedRequiredOperations
 * @tc.desc: The live set is not a removal proof. A structurally valid graph may
 *           contain unused operations the removal policy keeps `Required`: the
 *           trap-sensitive `I32DivVertex`, a runtime call and a tagged-field
 *           store stay exactly where they are, the result is `Unchanged`, and
 *           the graph is left byte-identical. A policy that retains everything
 *           does not pass this domain because the positive cases above prove
 *           real removals.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorRetainsUnmarkedRequiredOperations)
{
    AllocationFixture fixture = BuildAllocationFixture();
    auto *int32Constant = fixture.graph->GetInt32Constant(FIXTURE_INT32_VALUE);
    auto *divide = NewVertex<I32DivVertex>({int32Constant, int32Constant});
    auto *call =
        NewVertex<CallRuntimeVertex>({fixture.taggedConstant}, RTSTUB_ID(NewTaggedArray), SideEffectKind::SAFE_CALL);
    auto *store = NewVertex<StoreTaggedFieldVertex>({fixture.taggedConstant, fixture.taggedConstant}, 32);
    for (NonControlVertex *vertex : std::vector<NonControlVertex *> {divide, call, store}) {
        ASSERT_NE(vertex, nullptr) << "editor fixture construction failed";
        vertex->SetOwner(fixture.merge);
        fixture.merge->AddVertex(vertex);
    }
    RunVerifier(fixture.graph);
    const std::string before = SnapshotGraphText(fixture.graph);
    const std::vector<Vertex *> requiredTargets {
        divide, call, store, fixture.check, fixture.allocation, fixture.slowAllocation, fixture.initializer};
    for (Vertex *target : requiredTargets) {
        ASSERT_EQ(static_cast<uint32_t>(DceRemovalPolicy::Classify(target)),
                  static_cast<uint32_t>(DceRemovalClass::Required)) <<
            "the retention control must be a Required opcode: " << OpcodeToString(target->GetOpcode());
        RemovalRequest request;
        request.MarkRemoved(target);
        EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, request), DceResult::Unchanged) <<
            "an unmarked Required opcode must be retained: " << OpcodeToString(target->GetOpcode());
        EXPECT_EQ(SnapshotGraphText(fixture.graph), before) <<
            "a retained Required operation must keep every block and reference unchanged";
    }
    RunVerifier(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorValidatesBalancedLoopForm
 * @tc.desc: A production-shaped loop is accepted, is consumable by the
 *           pre-regalloc consumers and is left byte-identical by a no-op
 *           selection: the header/JumpLoop block-order relation, the two
 *           ordinary predecessors and the two-input header Phi are the
 *           structural facts the production loop convention relies on.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorValidatesBalancedLoopForm)
{
    LoopFixture fixture = BuildLoopFixture();
    ASSERT_NE(fixture.graph, nullptr) << "loop fixture construction failed";
    ASSERT_EQ(fixture.header->PredecessorCount(), 2U);
    ASSERT_EQ(fixture.headerPhi->GetInputCount(), 2U);
    RunVerifier(fixture.graph);

    RemovalRequest request;
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, request), DceResult::Unchanged);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorValidatesExceptionIndexAgainstExceptionalDomain
 * @tc.desc: The exception predecessor index of a throwable vertex is an index
 *           into the catch block's exceptional-input domain (the sequence of
 *           throwable vertices the builder registered for it), never into
 *           `BB::GetPredecessors()`. The production shape has a catch block with
 *           zero ordinary predecessors and two exceptional inputs; both indexes
 *           are accepted. The exceptional domain is a graph property: a request
 *           that would narrow it retains the registered throwables instead of
 *           renumbering the survivors, so the structure is preserved exactly and
 *           a fresh request still validates.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorValidatesExceptionIndexAgainstExceptionalDomain)
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";
    PhiVertex *catchPhi = AddCatchPhi(fixture, {fixture.first, fixture.second});
    ASSERT_NE(catchPhi, nullptr);

    // Production shape: `ActivateCatchBlock`/`InitFrameStateForCatchBlockHeader`
    // create the catch block and the catch Phi inputs without adding an ordinary
    // CFG predecessor, so the ordinary predecessor count is zero while the
    // exceptional-input domain has two entries. `ProcessCatchBlockHead` marks the
    // handler, and the handler ends in a jump to a returning body.
    ASSERT_TRUE(fixture.catchBlock->IsExceptionHandler());
    ASSERT_EQ(fixture.catchBlock->PredecessorCount(), 0U);
    ASSERT_EQ(catchPhi->GetInputCount(), 2U);
    RunVerifier(fixture.graph);

    RemovalRequest request;
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, request), DceResult::Unchanged);

    // The editor filters executable lists and never renumbers a surviving
    // exception index or resizes a live catch Phi. A live set that fails to mark a
    // registered throwable cannot narrow the domain: the throwable is a call the
    // removal policy keeps `Required`, so it is retained and the result is
    // `Unchanged`, not a renumbered or resized structure.
    const std::vector<NonControlVertex *> entryBefore(fixture.entry->GetVertices().begin(),
                                                      fixture.entry->GetVertices().end());
    const std::vector<ValueVertex *> phiInputsBefore {catchPhi->GetInput(0), catchPhi->GetInput(1)};
    RemovalRequest narrow;
    narrow.MarkRemoved(fixture.firstThrowable);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, narrow), DceResult::Unchanged) <<
        "a registered catch predecessor is Required and retained, never renumbered";

    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    EXPECT_EQ(entryAfter, entryBefore) << "a retained throwable must not be removed";
    ASSERT_EQ(catchPhi->GetInputCount(), 2U) << "the catch Phi must not be resized";
    const std::vector<ValueVertex *> phiInputsAfter {catchPhi->GetInput(0), catchPhi->GetInput(1)};
    EXPECT_EQ(phiInputsAfter, phiInputsBefore) << "live Phi inputs must not be mutated";
    EXPECT_EQ(fixture.firstThrowable->GetCatchPredecessorIndex(), 0U);
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U) <<
        "the exceptional domain must not be narrowed";

    // Idempotency: repeating the retained live set is still `Unchanged`, and a
    // fully live set validates the untouched graph.
    RemovalRequest repeated;
    repeated.MarkRemoved(fixture.firstThrowable);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, repeated), DceResult::Unchanged);
    RemovalRequest fresh;
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, fresh), DceResult::Unchanged) <<
        "a fully live set on the untouched graph must be a no-op";

    // The retained handler graph must be consumable by the production verifier and
    // the pre-regalloc consumers, not merely accepted by this domain's validator.
    // Liveness reads the catch Phi input at the throwable's exceptional index, so
    // a wrong handler shape fails here.
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorPreservesExceptionalSlotsOnOrdinaryRemoval
 * @tc.desc: A commit that removes ordinary dead values leaves the exceptional
 *           structure byte-identical: both throwables keep their slots and the
 *           catch Phi keeps both inputs in order. A request that instead targets the
 *           index-zero throwable retains it and keeps the same before/after snapshot,
 *           so a dropped or renumbered slot cannot pass because the result enum
 *           is successful.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorPreservesExceptionalSlotsOnOrdinaryRemoval)
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";
    // An ordinary dead value that no surviving reference uses.
    ValueVertex *deadProducer = NewVertex<I32ToTaggedIntVertex>({fixture.graph->GetInt32Constant(11)});
    ASSERT_NE(deadProducer, nullptr) << "catch fixture construction failed";
    deadProducer->SetOwner(fixture.entry);
    fixture.entry->AddVertex(deadProducer);
    PhiVertex *catchPhi = AddCatchPhi(fixture, {fixture.first, fixture.second});
    ASSERT_NE(catchPhi, nullptr) << "catch fixture construction failed";
    ASSERT_TRUE(fixture.catchBlock->IsExceptionHandler());
    RunVerifier(fixture.graph);

    const std::vector<ValueVertex *> inputsBefore {fixture.first, fixture.second};
    std::vector<ValueVertex *> actualBefore;
    for (uint32_t index = 0; index < catchPhi->GetInputCount(); ++index) {
        actualBefore.push_back(catchPhi->GetInput(index));
    }
    ASSERT_EQ(actualBefore, inputsBefore);

    // Required throwables retain their exceptional slots before an ordinary removal.
    CheckRetainedExceptionalSite(fixture, catchPhi);
    ASSERT_FALSE(HasFatalFailure());

    // Only the ordinary dead value is removed; the exceptional structure is
    // untouched.
    RemovalRequest removal;
    removal.MarkRemoved(deadProducer);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, removal), DceResult::Changed);
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    EXPECT_EQ(std::find(entryAfter.begin(), entryAfter.end(), deadProducer), entryAfter.end()) <<
        "the ordinary dead value must be removed";
    EXPECT_NE(std::find(entryAfter.begin(), entryAfter.end(), fixture.firstThrowable), entryAfter.end()) <<
        "an ordinary removal must not remove a throwable";
    EXPECT_EQ(fixture.firstThrowable->GetCatchPredecessorIndex(), 0U) <<
        "an ordinary removal must not renumber a throwable";
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U);
    ASSERT_EQ(catchPhi->GetInputCount(), 2U) << "an ordinary removal must not resize the catch Phi";
    const std::vector<ValueVertex *> inputsAfter {catchPhi->GetInput(0), catchPhi->GetInput(1)};
    EXPECT_EQ(inputsAfter, inputsBefore) << "catch Phi slot identity/order must be preserved";
    // Idempotency: the committed request removes nothing further.
    RemovalRequest repeated;
    repeated.MarkRemoved(deadProducer);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, repeated), DceResult::Unchanged);
    // The committed result is a valid production graph: the verifier and the
    // pre-regalloc consumers accept the retained handler with its live catch Phi.
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorRetainsUnmarkedControlThrowable
 * @tc.desc: A `ThrowVertex` is a control vertex and a throwable at the same time
 *           (`GraphBuilder::FinishBlockWith<ThrowVertex>` registers it through the
 *           same catch predecessor domain). It is a `Required` control, so an
 *           unmarked one is retained: both its slot and the catch Phi stay
 *           exactly as constructed and the result is `Unchanged`.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorRetainsUnmarkedControlThrowable)
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";
    ValueVertex *third = fixture.graph->GetTaggedConstant(JSTaggedValue::True().GetRawData());
    ASSERT_NE(third, nullptr);
    PhiVertex *catchPhi = AddCatchPhi(fixture, {fixture.first, fixture.second, third});
    ASSERT_NE(catchPhi, nullptr);
    ASSERT_EQ(catchPhi->GetInputCount(), 3U);

    // The control throwable takes the highest slot (index 2) and becomes the
    // entry block control; the non-control calls keep slots 0 and 1.
    ThrowVertex *throwVertex = NewVertex<ThrowVertex>(std::vector<ValueVertex *> {}, RTSTUB_ID(Throw));
    ASSERT_NE(throwVertex, nullptr) << "throw fixture construction failed";
    ASSERT_TRUE(throwVertex->CanThrow());
    throwVertex->LoadCatchBlock(fixture.catchBlock, 2);
    SetControl(fixture.entry, throwVertex);

    const std::vector<NonControlVertex *> entryBefore(fixture.entry->GetVertices().begin(),
                                                      fixture.entry->GetVertices().end());
    const std::vector<ValueVertex *> inputsBefore {fixture.first, fixture.second, third};

    RemovalRequest removal;
    removal.MarkRemoved(throwVertex);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, removal), DceResult::Unchanged) <<
        "a control throwable is Required and cannot be a candidate removal";

    EXPECT_EQ(fixture.entry->GetControlVertex(), throwVertex) << "the CFG must be unchanged";
    EXPECT_EQ(throwVertex->GetCatchPredecessorIndex(), 2U) << "the exceptional slot must be unchanged";
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U);
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    EXPECT_EQ(entryAfter, entryBefore) << "the retained throwable must not be removed";
    ASSERT_EQ(catchPhi->GetInputCount(), 3U);
    const std::vector<ValueVertex *> inputsAfter {catchPhi->GetInput(0), catchPhi->GetInput(1),
                                                  catchPhi->GetInput(2)};
    EXPECT_EQ(inputsAfter, inputsBefore) << "catch Phi slot identity/order must be preserved";
    RemovalRequest empty;
    RunVerifier(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorValidatesNoPhiExceptionHandlerDomain
 * @tc.desc: An exception handler without any catch Phi still has an
 *           exceptional-input domain: the index is bounded by the number of
 *           throwable vertices registered for the handler, not by the ordinary
 *           predecessor count. Both valid indexes are accepted, and removing a
 *           registered catch predecessor retains it for the no-Phi shape too,
 *           because the domain is a graph property and filtering never renumbers
 *           the survivors.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorValidatesNoPhiExceptionHandlerDomain)
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";
    ASSERT_FALSE(fixture.catchBlock->HasPhi());
    ASSERT_TRUE(fixture.catchBlock->IsExceptionHandler());
    ASSERT_EQ(fixture.catchBlock->PredecessorCount(), 0U);
    RunVerifier(fixture.graph);

    RemovalRequest request;
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, request), DceResult::Unchanged);

    const std::vector<NonControlVertex *> entryBefore(fixture.entry->GetVertices().begin(),
                                                      fixture.entry->GetVertices().end());
    RemovalRequest removal;
    removal.MarkRemoved(fixture.firstThrowable);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, removal), DceResult::Unchanged) <<
        "a registered catch predecessor is Required and retained, with or without catch Phis";
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    EXPECT_EQ(entryAfter, entryBefore) << "a retained throwable must not be removed";
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U) <<
        "the exceptional domain must not be narrowed";
    RemovalRequest fresh;
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, fresh), DceResult::Unchanged);
    // The no-Phi handler is a valid production graph: the verifier and the
    // pre-regalloc consumers accept the retained handler.
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorRemovesOnlyDependenciesOfRemovedConsumers
 * @tc.desc: The editor selects a removal closure, not a single vertex: a marked
 *           chain whose tail is a dead consumer and whose head is a dead
 *           producer is removed together, while the shared producer consumed by
 *           a retained Phi survives.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorRemovesOnlyDependenciesOfRemovedConsumers)
{
    EditorFixture fixture = BuildEditorFixture();
    ASSERT_NE(fixture.graph, nullptr) << "editor fixture construction failed";
    RemovalRequest request;
    request.MarkRemoved(fixture.deadChainTail);
    MarkDeadEditorSelection(fixture, request);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, request), DceResult::Changed);
    const std::vector<NonControlVertex *> entry(fixture.entry->GetVertices().begin(),
                                                fixture.entry->GetVertices().end());
    EXPECT_NE(std::find(entry.begin(), entry.end(), fixture.liveProducer), entry.end()) <<
        "the shared live producer must survive";
    EXPECT_EQ(std::find(entry.begin(), entry.end(), fixture.deadChainHead), entry.end());
    EXPECT_EQ(std::find(entry.begin(), entry.end(), fixture.deadChainTail), entry.end());
    RunVerifier(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorRemovesDeadCatchPhiWithItsExclusiveProducer
 * @tc.desc: The prospective-surviving-Phi rule: a dead catch Phi and the producer
 *           used only by that Phi are removed together. The producer is not
 *           reported as a dangling use from the old Phi list, because the removed
 *           Phi contributes no surviving reference. The retained throwables keep
 *           their slots and the graph stays consumable.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorRemovesDeadCatchPhiWithItsExclusiveProducer)
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
    ASSERT_TRUE(fixture.catchBlock->IsExceptionHandler());
    RunVerifier(fixture.graph);

    const std::vector<NonControlVertex *> entryBefore(fixture.entry->GetVertices().begin(),
                                                      fixture.entry->GetVertices().end());
    RemovalRequest removal;
    removal.MarkRemoved(producer);
    removal.MarkRemoved(deadCatchPhi);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, removal), DceResult::Changed) <<
        "the dead catch Phi and the producer it alone references must be removable together";
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                     fixture.entry->GetVertices().end());
    EXPECT_EQ(std::find(entryAfter.begin(), entryAfter.end(), producer), entryAfter.end()) <<
        "the exclusive producer of the removed catch Phi must be removed";
    EXPECT_NE(std::find(entryAfter.begin(), entryAfter.end(), fixture.firstThrowable), entryAfter.end()) <<
        "a retained throwable must not be removed";
    EXPECT_TRUE(fixture.catchBlock->GetPhis().empty()) <<
        "the dead catch Phi must be removed from the handler Phi list";
    EXPECT_EQ(fixture.firstThrowable->GetCatchPredecessorIndex(), 0U);
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U);
    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
    (void)entryBefore;
}

/**
 * @tc.name: ArkSteedDceTest.EditorAcceptsBuilderDummyLoopLatch
 * @tc.desc: `GraphBuilder::FinishDeadLoopBackEdge` retains a structural `JumpLoop`
 *           latch for an optimistic loop whose real back edge was removed, and
 *           that latch block has no ordinary incoming edge. A valid
 *           builder-supported dummy latch is preserved, not rejected, while the
 *           loop Phi keeps its two slots and the graph stays consumable.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorAcceptsBuilderDummyLoopLatch)
{
    Graph *graph = nullptr;
    BB *header = nullptr;
    BB *latch = nullptr;
    ValueVertex *entryValue = nullptr;
    I32AddVertex *backedgeValue = nullptr;
    PhiVertex *headerPhi = nullptr;
    ReturnVertex *headerReturn = nullptr;
    JumpLoopVertex *latchBackedge = nullptr;
    BuildDummyLoop(graph, header, latch, entryValue, backedgeValue, headerPhi, headerReturn, latchBackedge);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(header->PredecessorCount(), 2U);
    ASSERT_EQ(headerPhi->GetInputCount(), 2U);
    ASSERT_EQ(latch->PredecessorCount(), 0U) << "the dummy latch has no ordinary incoming edge";
    RunVerifier(graph);

    RemovalRequest live;
    DceLiveSet liveSet(&chunk_);
    MarkAllLiveExcept(graph, liveSet, live);
    EXPECT_EQ(DceGraphEditor::Filter(graph, liveSet), DceResult::Unchanged) <<
        "a builder-supported dummy loop latch is preserved, not rejected";
    EXPECT_EQ(header->GetControlVertex(), static_cast<ControlVertex *>(headerReturn));
    EXPECT_EQ(headerPhi->GetInputCount(), 2U);
    EXPECT_EQ(headerPhi->GetInput(0), entryValue);
    EXPECT_EQ(headerPhi->GetInput(1), backedgeValue);
    ASSERT_EQ(latch->GetControlVertex(), static_cast<ControlVertex *>(latchBackedge));
    RunPreRegallocConsumers(graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorRemovesRootlessNormalPhiCycle
 * @tc.desc: A normal Phi cycle that no control, control-flow-relevant value or
 *           snapshot roots is a valid builder-supported shape. Both Phis are
 *           candidates together, the commit removes the cycle, survivors keep
 *           their structure and the retained graph is consumable by the
 *           pre-regalloc consumers.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorRemovesRootlessNormalPhiCycle)
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

    // Two Phis in one block whose only inputs are each other. Both are created
    // with a temporary input and then rewired into the cycle, because the input
    // storage precedes the vertex.
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

    ReturnVertex *exitReturn = nullptr;
    ConnectPhiCycle(graph, entry, merge, exit, constant, exitReturn);
    ASSERT_FALSE(HasFatalFailure());

    RemovalRequest request;
    request.MarkRemoved(phiA);
    request.MarkRemoved(phiB);
    const std::string registriesBefore = SnapshotRegistriesText(graph);
    const std::string controlBefore = SnapshotControlText(graph);
    EXPECT_EQ(FilterRemovals(graph, &chunk_, request), DceResult::Changed) <<
        "a rootless Phi cycle must be removable as a unit";
    EXPECT_TRUE(merge->GetPhis().empty()) << "both cycle Phis must be removed";
    EXPECT_EQ(SnapshotRegistriesText(graph), registriesBefore);
    EXPECT_EQ(SnapshotControlText(graph), controlBefore);
    EXPECT_EQ(exitReturn->GetInput(0), constant) << "the surviving control reference is untouched";
    RunVerifier(graph);
    RunPreRegallocConsumers(graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorSuccessPreservesFullRetainedSnapshot
 * @tc.desc: A successful commit is not merely accepted: the complete registry
 *           snapshot (parameter count/index/fixed frame slot and all five
 *           constant maps), the structural block snapshot (ids, control targets,
 *           predecessors, branch targets, unconditional predecessor ids and
 *           loop/exception flags) and every survivor's own record (identity,
 *           representation, owner, input count and input pointers) are
 *           byte-identical before and after, while the dead selections are gone.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorSuccessPreservesFullRetainedSnapshot)
{
    EditorFixture fixture = BuildEditorFixture();
    ASSERT_NE(fixture.graph, nullptr) << "editor fixture construction failed";
    RunVerifier(fixture.graph);
    ASSERT_EQ(fixture.graph->GetParameterCount(), 1U);
    ASSERT_EQ(fixture.parameter->GetFrameSlotIndex(), 3);

    const std::string registriesBefore = SnapshotRegistriesText(fixture.graph);
    const std::string controlBefore = SnapshotControlText(fixture.graph);
    const std::vector<std::string> survivorsBefore {
        DescribeVertexRecord(fixture.liveProducer),
        DescribeVertexRecord(fixture.parameter),
        DescribeVertexRecord(fixture.livePhi),
        DescribeVertexRecord(fixture.branch),
        DescribeVertexRecord(fixture.trueJump),
        DescribeVertexRecord(fixture.falseJump),
        DescribeVertexRecord(fixture.mergeReturn),
    };

    RemovalRequest request;
    MarkDeadEditorSelection(fixture, request);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, request), DceResult::Changed);

    EXPECT_EQ(SnapshotRegistriesText(fixture.graph), registriesBefore) <<
        "parameter slots and constant maps must be unchanged";
    EXPECT_EQ(SnapshotControlText(fixture.graph), controlBefore) <<
        "blocks, control targets, predecessors and loop flags must be unchanged";
    const std::vector<Vertex *> survivors {fixture.liveProducer, fixture.parameter, fixture.livePhi, fixture.branch,
                                            fixture.trueJump, fixture.falseJump, fixture.mergeReturn};
    for (size_t index = 0; index < survivors.size(); ++index) {
        EXPECT_EQ(DescribeVertexRecord(survivors[index]), survivorsBefore[index]) <<
            "a survivor must keep its identity, representation, owner and inputs";
    }
    EXPECT_EQ(fixture.graph->GetParameterCount(), 1U);
    EXPECT_EQ(fixture.graph->GetParameter(0), fixture.parameter);
    EXPECT_EQ(fixture.parameter->GetFrameSlotIndex(), 3) << "the fixed frame slot must be unchanged";
    EXPECT_EQ(fixture.livePhi->GetInput(0), fixture.liveProducer);
    EXPECT_EQ(fixture.livePhi->GetInput(1), fixture.liveProducer);
}

/**
 * @tc.name: ArkSteedDceTest.EditorSuccessPreservesEagerLazyAndCatchState
 * @tc.desc: A successful commit on a graph with retained frame-bearing and
 *           catch-bearing survivors keeps the complete registry and structural
 *           snapshot byte-identical and every survivor's own record unchanged,
 *           including the retained eager frame value, the present lazy frame
 *           value and the installed catch target with its stored catch-site
 *           index. Only the dead value's executable membership changes.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorSuccessPreservesEagerLazyAndCatchState)
{
    CatchFixture fixture = BuildCatchFixture();
    ASSERT_NE(fixture.graph, nullptr) << "catch fixture construction failed";

    ValueVertex *deadValue = NewVertex<I32AddVertex>(
        {fixture.graph->GetInt32Constant(5), fixture.graph->GetInt32Constant(5)});
    ASSERT_NE(deadValue, nullptr) << "catch fixture construction failed";
    deadValue->SetOwner(fixture.entry);
    fixture.entry->GetVertices().insert(fixture.entry->GetVertices().begin(), deadValue);

    I32AddWithOverflowVertex *eagerSurvivor = nullptr;
    AddRetainedDeoptState(fixture, eagerSurvivor);
    ASSERT_FALSE(HasFatalFailure());

    // The catch block keeps one installed catch Phi covering the exceptional
    // domain (two registered throwables).
    PhiVertex *catchPhi = AddCatchPhi(fixture, {fixture.second, fixture.first});
    ASSERT_NE(catchPhi, nullptr) << "catch fixture construction failed";
    ASSERT_EQ(catchPhi->GetInputCount(), 2U);
    RunVerifier(fixture.graph);

    const std::string registriesBefore = SnapshotRegistriesText(fixture.graph);
    const std::string controlBefore = SnapshotControlText(fixture.graph);
    const std::vector<Vertex *> survivors {fixture.firstThrowable, fixture.secondThrowable, fixture.entryReturn,
                                            fixture.catchJump, fixture.handlerReturn, eagerSurvivor, catchPhi};
    std::vector<std::string> survivorsBefore;
    for (Vertex *survivor : survivors) {
        survivorsBefore.push_back(DescribeVertexRecord(survivor));
    }

    RemovalRequest removal;
    removal.MarkRemoved(deadValue);
    FilterOutcome diagnostic = FilterRemovalsCaptured(fixture.graph, &chunk_, removal, capture_);
    EXPECT_EQ(diagnostic.result, DceResult::Changed) <<
        "the dead value must be removed: " << diagnostic.log;
    EXPECT_EQ(SnapshotRegistriesText(fixture.graph), registriesBefore) <<
        "parameter slots and constant maps must be unchanged";
    EXPECT_EQ(SnapshotControlText(fixture.graph), controlBefore) <<
        "blocks, control targets, predecessors and exception flags must be unchanged";
    for (size_t index = 0; index < survivors.size(); ++index) {
        EXPECT_EQ(DescribeVertexRecord(survivors[index]), survivorsBefore[index]) <<
            "a survivor must keep its identity, hidden references and inputs";
    }
    EXPECT_EQ(std::find(fixture.entry->GetVertices().begin(), fixture.entry->GetVertices().end(), deadValue),
              fixture.entry->GetVertices().end()) <<
        "the dead value must be absent after the commit";

    CheckRetainedDeoptState(fixture, eagerSurvivor, catchPhi);

    RunVerifier(fixture.graph);
    RunPreRegallocConsumers(fixture.graph);
}

/**
 * @tc.name: ArkSteedDceTest.EditorSnapshotSensitivityDetectsOptionAndReferenceMutations
 * @tc.desc: Sensitivity control for the preservation snapshot itself: an option
 *           mutation (the branch condition), a hidden-reference mutation (a lazy
 *           frame value) and an order mutation (the merge Phi list) each change
 *           the snapshot, while restoring them restores the snapshot exactly. This
 *           proves the snapshot observes initialized option and hidden-reference
 *           state, not only the mutable vectors.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, EditorSnapshotSensitivityDetectsOptionAndReferenceMutations)
{
    EditorFixture fixture = BuildEditorFixture();
    ASSERT_NE(fixture.graph, nullptr) << "editor fixture construction failed";
    const std::string baseline = SnapshotGraphText(fixture.graph);
    ASSERT_NE(fixture.branch, nullptr);

    fixture.branch->SetCondition(Condition::NOT_EQUAL);
    EXPECT_NE(SnapshotGraphText(fixture.graph), baseline) << "the snapshot must observe the condition option";
    fixture.branch->SetCondition(Condition::EQUAL);
    EXPECT_EQ(SnapshotGraphText(fixture.graph), baseline);

    std::swap(fixture.merge->GetPhis()[0], fixture.merge->GetPhis()[1]);
    EXPECT_NE(SnapshotGraphText(fixture.graph), baseline) << "the snapshot must observe Phi order";
    std::swap(fixture.merge->GetPhis()[0], fixture.merge->GetPhis()[1]);
    EXPECT_EQ(SnapshotGraphText(fixture.graph), baseline);

    fixture.deadPhi->SetOwner(fixture.entry);
    EXPECT_NE(SnapshotGraphText(fixture.graph), baseline) << "the snapshot must observe the owner";
    fixture.deadPhi->SetOwner(fixture.merge);
    EXPECT_EQ(SnapshotGraphText(fixture.graph), baseline);
}

}  // namespace panda::ecmascript::arksteed
