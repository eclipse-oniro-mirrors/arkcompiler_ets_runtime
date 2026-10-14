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

#include "ecmascript/arksteed/arksteed_assembler-inl.h"
#include "ecmascript/arksteed/arksteed_codegen.h"
#include "ecmascript/arksteed/arksteed_comment.h"
#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_graph_processor.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/arksteed/arksteed_regalloc.h"
#include "ecmascript/arksteed/arksteed_regalloc_processors.h"
#include "ecmascript/arksteed/arksteed_safepoint_table.h"
#include "ecmascript/mem/chunk.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/tests/test_helper.h"

#include <cstddef>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace panda::ecmascript::arksteed {

using panda::test::BaseTestWithScope;

/*
 * Unit domain for matrix row G7.4u: emission and layout of deferred code
 * (C11 of `arksteed-cpp-unit-test-domains`).
 *
 * Mechanism: `ArkSteedCodeGenerator::Generate()` emits the main flow of the graph
 * and then `EmitDeferredCode()` walks the deferred-code list the visited vertices
 * queued. Every deferred record is marked with one `Deferred block` comment and is
 * emitted behind the whole main flow.
 *
 * Observable (D4): the composition and the order of the code comments recorded by
 * `ArkSteedAssembler` when comments are enabled. Machine-code bytes and absolute
 * offsets are never compared; the only values read from the emission are the
 * comment texts, their `pcOffset` ordering and the emitted code size.
 *
 * Fixtures (H4):
 *   * A - a real graph without an EcmaVM: `NativeAreaAllocator`, `Chunk`, `Graph`,
 *     `BB` and real IR vertices. The graph of every case is a single block that ends
 *     in its `ReturnVertex`, so the `Epilogue` comment of the return marks the beginning
 *     of the last code of the main flow.
 *   * C - `ArkSteedAssembler(chunk, nullptr, entryThread)` with
 *     `EnableComments(true)`. `compilerThread_` is never dereferenced on these paths,
 *     but every deferred body reads the glue address and the stub table from the entry
 *     thread, so `entryThread` must be a real `JSThread`; a null thread would trip the
 *     production `ASSERT` in Debug and make the case red.
 *
 * Independent oracle (D2): the number of deferred records is derived from the graph the
 * case builds, not from the production deferred list. `EnsureWritableElementsVertex`
 * carries `IS_DEFERRED_CALL` and its codegen path queues exactly one
 * `DeferredCopyCOWElements` per vertex, so a graph with `N` such vertices queues exactly
 * `N` records; a graph whose vertices carry no `IS_DEFERRED_CALL` queues none. The
 * expected comment layout (one `Deferred block` marker per record, the body of a
 * `DeferredCopyCOWElements` record starting with the `CopyCOWElements deferred call`
 * comment, and the whole deferred region behind the `Epilogue` comment of the return)
 * follows from the documented emission order of `EmitDeferredCode()`, not from the
 * emitted list itself.
 *
 * Debug/Release: the negative controls of the domain compare comment lists owned by the
 * test; they do not rely on a production `ASSERT`, so no case uses `#ifndef NDEBUG`, no
 * case is skipped and no case is disabled in Release (H5).
 */
namespace {

constexpr int32_t FIELD_OFFSET = 8;
constexpr const char *DEFERRED_BLOCK_COMMENT = "Deferred block";
constexpr const char *COW_BODY_COMMENT = "CopyCOWElements deferred call";
constexpr const char *MAIN_FLOW_TAIL_COMMENT = "Epilogue";
constexpr size_t COMMENT_NOT_FOUND = std::numeric_limits<size_t>::max();

// Shape of the fixture graph (fixture A). `cowEnsureVertices` vertices of
// `EnsureWritableElementsVertex` queue one `DeferredCopyCOWElements` each;
// `barrierFieldStores` vertices of `StoreTaggedFieldWithBarrierVertex` carry a heap
// value kind, so the emitter appends its post-store barrier through a deferred barrier
// call. `plainFieldStores` vertices of `StoreTaggedFieldVertex` queue nothing.
struct GraphShape {
    uint32_t cowEnsureVertices {0};
    uint32_t barrierFieldStores {0};
    uint32_t plainFieldStores {0};
};

struct CodegenRun {
    std::vector<CommentEntry> comments {};
    size_t codeSize {0};
};

// One full production pipeline over the fixture graph: the real register-allocator
// processors, the real register allocator and the real code generator. The returned
// comment list is a copy, so the emission state may go out of scope with the case.
CodegenRun RunCodegen(JSThread *entryThread, const GraphShape &shape)
{
    NativeAreaAllocator nativeArea;
    Chunk chunk(&nativeArea);
    Graph *graph = Graph::New(&chunk);

    // The entry block of a real graph is created without a register merge state
    // (`GraphBuilder` creates `blocks_[0]` with a plain `BB::New`): a block with no
    // predecessors has no incoming register state to merge, so the allocator starts it
    // with all registers free.
    BB *block = BB::New(&chunk);
    graph->Add(block);

    ValueVertex *elements = graph->GetHeapConstant(0, 0);
    ValueVertex *receiver = graph->GetHeapConstant(1, 0);
    ValueVertex *glue = graph->GetInt64Constant(0);

    for (uint32_t i = 0; i < shape.cowEnsureVertices; i++) {
        std::initializer_list<ValueVertex *> inputs {elements, receiver};
        auto *ensure = Vertex::New<EnsureWritableElementsVertex>(&chunk, inputs);
        ensure->SetOwner(block);
        block->AddVertex(ensure);
    }
    for (uint32_t i = 0; i < shape.barrierFieldStores; i++) {
        std::initializer_list<ValueVertex *> inputs {glue, receiver, elements};
        auto *store = Vertex::New<StoreTaggedFieldWithBarrierVertex>(
            &chunk, inputs, FIELD_OFFSET, ArkSteedWriteBarrierValueKind::HeapObject);
        store->SetOwner(block);
        block->AddVertex(store);
    }
    for (uint32_t i = 0; i < shape.plainFieldStores; i++) {
        std::initializer_list<ValueVertex *> inputs {receiver, elements};
        auto *store = Vertex::New<StoreTaggedFieldVertex>(&chunk, inputs, FIELD_OFFSET);
        store->SetOwner(block);
        block->AddVertex(store);
    }
    {
        std::initializer_list<ValueVertex *> inputs {elements};
        auto *returns = Vertex::New<ReturnVertex>(&chunk, inputs);
        returns->SetOwner(block);
        block->SetControlVertex(returns);
    }

    GraphProcessor<VertexInfoAllocateProcessor> vertexInfoProcessor;
    vertexInfoProcessor.Run(graph);
    GraphProcessor<ValueLocationConstraintProcessor> constraintProcessor;
    constraintProcessor.Run(graph);
    GraphProcessor<MaxCallDepthProcessor> callDepthProcessor;
    callDepthProcessor.Run(graph);
    GraphProcessor<LivenessProcessor> livenessProcessor;
    livenessProcessor.Run(graph);
    ArkSteedRegisterAllocator registerAllocator(graph);

    ArkSteedAssembler assembler(&chunk, nullptr, entryThread);
    assembler.EnableComments(true);
    ArkSteedSafepointTableBuilder safepointBuilder(&chunk);
    ArkSteedCodeGenerator codegen(&assembler, graph, &safepointBuilder);
    codegen.Generate();

    return {assembler.GetCommentList().GetComments(), assembler.GetCodeSize()};
}

size_t CountComments(const std::vector<CommentEntry> &comments, std::string_view text)
{
    size_t count = 0;
    for (const CommentEntry &entry : comments) {
        if (entry.comment == text) {
            ++count;
        }
    }
    return count;
}

size_t FirstCommentIndex(const std::vector<CommentEntry> &comments, std::string_view text)
{
    for (size_t i = 0; i < comments.size(); i++) {
        if (comments[i].comment == text) {
            return i;
        }
    }
    return COMMENT_NOT_FOUND;
}

size_t LastCommentIndex(const std::vector<CommentEntry> &comments, std::string_view text)
{
    for (size_t i = comments.size(); i > 0; i--) {
        if (comments[i - 1].comment == text) {
            return i - 1;
        }
    }
    return COMMENT_NOT_FOUND;
}

// Detector of the deferred-code layout (C11) over one comment list of the single-block
// fixture. It is red when a deferred block is lost or duplicated (the marker count
// differs from the number of records the graph queues), when a marker carries no body of
// its own, or when a marker lands inside the main flow, in front of the `Epilogue`
// comment the block's return emits and in front of the code that comment starts.
// `expectedRecords` is the independent oracle above.
struct DeferredLayout {
    size_t expectedRecords {0};
    size_t markerCount {0};
    size_t cowBodyCount {0};
    size_t firstMarkerIndex {COMMENT_NOT_FOUND};
    size_t mainFlowTailIndex {COMMENT_NOT_FOUND};
    uint32_t firstMarkerPcOffset {0};
    uint32_t mainFlowTailPcOffset {0};
    bool everyMarkerOwnsItsBody {false};
    bool pcOffsetsNonDecreasing {false};

    bool Accepts() const
    {
        if (mainFlowTailIndex == COMMENT_NOT_FOUND) {
            // The main flow of the fixture is not visible: the emission did not run.
            return false;
        }
        if (markerCount != expectedRecords || cowBodyCount != expectedRecords) {
            return false;
        }
        if (expectedRecords == 0) {
            return true;
        }
        // The deferred region must start behind the last comment of the main flow and
        // behind the code that comment marks the beginning of.
        if (firstMarkerIndex <= mainFlowTailIndex || firstMarkerPcOffset <= mainFlowTailPcOffset) {
            return false;
        }
        return everyMarkerOwnsItsBody && pcOffsetsNonDecreasing;
    }

    std::string Describe() const
    {
        std::string text = "expected=" + std::to_string(expectedRecords) +
                           " markers=" + std::to_string(markerCount) +
                           " cowBodies=" + std::to_string(cowBodyCount) +
                           " firstMarker=" + std::to_string(firstMarkerIndex) +
                           " mainFlowTail=" + std::to_string(mainFlowTailIndex) +
                           " firstMarkerPc=" + std::to_string(firstMarkerPcOffset) +
                           " mainFlowTailPc=" + std::to_string(mainFlowTailPcOffset) +
                           " bodiesFollowMarkers=" + (everyMarkerOwnsItsBody ? "true" : "false") +
                           " pcOffsetsNonDecreasing=" + (pcOffsetsNonDecreasing ? "true" : "false");
        return text;
    }
};

DeferredLayout InspectLayout(const std::vector<CommentEntry> &comments, size_t expectedRecords)
{
    DeferredLayout layout;
    layout.expectedRecords = expectedRecords;
    layout.mainFlowTailIndex = LastCommentIndex(comments, MAIN_FLOW_TAIL_COMMENT);
    if (layout.mainFlowTailIndex != COMMENT_NOT_FOUND) {
        layout.mainFlowTailPcOffset = comments[layout.mainFlowTailIndex].pcOffset;
    }

    bool previousMarkerSeen = false;
    uint32_t previousMarkerPcOffset = 0;
    layout.pcOffsetsNonDecreasing = true;
    layout.everyMarkerOwnsItsBody = true;

    for (size_t i = 0; i < comments.size(); i++) {
        const CommentEntry &entry = comments[i];
        if (entry.comment == DEFERRED_BLOCK_COMMENT) {
            if (layout.markerCount == 0) {
                layout.firstMarkerIndex = i;
                layout.firstMarkerPcOffset = entry.pcOffset;
            }
            if (previousMarkerSeen && entry.pcOffset < previousMarkerPcOffset) {
                layout.pcOffsetsNonDecreasing = false;
            }
            previousMarkerPcOffset = entry.pcOffset;
            previousMarkerSeen = true;
            layout.markerCount++;

            // The body of a COW record emits the COW call comment before anything else
            // and before the next record starts.
            if (i + 1 >= comments.size() || comments[i + 1].comment != COW_BODY_COMMENT) {
                layout.everyMarkerOwnsItsBody = false;
            }
            continue;
        }
        if (entry.comment == COW_BODY_COMMENT) {
            layout.cowBodyCount++;
        }
    }
    return layout;
}

// Index of the marker whose record owns the COW body comment, or COMMENT_NOT_FOUND.
size_t MarkerOwningCowBody(const std::vector<CommentEntry> &comments)
{
    size_t cowBody = FirstCommentIndex(comments, COW_BODY_COMMENT);
    if (cowBody == COMMENT_NOT_FOUND) {
        return COMMENT_NOT_FOUND;
    }
    size_t marker = COMMENT_NOT_FOUND;
    for (size_t i = 0; i < cowBody; i++) {
        if (comments[i].comment == DEFERRED_BLOCK_COMMENT) {
            marker = i;
        }
    }
    return marker;
}

size_t LastMarkerIndex(const std::vector<CommentEntry> &comments)
{
    return LastCommentIndex(comments, DEFERRED_BLOCK_COMMENT);
}

}  // namespace

class ArkSteedDeferredCodeTest : public BaseTestWithScope<false> {
};

/*
 * @tc.name: ArkSteedDeferredCodeTest.GraphWithoutDeferredVertexEmitsNoDeferredBlock
 * @tc.desc: a graph whose vertices carry no IS_DEFERRED_CALL property queues no deferred
 *           record, so the comment list of the emission contains no `Deferred block`
 *           marker while the main flow is still commented.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeferredCodeTest, GraphWithoutDeferredVertexEmitsNoDeferredBlock)
{
    CodegenRun run = RunCodegen(thread, GraphShape {.plainFieldStores = 1});

    ASSERT_GT(run.codeSize, 0u) << "the fixture graph must have emitted code";
    EXPECT_GT(run.comments.size(), 0u) << "comments must be enabled for this domain";
    EXPECT_NE(FirstCommentIndex(run.comments, MAIN_FLOW_TAIL_COMMENT), COMMENT_NOT_FOUND)
        << "the main flow of the fixture graph must be visible";

    DeferredLayout layout = InspectLayout(run.comments, 0);
    EXPECT_EQ(CountComments(run.comments, DEFERRED_BLOCK_COMMENT), 0u)
        << "a graph without a deferred block must not emit a deferred block comment: " << layout.Describe();
    EXPECT_TRUE(layout.Accepts()) << layout.Describe();
}

/*
 * @tc.name: ArkSteedDeferredCodeTest.CowEnsureVertexQueuesOneDeferredBlockBehindTheMainFlow
 * @tc.desc: the fixture graph has exactly one EnsureWritableElementsVertex, which queues
 *           exactly one DeferredCopyCOWElements; the emission records one `Deferred block`
 *           marker with the COW body behind the tail of the main flow (the Epilogue
 *           comment of the block's return).
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeferredCodeTest, CowEnsureVertexQueuesOneDeferredBlockBehindTheMainFlow)
{
    CodegenRun run = RunCodegen(thread, GraphShape {.cowEnsureVertices = 1});

    ASSERT_GT(run.codeSize, 0u);
    DeferredLayout layout = InspectLayout(run.comments, 1);
    ASSERT_NE(layout.firstMarkerIndex, COMMENT_NOT_FOUND)
        << "the deferred record of the COW vertex is missing: " << layout.Describe();
    ASSERT_NE(layout.mainFlowTailIndex, COMMENT_NOT_FOUND);

    EXPECT_TRUE(layout.Accepts()) << layout.Describe();
    EXPECT_EQ(CountComments(run.comments, DEFERRED_BLOCK_COMMENT), 1u);
    EXPECT_EQ(CountComments(run.comments, COW_BODY_COMMENT), 1u)
        << "the COW record must emit exactly one body comment";

    // The deferred block is emitted as one whole region at the very end of the emission:
    // the first marker follows the `Epilogue` comment of the return in the list and carries
    // a higher pcOffset, so it is laid out behind the main flow. The comment directly in
    // front of it still belongs to the main flow.
    ASSERT_GT(layout.firstMarkerIndex, 0u);
    EXPECT_NE(run.comments[layout.firstMarkerIndex - 1].comment, DEFERRED_BLOCK_COMMENT);
    EXPECT_NE(run.comments[layout.firstMarkerIndex - 1].comment, COW_BODY_COMMENT);
    EXPECT_GE(run.comments[layout.firstMarkerIndex - 1].pcOffset,
              run.comments[layout.mainFlowTailIndex].pcOffset)
        << "the comment in front of the deferred region must be the tail of the main flow";
    EXPECT_GT(run.comments[layout.firstMarkerIndex].pcOffset,
              run.comments[layout.mainFlowTailIndex].pcOffset)
        << "the deferred block must be laid out behind the main flow";
}

/*
 * @tc.name: ArkSteedDeferredCodeTest.TwoCowVerticesQueueTwoDeferredBlocksInPostingOrder
 * @tc.desc: two EnsureWritableElementsVertex instances queue two records; the emission
 *           records two markers, each owning one COW body, with non-decreasing pcOffsets,
 *           and the whole deferred region stays behind the main flow.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeferredCodeTest, TwoCowVerticesQueueTwoDeferredBlocksInPostingOrder)
{
    CodegenRun run = RunCodegen(thread, GraphShape {.cowEnsureVertices = 2});

    ASSERT_GT(run.codeSize, 0u);
    DeferredLayout layout = InspectLayout(run.comments, 2);
    EXPECT_TRUE(layout.Accepts()) << layout.Describe();
    EXPECT_EQ(CountComments(run.comments, DEFERRED_BLOCK_COMMENT), 2u);
    EXPECT_EQ(CountComments(run.comments, COW_BODY_COMMENT), 2u);

    ASSERT_NE(layout.mainFlowTailIndex, COMMENT_NOT_FOUND);
    ASSERT_NE(layout.firstMarkerIndex, COMMENT_NOT_FOUND);
    EXPECT_GT(layout.firstMarkerIndex, layout.mainFlowTailIndex)
        << "no deferred block may be emitted inside the main flow";

    // The two records are indistinguishable by content (both are COW records), so the
    // posting order is observable through the pairs: the first marker within the deferred
    // region must own the first COW body of that region.
    size_t cowBody = FirstCommentIndex(run.comments, COW_BODY_COMMENT);
    ASSERT_NE(cowBody, COMMENT_NOT_FOUND);
    EXPECT_EQ(MarkerOwningCowBody(run.comments), layout.firstMarkerIndex)
        << "the first marker of the region must own the first COW body";
}

/*
 * @tc.name: ArkSteedDeferredCodeTest.DeferredBlocksFollowPostingOrderAcrossVertexKinds
 * @tc.desc: a COW vertex posted before a deferred barrier store is emitted before that
 *           store's deferred record; the COW body comment therefore sits behind the first
 *           marker and in front of the last marker of the region. This is the observable
 *           form of "the comments follow the order in which the records were queued".
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeferredCodeTest, DeferredBlocksFollowPostingOrderAcrossVertexKinds)
{
    CodegenRun run = RunCodegen(thread, GraphShape {.cowEnsureVertices = 1, .barrierFieldStores = 1});

    ASSERT_GT(run.codeSize, 0u);
    ASSERT_NE(LastCommentIndex(run.comments, MAIN_FLOW_TAIL_COMMENT), COMMENT_NOT_FOUND);
    // The barrier store queues at least one deferred barrier call; the exact count depends
    // on the barrier configuration, so the layout oracle is stated as a lower bound here.
    size_t markerCount = CountComments(run.comments, DEFERRED_BLOCK_COMMENT);
    ASSERT_GE(markerCount, 2u) << "the COW record and the barrier record must both be emitted";
    ASSERT_EQ(CountComments(run.comments, COW_BODY_COMMENT), 1u)
        << "only the COW vertex of this fixture emits a body comment";

    size_t cowBody = FirstCommentIndex(run.comments, COW_BODY_COMMENT);
    size_t firstMarker = FirstCommentIndex(run.comments, DEFERRED_BLOCK_COMMENT);
    size_t lastMarker = LastMarkerIndex(run.comments);
    ASSERT_NE(cowBody, COMMENT_NOT_FOUND);
    ASSERT_NE(firstMarker, COMMENT_NOT_FOUND);
    ASSERT_NE(lastMarker, COMMENT_NOT_FOUND);

    EXPECT_GT(cowBody, firstMarker)
        << "the COW record (posted first) must be emitted before the barrier records";
    EXPECT_LT(cowBody, lastMarker)
        << "the barrier record (posted last) must be emitted after the COW record";
    EXPECT_EQ(MarkerOwningCowBody(run.comments), firstMarker)
        << "the first marker must be the COW record posted first";
    EXPECT_GT(firstMarker, LastCommentIndex(run.comments, MAIN_FLOW_TAIL_COMMENT))
        << "the whole deferred region must sit behind the main flow";
    EXPECT_LT(run.comments[firstMarker].pcOffset, run.comments[cowBody].pcOffset)
        << "the COW body must be laid out behind its own marker";
}

/*
 * @tc.name: ArkSteedDeferredCodeTest.MutationControlRejectsLostBlockAndMidFlowBlock
 * @tc.desc: the detector of the deferred layout is sensitive: a lost deferred block
 *           (fewer markers than the graph queues) and a deferred block placed inside the
 *           main flow are both rejected, while the unmutated lists are accepted.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDeferredCodeTest, MutationControlRejectsLostBlockAndMidFlowBlock)
{
    CodegenRun emptyRun = RunCodegen(thread, GraphShape {.plainFieldStores = 1});
    CodegenRun cowRun = RunCodegen(thread, GraphShape {.cowEnsureVertices = 1});

    DeferredLayout emptyLayout = InspectLayout(emptyRun.comments, 0);
    DeferredLayout cowLayout = InspectLayout(cowRun.comments, 1);
    ASSERT_TRUE(emptyLayout.Accepts()) << emptyLayout.Describe();
    ASSERT_TRUE(cowLayout.Accepts()) << cowLayout.Describe();

    // A lost deferred block: the COW graph queues one record, but the emission of the
    // graph without deferred vertices carries no marker.
    DeferredLayout lostBlock = InspectLayout(emptyRun.comments, 1);
    EXPECT_FALSE(lostBlock.Accepts())
        << "a missing deferred block must be detected: " << lostBlock.Describe();

    // A spurious deferred block: the plain fixture queues no record, so its marker count
    // must not satisfy an expectation of one.
    DeferredLayout spuriousBlock = InspectLayout(cowRun.comments, 0);
    EXPECT_FALSE(spuriousBlock.Accepts())
        << "a spurious deferred block must be detected: " << spuriousBlock.Describe();

    // A deferred block inside the main flow: place the marker and its body of the COW run
    // in front of the `Epilogue` comment, with the pcOffset of the code that precedes the
    // epilogue, the way an emission inside the main flow would record them. The count stays
    // at one, so only the tail invariant can reject the list.
    const std::vector<CommentEntry> &observed = cowRun.comments;
    size_t marker = FirstCommentIndex(observed, DEFERRED_BLOCK_COMMENT);
    size_t tail = LastCommentIndex(observed, MAIN_FLOW_TAIL_COMMENT);
    ASSERT_NE(marker, COMMENT_NOT_FOUND);
    ASSERT_NE(tail, COMMENT_NOT_FOUND);
    ASSERT_LT(marker + 1, observed.size());
    ASSERT_EQ(observed[marker + 1].comment, COW_BODY_COMMENT);
    ASSERT_GT(tail, 0u);
    uint32_t insideMainFlowPcOffset = observed[tail - 1].pcOffset;
    ASSERT_LT(insideMainFlowPcOffset, observed[tail].pcOffset);
    CommentEntry bodyEntry(insideMainFlowPcOffset, COW_BODY_COMMENT);
    std::vector<CommentEntry> midFlow;
    midFlow.reserve(observed.size());
    for (const CommentEntry &entry : observed) {
        if (entry.comment == DEFERRED_BLOCK_COMMENT || entry.comment == COW_BODY_COMMENT) {
            continue;
        }
        if (entry.comment == MAIN_FLOW_TAIL_COMMENT) {
            midFlow.emplace_back(insideMainFlowPcOffset, DEFERRED_BLOCK_COMMENT);
            midFlow.push_back(bodyEntry);
        }
        midFlow.push_back(entry);
    }

    DeferredLayout midFlowLayout = InspectLayout(midFlow, 1);
    EXPECT_EQ(midFlowLayout.markerCount, 1u);
    EXPECT_EQ(midFlowLayout.cowBodyCount, 1u);
    EXPECT_FALSE(midFlowLayout.Accepts())
        << "a deferred block inside the main flow must be detected: " << midFlowLayout.Describe();
    ASSERT_NE(midFlowLayout.mainFlowTailIndex, COMMENT_NOT_FOUND);
    ASSERT_NE(midFlowLayout.firstMarkerIndex, COMMENT_NOT_FOUND);
    EXPECT_LT(midFlowLayout.firstMarkerIndex, midFlowLayout.mainFlowTailIndex);
    EXPECT_LE(midFlowLayout.firstMarkerPcOffset, midFlowLayout.mainFlowTailPcOffset);
}

}  // namespace panda::ecmascript::arksteed
