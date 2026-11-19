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

#include <algorithm>
#include <cmath>
#include <getopt.h>
#include <limits>

#include "ecmascript/arksteed/arksteed_compile_info_facts.h"
#include "ecmascript/arksteed/arksteed_dce.h"
#include "ecmascript/arksteed/arksteed_phi_representation_selector.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/js_runtime_options.h"
#include "gtest/gtest.h"

namespace panda::ecmascript::arksteed {
class ArkSteedPhiRepresentationSelectorTest : public testing::Test {
protected:
    static constexpr uint32_t ENTRY_CHECKPOINT_OFFSET = 37;
    static constexpr uint32_t BACKEDGE_CHECKPOINT_OFFSET = 23;
    static constexpr uint32_t CHECK_BYTECODE_OFFSET = 42;

    NativeAreaAllocator native_;
    Chunk chunk_ {&native_};
    Graph graph_ {&chunk_};

    ValueVertex *Tagged(int value)
    {
        return graph_.GetTaggedConstant(JSTaggedValue(value).GetRawData());
    }

    BB *Block()
    {
        auto *block = BB::New(&chunk_);
        graph_.Add(block);
        auto *ret = Vertex::New<ReturnVertex>(&chunk_, std::array<ValueVertex *, 1> {Tagged(0)});
        ret->SetOwner(block);
        block->SetControlVertex(ret);
        return block;
    }

    BB *Diamond(BB *entry)
    {
        auto *left = Block();
        auto *right = Block();
        auto *merge = Block();
        left->AddPredecessor(entry);
        right->AddPredecessor(entry);
        auto *branch = Vertex::New<BranchIfTaggedHeapObjectVertex>(
            &chunk_, std::array<ValueVertex *, 1> {Tagged(0)}, left, right);
        branch->SetOwner(entry);
        entry->SetControlVertex(branch);
        for (auto *predecessor : {left, right}) {
            auto *jump = Vertex::New<JumpVertex>(&chunk_, size_t(0), merge);
            jump->SetOwner(predecessor);
            predecessor->SetControlVertex(jump);
            merge->AddPredecessor(predecessor);
        }
        return merge;
    }

    PhiVertex *Phi(BB *block, ValueVertex *left, ValueVertex *right)
    {
        auto *phi = Vertex::New<PhiVertex>(
            &chunk_, std::array<ValueVertex *, 2> {left, right}, VirtualRegister(0));
        phi->SetOwner(block);
        phi->SetIsUnmergedLoopPhi(block->IsLoopHeader());
        NodeType entryType = left == nullptr ? NodeType::UNKNOWN : left->GetStaticType();
        if (block->IsLoopHeader()) {
            phi->SetPostLoopType(entryType);
        } else if (!block->IsExceptionHandler()) {
            phi->SetType(UnionNodeType(entryType, right == nullptr ? NodeType::UNKNOWN : right->GetStaticType()));
        }
        block->AddPhiVertex(phi);
        return phi;
    }

    template<class T, class... Args>
    T *Unary(BB *block, ValueVertex *input, Args... args)
    {
        if constexpr (std::is_same_v<T, TaggedIntToI32Vertex>) {
            input->MaybeRecordUseReprHint(UseRepresentation::INT32);
        }
        auto *value = Vertex::New<T>(&chunk_, std::array<ValueVertex *, 1> {input}, args...);
        value->SetOwner(block);
        block->AddVertex(value);
        return value;
    }

    CheckedNumberToF64Vertex *NumberUse(BB *block, ValueVertex *input,
                                       UseRepresentation use = UseRepresentation::FLOAT64)
    {
        input->MaybeRecordUseReprHint(use);
        return Unary<CheckedNumberToF64Vertex>(block, input, &chunk_, 0);
    }

    ValueVertex *DynamicDouble(BB *block)
    {
        auto *value = Vertex::New<F64AddVertex>(&chunk_, std::array<ValueVertex *, 2> {
            graph_.GetFloat64Constant(1.25), graph_.GetFloat64Constant(2.5)});
        value->SetOwner(block);
        block->AddVertex(value);
        return Unary<F64ToTaggedDoubleVertex>(block, value);
    }

    DeoptVertex *EagerFrame(BB *block, ValueVertex *value)
    {
        auto *deopt = Vertex::New<DeoptVertex>(&chunk_, size_t(0), &chunk_, kungfu::DeoptType::NOTINT1, 0);
        EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk_);
        frame.emplace_back(0, value, DeoptTranslationKind::TAGGED);
        deopt->SetEagerDeoptFrameState(std::move(frame));
        deopt->SetOwner(block);
        block->SetControlVertex(deopt);
        return deopt;
    }

    CallVertex *LazyFrame(BB *block, ValueVertex *value)
    {
        auto *call = Vertex::New<CallVertex>(&chunk_, std::array<ValueVertex *, 3> {
            Tagged(0), Tagged(0), Tagged(0)}, 0);
        auto *frame = chunk_.New<LazyDeoptimizableMixin::LazyDeoptFrameState>(&chunk_);
        frame->emplace_back(0, value, DeoptTranslationKind::TAGGED);
        call->SetLazyDeoptFrameState(frame, 0);
        call->SetOwner(block);
        block->AddVertex(call);
        return call;
    }

    auto Loop()
    {
        BB *entry = Block();
        BB *header = Block();
        BB *body = Block();
        BB *exit = Block();
        header->SetIsLoopHeader(true);
        header->AddPredecessor(entry);
        header->AddPredecessor(body);
        body->AddPredecessor(header);
        exit->AddPredecessor(header);
        auto *enter = Vertex::New<JumpVertex>(&chunk_, size_t(0), header);
        enter->SetOwner(entry);
        entry->SetControlVertex(enter);
        auto *branch = Vertex::New<BranchIfTrueVertex>(&chunk_,
            std::array<ValueVertex *, 1> {Tagged(0)}, body, exit);
        branch->SetOwner(header);
        header->SetControlVertex(branch);
        auto *backedge = Vertex::New<JumpLoopVertex>(&chunk_, size_t(0), &chunk_, header);
        backedge->SetOwner(body);
        backedge->SetPredecessorId(1);
        body->SetControlVertex(backedge);
        return std::array{entry, header, body, exit};
    }

    ValueVertex *UnknownParameter(BB *entry)
    {
        auto *value = Vertex::New<InitialValueVertex>(&chunk_, size_t(0), 3);
        value->SetOwner(entry);
        entry->AddVertex(value);
        return value;
    }

    void CheckpointEntry(BB *entry, ValueVertex *value)
    {
        EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk_);
        frame.emplace_back(0, value, DeoptTranslationKind::TAGGED);
        entry->GetControlVertex()->Cast<JumpVertex>()->SetPhiUntaggingFrameState(
            &chunk_, frame, ENTRY_CHECKPOINT_OFFSET);
    }

    void SelectRepresentations(bool allowHoisting = false, bool allowTruncation = true)
    {
        graph_.SetSpeculativePhiHoistingEnabled(allowHoisting);
        graph_.SetTruncatedInt32PhiEnabled(allowTruncation);
        for (BB *block : graph_) {
            for (auto *phi : block->GetPhis()) {
                if (block->IsLoopHeader()) {
                    ValueVertex *backedge = phi->GetInput(1);
                    phi->SetType(UnionNodeType(phi->GetPostLoopType(),
                        backedge == phi ? phi->GetPostLoopType() : backedge->GetStaticType()));
                    phi->PropagateUseReprHintsToInput(1);
                }
            }
            for (auto *phi : block->GetPhis()) {
                phi->SetIsUnmergedLoopPhi(false);
            }
        }
        PhiRepresentationSelector(&graph_).Run();
        ArkSteedDce::Run(&graph_);
    }

    void CheckNoReference(ValueVertex *removed)
    {
        auto check = [removed](Vertex *vertex) {
            EXPECT_NE(vertex, removed);
            for (uint32_t index = 0; index < vertex->GetInputCount(); ++index) {
                EXPECT_NE(vertex->GetInput(index), removed);
            }
        };
        for (BB *block : graph_) {
            for (auto *phi : block->GetPhis()) {
                check(phi);
            }
            for (auto *vertex : block->GetVertices()) {
                check(vertex);
            }
            check(block->GetControlVertex());
        }
    }
};

TEST_F(ArkSteedPhiRepresentationSelectorTest, IndependentPhiOptionsParseCommandLine)
{
    JSRuntimeOptions options;
    EXPECT_TRUE(options.GetCompilerArkSteedUntaggedPhis());
    EXPECT_FALSE(options.GetCompilerArkSteedSpeculativeHoistPhiUntagging());
    EXPECT_FALSE(options.GetCompilerArkSteedTruncatedInt32Phis());
    EXPECT_TRUE(graph_.IsPhiUntaggingEnabled());
    EXPECT_FALSE(graph_.IsSpeculativePhiHoistingEnabled());
    EXPECT_FALSE(graph_.IsTruncatedInt32PhiEnabled());
    const char *args[] = {"ark_js_vm", "--compiler-arksteed-untagged-phis=false",
                          "--compiler-arksteed-speculative-hoist-phi-untagging=true",
                          "--compiler-arksteed-truncated-int32-phis=true"};
    int previousOptind = optind;
    optind = 0;
    bool parsed = options.ParseCommand(static_cast<int>(std::size(args)), args);
    optind = previousOptind;
    ASSERT_TRUE(parsed);
    EXPECT_FALSE(options.GetCompilerArkSteedUntaggedPhis());
    EXPECT_TRUE(options.GetCompilerArkSteedSpeculativeHoistPhiUntagging());
    EXPECT_TRUE(options.GetCompilerArkSteedTruncatedInt32Phis());
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, DisabledPhiSelectionPreservesConversions)
{
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    auto *untag = Unary<TaggedIntToI32Vertex>(block, phi);
    auto *tag = Unary<I32ToTaggedIntVertex>(block, untag);
    block->GetControlVertex()->SetInput(0, tag);
    graph_.SetPhiUntaggingEnabled(false);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsTagged());
    EXPECT_TRUE(untag->Is<TaggedIntToI32Vertex>());
    EXPECT_EQ(untag->GetInput(0), phi);
    EXPECT_EQ(tag->GetInput(0), untag);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, TruncatedPhiOptionCanBeDisabledIndependently)
{
    auto *entry = Block();
    auto *block = Diamond(entry);
    auto *phi = Phi(block, DynamicDouble(entry), Tagged(0));
    auto *number = NumberUse(block, phi, UseRepresentation::TRUNCATED_INT32);
    auto *trunc = Unary<DoubleToInt32Vertex>(block, number);
    auto *deopt = EagerFrame(block, trunc);
    deopt->SetDeoptFrameValue(0, trunc, DeoptTranslationKind::INT32_TO_TAGGED);
    SelectRepresentations(false, false);
    EXPECT_TRUE(phi->IsFloat64());
    EXPECT_TRUE(trunc->Is<DoubleToInt32Vertex>());
    EXPECT_EQ(trunc->GetInput(0), phi);
    EXPECT_EQ(deopt->GetDeoptFrameValue(0), trunc);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, PreservesUnusedIndexedParameter)
{
    auto *entry = Block();
    auto *parameter = UnknownParameter(entry);
    graph_.AddParameter(parameter);

    SelectRepresentations();

    EXPECT_EQ(graph_.GetParameter(0), parameter);
    bool installed = false;
    for (NonControlVertex *vertex : entry->GetVertices()) {
        installed |= vertex == parameter;
    }
    EXPECT_TRUE(installed);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, PreservesNodeOrderWhenTaggingControlAfterConversionRemoval)
{
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    auto *prefix = LazyFrame(block, Tagged(0));
    auto *untag = Unary<TaggedIntToI32Vertex>(block, phi);
    auto *suffix = LazyFrame(block, Tagged(0));
    block->GetControlVertex()->SetInput(0, phi);

    PhiRepresentationSelector(&graph_).Run();

    ASSERT_TRUE(phi->IsInt32());
    auto *tag = block->GetControlVertex()->GetInput(0);
    ASSERT_TRUE(tag->Is<I32ToTaggedIntVertex>());
    EXPECT_EQ(tag->GetInput(0), phi);
    std::vector<NonControlVertex *> expected {prefix, suffix, tag};
    EXPECT_EQ(std::vector(block->GetVertices().begin(), block->GetVertices().end()), expected);
    CheckNoReference(untag);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, RemovesTrailingConversionsAfterUnchangedNodes)
{
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    auto *prefix = LazyFrame(block, Tagged(0));
    auto *untag = Unary<TaggedIntToI32Vertex>(block, phi);

    PhiRepresentationSelector(&graph_).Run();

    ASSERT_TRUE(phi->IsInt32());
    ASSERT_EQ(block->GetVertices().size(), 1U);
    EXPECT_EQ(block->GetVertices().front(), prefix);
    CheckNoReference(untag);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, TaggingSnapshotsSupportSparseBlockIds)
{
    graph_.NewBlockId();
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    Unary<TaggedIntToI32Vertex>(block, phi);
    auto *call = LazyFrame(block, phi);
    call->SetInput(0, phi);
    graph_.NewBlockId();
    auto *join = Diamond(block);
    join->GetControlVertex()->SetInput(0, phi);

    SelectRepresentations();

    ASSERT_GT(graph_.MaxBlockId(), graph_.NumBlocks());
    ASSERT_TRUE(phi->IsInt32());
    auto *tag = call->GetInput(0);
    ASSERT_TRUE(tag->Is<I32ToTaggedIntVertex>());
    EXPECT_EQ(tag->GetInput(0), phi);
    EXPECT_EQ(join->GetControlVertex()->GetInput(0), tag);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, LoopHintsStopAtBackedgeMerge)
{
    auto *header = Block();
    header->SetIsLoopHeader(true);
    auto *phi = Phi(header, Tagged(1), nullptr);
    phi->MaybeRecordUseReprHint(UseRepresentation::INT32);
    phi->SetInput(1, phi);
    phi->PropagateUseReprHintsToInput(1);
    phi->SetIsUnmergedLoopPhi(false);
    phi->MaybeRecordUseReprHint(UseRepresentation::TAGGED);
    EXPECT_TRUE(phi->GetUseReprHints().Contains(UseRepresentation::TAGGED));
    EXPECT_TRUE(phi->GetSameLoopUseReprHints().Contains(UseRepresentation::INT32));
    EXPECT_FALSE(phi->GetSameLoopUseReprHints().Contains(UseRepresentation::TAGGED));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, NestedLoopAndLateBackedgeHints)
{
    auto *outer = Block();
    auto *inner = Block();
    outer->SetIsLoopHeader(true);
    inner->SetIsLoopHeader(true);
    auto *outerPhi = Phi(outer, Tagged(1), nullptr);
    auto *innerPhi = Phi(inner, outerPhi, nullptr);
    innerPhi->MaybeRecordUseReprHint(UseRepresentation::INT32);
    EXPECT_TRUE(outerPhi->GetSameLoopUseReprHints().Contains(UseRepresentation::INT32));

    auto *lateInput = Phi(Block(), Tagged(2), Tagged(3));
    innerPhi->SetInput(1, lateInput);
    innerPhi->PropagateUseReprHintsToInput(1);
    EXPECT_TRUE(lateInput->GetUseReprHints().Contains(UseRepresentation::INT32));
    innerPhi->SetIsUnmergedLoopPhi(false);
    // Outside the inner loop, but still inside the outer loop.
    innerPhi->MaybeRecordUseReprHint(UseRepresentation::FLOAT64);
    EXPECT_FALSE(innerPhi->GetSameLoopUseReprHints().Contains(UseRepresentation::FLOAT64));
    EXPECT_TRUE(outerPhi->GetSameLoopUseReprHints().Contains(UseRepresentation::FLOAT64));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, FailedOptimisticLoopClearsSameLoopHints)
{
    auto *header = Block();
    header->SetIsLoopHeader(true);
    auto *phi = Phi(header, Tagged(1), nullptr);
    phi->MaybeRecordUseReprHint(UseRepresentation::INT32);
    header->SetIsLoopHeader(false);
    phi->ReduceToSinglePredecessor();
    phi->MaybeRecordUseReprHint(UseRepresentation::TAGGED);
    EXPECT_TRUE(phi->GetSameLoopUseReprHints().Empty());
    EXPECT_TRUE(phi->GetUseReprHints().Contains(UseRepresentation::INT32));
    EXPECT_TRUE(phi->GetUseReprHints().Contains(UseRepresentation::TAGGED));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, RepeatedRequestStillRecordsSameLoopHint)
{
    auto *header = Block();
    header->SetIsLoopHeader(true);
    auto *phi = Phi(header, Tagged(1), Tagged(2));
    phi->SetInLoopScope(false);
    phi->MaybeRecordUseReprHint(UseRepresentation::FLOAT64);
    phi->SetInLoopScope(true);
    phi->MaybeRecordUseReprHint(UseRepresentation::FLOAT64);
    EXPECT_TRUE(phi->GetSameLoopUseReprHints().Contains(UseRepresentation::FLOAT64));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, LoopTypeIsUnknownUntilBackedgeMerge)
{
    auto *header = Block();
    header->SetIsLoopHeader(true);
    auto *phi = Phi(header, Tagged(1), nullptr);
    EXPECT_EQ(phi->GetType(), NodeType::UNKNOWN);
    EXPECT_EQ(phi->GetPostLoopType(), NodeType::INT);
    CompileInfoFacts facts(&chunk_);
    EXPECT_EQ(facts.GetKnownType(phi), NodeType::UNKNOWN);
    auto *number = graph_.GetTaggedConstant(JSTaggedValue(2.5).GetRawData());
    phi->SetInput(1, number);
    phi->MergePostLoopType(facts.GetKnownType(number));
    phi->PromotePostLoopType();
    phi->SetIsUnmergedLoopPhi(false);
    EXPECT_EQ(phi->GetType(), NodeType::NUMBER);
    EXPECT_EQ(facts.GetKnownType(phi), NodeType::NUMBER);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, OptimisticLoopTypeSurvivesConversionToPeeledIteration)
{
    auto *header = Block();
    header->SetIsLoopHeader(true);
    auto *phi = Phi(header, Tagged(1), nullptr);
    phi->SetType(phi->GetPostLoopType());
    EXPECT_EQ(phi->GetStaticType(), NodeType::INT);
    phi->MaybeRecordUseReprHint(UseRepresentation::INT32);
    header->SetIsLoopHeader(false);
    phi->ReduceToSinglePredecessor();
    EXPECT_EQ(phi->GetInputCount(), 1U);
    EXPECT_EQ(phi->GetType(), NodeType::INT);
    EXPECT_TRUE(phi->GetSameLoopUseReprHints().Empty());
    EXPECT_TRUE(phi->GetUseReprHints().Contains(UseRepresentation::INT32));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, PhiTypeDoesNotNarrowItsInputsOnOtherPaths)
{
    auto *entry = Block();
    auto *input = UnknownParameter(entry);
    auto *merge = Diamond(entry);
    CompileInfoFacts leftFacts(&chunk_);
    CompileInfoFacts rightFacts(&chunk_);
    leftFacts.EnsureType(input, NodeType::INT);
    auto *phi = Phi(merge, input, Tagged(1));
    phi->SetType(UnionNodeType(leftFacts.GetKnownType(input), rightFacts.GetKnownType(Tagged(1))));
    EXPECT_EQ(phi->GetType(), NodeType::INT);
    EXPECT_EQ(rightFacts.GetKnownType(input), NodeType::UNKNOWN);
    EXPECT_EQ(input->GetStaticType(), NodeType::UNKNOWN);
    NumberUse(merge, phi);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsTagged());
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, TaggedNumericPhiCanFeedUntaggedPhi)
{
    auto *entry = Block();
    auto *input = UnknownParameter(entry);
    auto *merge = Diamond(entry);
    auto *taggedPhi = Phi(merge, input, Tagged(1));
    // Models the union of predecessor-local integer proofs. The input itself
    // remains unknown, so only the consumer Phi can be selected without checks.
    taggedPhi->SetType(NodeType::INT);
    auto *consumerBlock = Diamond(merge);
    auto *consumer = Phi(consumerBlock, taggedPhi, Tagged(2));
    auto *use = Unary<TaggedIntToI32Vertex>(consumerBlock, consumer);
    consumerBlock->GetControlVertex()->SetInput(0, Unary<I32ToTaggedIntVertex>(consumerBlock, use));
    SelectRepresentations();
    EXPECT_TRUE(taggedPhi->IsTagged());
    ASSERT_TRUE(consumer->IsInt32());
    auto *untag = consumer->GetInput(0);
    ASSERT_TRUE(untag->Is<TaggedIntToI32Vertex>());
    EXPECT_EQ(untag->GetInput(0), taggedPhi);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, PruningPredecessorsRetainsConservativePhiType)
{
    auto *merge = Diamond(Block());
    auto *number = graph_.GetTaggedConstant(JSTaggedValue(-0.0).GetRawData());
    auto *phi = Phi(merge, Tagged(1), number);
    EXPECT_EQ(phi->GetType(), NodeType::NUMBER);
    phi->RetainInputs({1});
    EXPECT_EQ(phi->GetInputCount(), 1U);
    EXPECT_EQ(phi->GetInput(0), number);
    EXPECT_EQ(phi->GetType(), NodeType::NUMBER);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, CheckedTaggingConversionPreservesItsCheck)
{
    constexpr int checkedValue = 7;
    auto *entry = Block();
    auto *raw = graph_.GetInt32Constant(checkedValue);
    auto *tagged = Unary<CheckedNonNegativeI32ToTaggedIntVertex>(entry, raw, &chunk_, CHECK_BYTECODE_OFFSET);
    EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk_);
    frame.emplace_back(0, Tagged(checkedValue), DeoptTranslationKind::TAGGED);
    tagged->SetEagerDeoptFrameState(std::move(frame));
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, tagged, Tagged(2));
    auto *use = Unary<TaggedIntToI32Vertex>(merge, phi);
    merge->GetControlVertex()->SetInput(0, Unary<I32ToTaggedIntVertex>(merge, use));
    SelectRepresentations();
    ASSERT_TRUE(phi->IsInt32());
    EXPECT_EQ(phi->GetInput(0), raw);
    EXPECT_TRUE(tagged->Is<CheckedNonNegativeI32ToTaggedIntVertex>());
    EXPECT_TRUE(tagged->CanEagerDeopt());
    EXPECT_EQ(tagged->GetBytecodeOffset(), CHECK_BYTECODE_OFFSET);
    EXPECT_NE(std::find(entry->GetVertices().begin(), entry->GetVertices().end(), tagged), entry->GetVertices().end());
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, RawTaggedBitsAreNotNumericPhiInputs)
{
    auto *entry = Block();
    auto *raw = Unary<TaggedToRawI64Vertex>(entry, Tagged(7));
    auto *tagged = Unary<RawI64ToTaggedVertex>(entry, raw);
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, tagged, Tagged(2));
    auto *use = Unary<TaggedIntToI32Vertex>(merge, phi);
    merge->GetControlVertex()->SetInput(0, Unary<I32ToTaggedIntVertex>(merge, use));
    SelectRepresentations();
    EXPECT_TRUE(phi->IsTagged());
    EXPECT_EQ(phi->GetInput(0), tagged);
    EXPECT_TRUE(use->Is<TaggedIntToI32Vertex>());
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, GenericTaggingConversionReusesFloat64Input)
{
    auto *entry = Block();
    auto *raw = Vertex::New<F64AddVertex>(&chunk_, std::array<ValueVertex *, 2> {
        graph_.GetFloat64Constant(1.25), graph_.GetFloat64Constant(2.5)});
    raw->SetOwner(entry);
    entry->AddVertex(raw);
    auto *tagged = Unary<NumberToTaggedVertex>(entry, raw, NumberTaggingKind::DOUBLE);
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, tagged, tagged);
    auto *use = NumberUse(merge, phi);
    merge->GetControlVertex()->SetInput(0, Unary<F64ToTaggedDoubleVertex>(merge, use));
    SelectRepresentations();
    ASSERT_TRUE(phi->IsFloat64());
    EXPECT_EQ(phi->GetInput(0), raw);
    EXPECT_EQ(phi->GetInput(1), raw);
    EXPECT_EQ(phi->GetType(), NodeType::DOUBLE);
    CheckNoReference(tagged);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, GenericIntegerTagPreservesFloat64Truncation)
{
    auto *entry = Block();
    auto *raw = graph_.GetFloat64Constant(3.5);
    auto *tagged = Unary<NumberToTaggedVertex>(entry, raw, NumberTaggingKind::INT32);
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, tagged, tagged);
    auto *use = Unary<TaggedIntToI32Vertex>(merge, phi);
    merge->GetControlVertex()->SetInput(0, Unary<I32ToTaggedIntVertex>(merge, use));
    SelectRepresentations();
    ASSERT_TRUE(phi->IsInt32());
    for (uint32_t i = 0; i < phi->GetInputCount(); ++i) {
        auto *input = phi->GetInput(i);
        ASSERT_TRUE(input->Is<DoubleToInt32Vertex>());
        EXPECT_EQ(input->GetInput(0), raw);
    }
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, GenericIntegerTagPreservesTruncationForFloat64Use)
{
    auto *entry = Block();
    auto *raw = graph_.GetFloat64Constant(3.5);
    auto *tagged = Unary<NumberToTaggedVertex>(entry, raw, NumberTaggingKind::INT32);
    auto *merge = Diamond(entry);
    auto *number = graph_.GetTaggedConstant(JSTaggedValue(1.5).GetRawData());
    auto *phi = Phi(merge, tagged, number);
    auto *use = NumberUse(merge, phi);
    merge->GetControlVertex()->SetInput(0, Unary<F64ToTaggedDoubleVertex>(merge, use));
    SelectRepresentations();
    ASSERT_TRUE(phi->IsFloat64());
    auto *widened = phi->GetInput(0);
    ASSERT_TRUE(widened->Is<I32ToF64Vertex>());
    auto *truncated = widened->GetInput(0);
    ASSERT_TRUE(truncated->Is<DoubleToInt32Vertex>());
    EXPECT_EQ(truncated->GetInput(0), raw);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, GenericDoubleTagDoesNotEraseIntegerCheck)
{
    auto *entry = Block();
    auto *tagged = Unary<NumberToTaggedVertex>(entry, graph_.GetInt32Constant(3), NumberTaggingKind::DOUBLE);
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, tagged, tagged);
    phi->MaybeRecordUseReprHint(UseRepresentation::INT32);
    auto *checked = Unary<CheckedTaggedIntToI32Vertex>(merge, phi, &chunk_, CHECK_BYTECODE_OFFSET);
    EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk_);
    frame.emplace_back(0, phi, DeoptTranslationKind::TAGGED);
    checked->SetEagerDeoptFrameState(std::move(frame));
    merge->GetControlVertex()->SetInput(0, Unary<I32ToTaggedIntVertex>(merge, checked));
    SelectRepresentations();
    ASSERT_TRUE(phi->IsInt32());
    ASSERT_TRUE(merge->GetControlVertex()->Is<DeoptVertex>());
    EXPECT_EQ(merge->GetControlVertex()->Cast<DeoptVertex>()->GetDeoptType(), kungfu::DeoptType::NOTINT1);
    CheckNoReference(checked);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, DoubleConstantDoesNotBecomeInt32Candidate)
{
    auto *merge = Diamond(Block());
    auto *number = graph_.GetTaggedConstant(JSTaggedValue(1.0).GetRawData());
    auto *phi = Phi(merge, number, Tagged(2));
    Unary<TaggedIntToI32Vertex>(merge, phi);
    SelectRepresentations();
    // A tagged double constant retains its Float64 input classification even when integral.
    EXPECT_TRUE(phi->IsTagged());
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, Float64ConstantsPreserveNaNAndSignedZero)
{
    ValueVertex *one = graph_.GetFloat64Constant(1.0);
    ValueVertex *zero = graph_.GetFloat64Constant(0.0);
    ValueVertex *negativeZero = graph_.GetFloat64Constant(-0.0);
    double nan = std::numeric_limits<double>::quiet_NaN();
    ValueVertex *notANumber = graph_.GetFloat64Constant(nan);
    EXPECT_NE(zero, negativeZero);
    EXPECT_NE(one, notANumber);
    EXPECT_EQ(notANumber, graph_.GetFloat64Constant(nan));
    EXPECT_TRUE(std::signbit(negativeZero->Cast<Float64ConstantVertex>()->GetValue()));
    EXPECT_TRUE(std::isnan(notANumber->Cast<Float64ConstantVertex>()->GetValue()));

    auto *merge = Diamond(Block());
    auto *phi = Phi(merge, graph_.GetTaggedConstant(JSTaggedValue(nan).GetRawData()), Tagged(0));
    NumberUse(merge, phi);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsFloat64());
    EXPECT_TRUE(std::isnan(phi->GetInput(0)->Cast<Float64ConstantVertex>()->GetValue()));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, PreserveInt64ArgumentCountTagging)
{
    BB *block = Block();
    auto *argc = Vertex::New<ActualArgcVertex>(&chunk_, size_t(0));
    argc->SetOwner(block);
    block->AddVertex(argc);
    auto *tagged = Unary<I32ToTaggedIntVertex>(block, argc);
    auto *call = LazyFrame(block, tagged);
    SelectRepresentations();
    EXPECT_EQ(call->GetDeoptFrameValue(0), tagged);
    EXPECT_EQ(call->GetDeoptValueKind(0), DeoptTranslationKind::TAGGED);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, UnpeeledMutuallyDependentLoopPhisStayTagged)
{
    auto *entry = Block();
    auto *header = Block();
    auto *body = Block();
    header->SetIsLoopHeader(true);
    header->AddPredecessor(entry);
    header->AddPredecessor(body);
    body->AddPredecessor(header);
    auto *first = Phi(header, Tagged(1), nullptr);
    auto *second = Phi(header, Tagged(2), first);
    first->SetInput(1, second);
    Unary<TaggedIntToI32Vertex>(body, first);
    Unary<TaggedIntToI32Vertex>(body, second);
    auto *jump = Vertex::New<JumpLoopVertex>(&chunk_, size_t(0), &chunk_, header);
    jump->SetOwner(body);
    body->SetControlVertex(jump);
    SelectRepresentations();
    // Do not globally assume that a cycle of tagged input Phis will untag.
    EXPECT_TRUE(first->IsTagged());
    EXPECT_TRUE(second->IsTagged());
}

TEST(ArkSteedSnapshotTableTest, RestoreBranchesAndMergeChangedKeys)
{
    SnapshotTable<int> table;
    auto first = table.NewKey();
    auto second = table.NewKey();
    table.Set(first, 10);
    auto *base = table.Seal();
    table.Set(second, 20);
    table.Set(second, 21);
    auto *left = table.Seal();
    auto shouldNotMerge = [](auto, const std::vector<int> &) { ADD_FAILURE(); return -1; };
    table.StartNewSnapshot({base}, shouldNotMerge);
    EXPECT_EQ(table.Get(second), 0);
    table.Set(second, 30);
    auto *right = table.Seal();
    int calls = 0;
    table.StartNewSnapshot({left, right}, [&](auto key, const std::vector<int> &values) {
        ++calls;
        EXPECT_TRUE(key == second);
        EXPECT_EQ(values[0], 21);
        EXPECT_EQ(values[1], 30);
        return 40;
    });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(table.Get(first), 10);
    EXPECT_EQ(table.Get(second), 40);
    EXPECT_EQ(table.GetPredecessorValue(second, 0), 21);
    EXPECT_EQ(table.Get(second), 40);
    table.Set(second, 41);
    EXPECT_EQ(table.GetPredecessorValue(second, 0), 21);
    EXPECT_EQ(table.GetPredecessorValue(second, 1), 30);
    EXPECT_EQ(table.Get(second), 41);
    table.Set(second, 40);
    table.Set(first, 11);
    auto *merged = table.Seal();
    table.StartNewSnapshot({right}, shouldNotMerge);
    EXPECT_EQ(table.Get(first), 10);
    EXPECT_EQ(table.Get(second), 30);
    table.StartNewSnapshot({merged}, shouldNotMerge);
    EXPECT_EQ(table.Get(first), 11);
    EXPECT_EQ(table.Get(second), 40);
}

TEST(ArkSteedSnapshotTableTest, MissingPathAndEqualValues)
{
    SnapshotTable<int> table;
    auto first = table.NewKey();
    auto *empty = table.Seal();
    table.Set(first, 7);
    auto *left = table.Seal();
    table.StartNewSnapshot({empty}, [](auto, const std::vector<int> &) { return -1; });
    table.Set(first, 7);
    auto *right = table.Seal();
    table.StartNewSnapshot({left, right}, [](auto, const std::vector<int> &) { ADD_FAILURE(); return -1; });
    EXPECT_EQ(table.Get(first), 7);
    table.StartNewSnapshot({left, empty}, [](auto, const std::vector<int> &values) {
        EXPECT_EQ(values[0], 7);
        EXPECT_EQ(values[1], 0);
        return 0;
    });
    EXPECT_EQ(table.Get(first), 0);
}

TEST(ArkSteedSnapshotTableTest, PointerEntryUsesCompactIndices)
{
    SnapshotTable<ValueVertex *> table;
    auto key = table.NewKey();
    EXPECT_EQ(sizeof(*key.entry), sizeof(ValueVertex *) + 2 * sizeof(uint32_t));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, ResolveSelectedPhiAndDeoptAfterStrippingTag)
{
    auto *block = Diamond(Block());
    auto *source = Phi(block, Tagged(1), Tagged(2));
    auto *untag = Unary<TaggedIntToI32Vertex>(block, source);
    auto *tag = Unary<I32ToTaggedIntVertex>(block, untag);
    auto *merge = Diamond(block);
    auto *phi = Phi(merge, tag, Tagged(3));
    Unary<TaggedIntToI32Vertex>(merge, phi);
    auto *deopt = EagerFrame(merge, tag);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsInt32());
    EXPECT_EQ(phi->GetInput(0), source);
    EXPECT_EQ(deopt->GetDeoptFrameValue(0), source);
    EXPECT_EQ(deopt->GetDeoptValueKind(0), DeoptTranslationKind::INT32_TO_TAGGED);
    CheckNoReference(untag);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, ResolvePendingEdgeConversion)
{
    auto *block = Diamond(Block());
    auto *source = Phi(block, Tagged(1), Tagged(2));
    auto *untag = Unary<TaggedIntToI32Vertex>(block, source);
    auto *tag = Unary<I32ToTaggedIntVertex>(block, untag);
    auto *number = DynamicDouble(block);
    auto *merge = Diamond(block);
    auto *phi = Phi(merge, tag, number);
    auto *use = NumberUse(merge, phi);
    Unary<F64ToTaggedDoubleVertex>(merge, use);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsFloat64());
    ASSERT_TRUE(phi->GetInput(0)->Is<I32ToF64Vertex>());
    EXPECT_EQ(phi->GetInput(0)->GetInput(0), source);
    CheckNoReference(untag);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, PreserveUnsignedDeoptTagging)
{
    auto *entry = Block();
    auto *tag = Unary<U32ToTaggedNumberVertex>(entry, graph_.GetInt32Constant(INT32_MIN));
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, tag, Tagged(0));
    auto *use = NumberUse(merge, phi);
    Unary<F64ToTaggedDoubleVertex>(merge, use);
    auto *eager = EagerFrame(merge, tag);
    auto *lazy = LazyFrame(merge, tag);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsFloat64());
    EXPECT_EQ(eager->GetDeoptFrameValue(0), tag);
    EXPECT_EQ(eager->GetDeoptValueKind(0), DeoptTranslationKind::TAGGED);
    EXPECT_EQ(lazy->GetDeoptFrameValue(0), tag);
    EXPECT_EQ(lazy->GetDeoptValueKind(0), DeoptTranslationKind::TAGGED);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, DeoptPreventsLossyTruncationThroughPhiChain)
{
    auto *entry = Block();
    auto *number = DynamicDouble(entry);
    auto *first = Diamond(entry);
    auto *source = Phi(first, number, Tagged(0));
    auto *merge = Diamond(first);
    auto *phi = Phi(merge, source, number);
    auto *use = NumberUse(merge, phi, UseRepresentation::TRUNCATED_INT32);
    Unary<DoubleToInt32Vertex>(merge, use);
    auto *eager = EagerFrame(merge, phi);
    auto *lazy = LazyFrame(merge, phi);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsFloat64());
    EXPECT_TRUE(source->IsFloat64());
    EXPECT_EQ(eager->GetDeoptValueKind(0), DeoptTranslationKind::TAGGED);
    EXPECT_EQ(lazy->GetDeoptValueKind(0), DeoptTranslationKind::TAGGED);
    auto *tag = eager->GetDeoptFrameValue(0);
    ASSERT_TRUE(tag->Is<NumberToTaggedVertex>());
    EXPECT_EQ(tag->GetInput(0), phi);
    EXPECT_EQ(tag->Cast<NumberToTaggedVertex>()->GetKind(), NumberTaggingKind::NUMBER);
    EXPECT_EQ(lazy->GetDeoptFrameValue(0), tag);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, DeoptOfConvertedValueAlsoPreventsTruncation)
{
    auto *entry = Block();
    auto *number = DynamicDouble(entry);
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, number, Tagged(0));
    auto *use = NumberUse(merge, phi, UseRepresentation::TRUNCATED_INT32);
    Unary<DoubleToInt32Vertex>(merge, use);
    auto *deopt = EagerFrame(merge, use);
    deopt->SetDeoptFrameValue(0, use, DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsFloat64());
    EXPECT_EQ(deopt->GetDeoptFrameValue(0), phi);
    EXPECT_EQ(deopt->GetDeoptValueKind(0), DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, TruncatedDeoptResultDoesNotBlockTruncation)
{
    auto *entry = Block();
    auto *number = DynamicDouble(entry);
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, number, Tagged(0));
    auto *use = NumberUse(merge, phi, UseRepresentation::TRUNCATED_INT32);
    auto *trunc = Unary<DoubleToInt32Vertex>(merge, use);
    auto *deopt = EagerFrame(merge, trunc);
    deopt->SetDeoptFrameValue(0, trunc, DeoptTranslationKind::INT32_TO_TAGGED);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsInt32());
    EXPECT_EQ(deopt->GetDeoptFrameValue(0), phi);
    EXPECT_EQ(deopt->GetDeoptValueKind(0), DeoptTranslationKind::INT32_TO_TAGGED);
}
TEST_F(ArkSteedPhiRepresentationSelectorTest, ResolveLoopBackedge)
{
    auto *entry = Block();
    auto *header = Block();
    auto *body = Block();
    header->SetIsLoopHeader(true);
    header->AddPredecessor(entry);
    header->AddPredecessor(body);
    body->AddPredecessor(header);
    auto *enter = Vertex::New<JumpVertex>(&chunk_, size_t(0), header);
    enter->SetOwner(entry);
    entry->SetControlVertex(enter);
    auto *forward = Vertex::New<JumpVertex>(&chunk_, size_t(0), body);
    forward->SetOwner(header);
    header->SetControlVertex(forward);
    auto *backedge = Vertex::New<JumpLoopVertex>(&chunk_, size_t(0), &chunk_, header);
    backedge->SetOwner(body);
    body->SetControlVertex(backedge);
    auto *phi = Phi(header, Tagged(1), Tagged(1));
    auto *untag = Unary<TaggedIntToI32Vertex>(body, phi);
    phi->SetInput(1, Unary<I32ToTaggedIntVertex>(body, untag));
    SelectRepresentations();
    EXPECT_TRUE(phi->IsInt32());
    EXPECT_EQ(phi->GetInput(1), phi);
    CheckNoReference(untag);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, SavedBackedgeFramePreventsTruncation)
{
    auto *entry = Block();
    auto *number = DynamicDouble(entry);
    auto *header = Block();
    auto *body = Block();
    header->SetIsLoopHeader(true);
    header->SetHasPeeledIteration(true);
    header->AddPredecessor(entry);
    header->AddPredecessor(body);
    body->AddPredecessor(header);
    auto *enter = Vertex::New<JumpVertex>(&chunk_, size_t(0), header);
    enter->SetOwner(entry);
    entry->SetControlVertex(enter);
    auto *forward = Vertex::New<JumpVertex>(&chunk_, size_t(0), body);
    forward->SetOwner(header);
    header->SetControlVertex(forward);
    auto *backedge = Vertex::New<JumpLoopVertex>(&chunk_, size_t(0), &chunk_, header);
    backedge->SetOwner(body);
    body->SetControlVertex(backedge);
    auto *phi = Phi(header, number, number);
    phi->SetInput(1, phi);
    auto *use = NumberUse(body, phi, UseRepresentation::TRUNCATED_INT32);
    Unary<DoubleToInt32Vertex>(body, use);
    EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk_);
    frame.emplace_back(0, phi, DeoptTranslationKind::TAGGED);
    backedge->SetPhiUntaggingFrameState(&chunk_, frame, 0);
    SelectRepresentations(true);
    EXPECT_TRUE(phi->IsFloat64());
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, UnpeeledLoopIgnoresUnusedBackedgeFrame)
{
    auto [entry, header, body, exit] = Loop();
    header->SetHasPeeledIteration(false);
    auto *number = DynamicDouble(entry);
    auto *phi = Phi(header, number, number);
    phi->SetInput(1, phi);
    auto *use = NumberUse(body, phi, UseRepresentation::TRUNCATED_INT32);
    Unary<DoubleToInt32Vertex>(body, use);
    EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk_);
    frame.emplace_back(0, phi, DeoptTranslationKind::TAGGED);
    body->GetControlVertex()->Cast<JumpLoopVertex>()->SetPhiUntaggingFrameState(&chunk_, frame, 0);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsInt32());
    EXPECT_FALSE(phi->GetUseReprHints().Contains(UseRepresentation::NON_TRUNCATED));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, UnsafeIntegerUseAfterFloat64LoopPhi)
{
    auto *entry = Block();
    auto *header = Block();
    auto *body = Block();
    auto *exit = Block();
    header->SetIsLoopHeader(true);
    header->AddPredecessor(entry);
    header->AddPredecessor(body);
    body->AddPredecessor(header);
    exit->AddPredecessor(header);
    auto *enter = Vertex::New<JumpVertex>(&chunk_, size_t(0), header);
    enter->SetOwner(entry);
    entry->SetControlVertex(enter);
    auto *branch = Vertex::New<BranchIfTrueVertex>(
        &chunk_, std::array<ValueVertex *, 1> {Tagged(0)}, body, exit);
    branch->SetOwner(header);
    header->SetControlVertex(branch);
    auto *backedge = Vertex::New<JumpLoopVertex>(&chunk_, size_t(0), &chunk_, header);
    backedge->SetOwner(body);
    body->SetControlVertex(backedge);
    auto *phi = Phi(header, Tagged(1), Tagged(2));
    NumberUse(body, phi);
    phi->SetIsUnmergedLoopPhi(false);
    auto *integer = Unary<TaggedIntToI32Vertex>(exit, phi);
    auto *tagged = Unary<I32ToTaggedIntVertex>(exit, integer);
    exit->GetControlVertex()->SetInput(0, tagged);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsFloat64());
    auto *conversion = integer->UnwrapIdentities();
    ASSERT_TRUE(conversion->Is<UnsafeF64ToI32Vertex>());
    EXPECT_EQ(conversion->GetInput(0), phi);
    CheckNoReference(integer);
}
TEST_F(ArkSteedPhiRepresentationSelectorTest, CheckedIntegerUseAfterFloat64LoopPhi)
{
    auto *entry = Block();
    auto *header = Block();
    auto *body = Block();
    auto *exit = Block();
    header->SetIsLoopHeader(true);
    header->AddPredecessor(entry);
    header->AddPredecessor(body);
    body->AddPredecessor(header);
    exit->AddPredecessor(header);
    auto *enter = Vertex::New<JumpVertex>(&chunk_, size_t(0), header);
    enter->SetOwner(entry);
    entry->SetControlVertex(enter);
    auto *branch = Vertex::New<BranchIfTrueVertex>(
        &chunk_, std::array<ValueVertex *, 1> {Tagged(0)}, body, exit);
    branch->SetOwner(header);
    header->SetControlVertex(branch);
    auto *backedge = Vertex::New<JumpLoopVertex>(&chunk_, size_t(0), &chunk_, header);
    backedge->SetOwner(body);
    body->SetControlVertex(backedge);
    auto *phi = Phi(header, Tagged(1), Tagged(2));
    NumberUse(body, phi);
    phi->SetIsUnmergedLoopPhi(false);
    phi->MaybeRecordUseReprHint(UseRepresentation::INT32);
    auto *integer = Unary<CheckedTaggedIntToI32Vertex>(exit, phi, &chunk_, CHECK_BYTECODE_OFFSET);
    EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk_);
    frame.emplace_back(0, phi, DeoptTranslationKind::TAGGED);
    integer->SetEagerDeoptFrameState(std::move(frame));
    auto *tagged = Unary<I32ToTaggedIntVertex>(exit, integer);
    exit->GetControlVertex()->SetInput(0, tagged);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsFloat64());
    auto *conversion = integer->UnwrapIdentities();
    ASSERT_TRUE(conversion->Is<CheckedF64ToI32Vertex>());
    auto *check = conversion->Cast<CheckedF64ToI32Vertex>();
    EXPECT_EQ(check->GetBytecodeOffset(), CHECK_BYTECODE_OFFSET);
    auto *recovery = check->GetDeoptFrameValue(0);
    ASSERT_TRUE(recovery->Is<NumberToTaggedVertex>());
    EXPECT_EQ(recovery->Cast<NumberToTaggedVertex>()->GetKind(), NumberTaggingKind::INT32);
    EXPECT_EQ(recovery->GetInput(0), phi);
    EXPECT_EQ(check->GetDeoptValueKind(0), DeoptTranslationKind::TAGGED);
    EXPECT_EQ(conversion->GetInput(0), phi);
    CheckNoReference(integer);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, Uint32InputWithTaggedUseStillWidens)
{
    auto *entry = Block();
    auto *unsignedValue = Unary<U32ToTaggedNumberVertex>(entry, graph_.GetInt32Constant(-1));
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, unsignedValue, Tagged(1));
    NumberUse(merge, phi);
    phi->MaybeRecordUseReprHint(UseRepresentation::TAGGED);
    merge->GetControlVertex()->SetInput(0, phi);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsFloat64());
    EXPECT_TRUE(phi->GetInput(0)->Is<U32ToF64Vertex>());
    auto *tag = merge->GetControlVertex()->GetInput(0);
    ASSERT_TRUE(tag->Is<NumberToTaggedVertex>());
    EXPECT_EQ(tag->Cast<NumberToTaggedVertex>()->GetKind(), NumberTaggingKind::NUMBER);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, Uint32InputDoesNotTakeFloat64TruncationShortcut)
{
    auto *entry = Block();
    auto *unsignedValue = Unary<U32ToTaggedNumberVertex>(entry, graph_.GetInt32Constant(-1));
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, unsignedValue, Tagged(1));
    auto *number = NumberUse(merge, phi, UseRepresentation::TRUNCATED_INT32);
    Unary<DoubleToInt32Vertex>(merge, number);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsFloat64());
    EXPECT_TRUE(phi->GetInput(0)->Is<U32ToF64Vertex>());
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, Float64DoubleBooleanUsesRawPhi)
{
    auto *entry = Block();
    auto *number = DynamicDouble(entry);
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, number, number);
    NumberUse(merge, phi);
    auto *boolean = Unary<TaggedDoubleToBooleanVertex>(merge, phi, &chunk_, 0, false);
    merge->GetControlVertex()->SetInput(0, boolean);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsFloat64());
    auto *result = boolean->UnwrapIdentities();
    ASSERT_TRUE(result->Is<Float64ToBooleanVertex>());
    EXPECT_EQ(result->GetInput(0), phi);
    EXPECT_FALSE(result->Cast<Float64ToBooleanVertex>()->IsTrueIfNonZero());
    CheckNoReference(boolean);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, Int32DoubleBooleanBecomesUnconditionalDeopt)
{
    auto *entry = Block();
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, Tagged(1), Tagged(2));
    Unary<TaggedIntToI32Vertex>(merge, phi);
    auto *boolean = Unary<TaggedDoubleToBooleanVertex>(merge, phi, &chunk_, 0, true);
    merge->GetControlVertex()->SetInput(0, boolean);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsInt32());
    ASSERT_TRUE(merge->GetControlVertex()->Is<DeoptVertex>());
    EXPECT_EQ(merge->GetControlVertex()->Cast<DeoptVertex>()->GetDeoptType(), kungfu::DeoptType::NOTDOUBLE1);
    CheckNoReference(boolean);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, MixedNumberBooleanKeepsDoubleGuard)
{
    auto *entry = Block();
    auto *number = DynamicDouble(entry);
    auto *merge = Diamond(entry);
    auto *phi = Phi(merge, number, Tagged(1));
    NumberUse(merge, phi);
    auto *boolean = Unary<TaggedDoubleToBooleanVertex>(merge, phi, &chunk_, 0, true);
    merge->GetControlVertex()->SetInput(0, boolean);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsFloat64());
    ASSERT_TRUE(boolean->Is<TaggedDoubleToBooleanVertex>());
    auto *tag = boolean->GetInput(0);
    ASSERT_TRUE(tag->Is<NumberToTaggedVertex>());
    EXPECT_EQ(tag->Cast<NumberToTaggedVertex>()->GetKind(), NumberTaggingKind::NUMBER);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, CommonBooleanCompactsCatchInputs)
{
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    Unary<TaggedIntToI32Vertex>(block, phi);
    auto *handler = Block();
    handler->SetIsExceptionHandler(true);
    auto *boolean = Vertex::New<CallCommonStubVertex>(
        &chunk_, std::array<ValueVertex *, 1>{phi}, CommonStubID::ToBooleanTrue, SideEffectKind::SAFE_CALL);
    boolean->SetOwner(block);
    boolean->LoadCatchBlock(handler, 0);
    block->AddVertex(boolean);
    auto *remaining = LazyFrame(block, phi);
    remaining->LoadCatchBlock(handler, 1);
    auto *catchPhi = Phi(handler, Tagged(9), phi);
    handler->GetControlVertex()->SetInput(0, catchPhi);
    SelectRepresentations();
    EXPECT_TRUE(boolean->UnwrapIdentities()->Is<I32ConditionCheckVertex>());
    EXPECT_EQ(remaining->GetCatchPredecessorIndex(), 0U);
    ASSERT_EQ(catchPhi->GetInputCount(), 1U);
    auto *tag = catchPhi->GetInput(0);
    ASSERT_TRUE(tag->Is<I32ToTaggedIntVertex>());
    EXPECT_EQ(tag->GetInput(0), phi);
    CheckNoReference(boolean);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, CompactsMultipleCatchHandlersIncludingAnEmptyHandler)
{
    constexpr size_t handlerCount = 3;
    constexpr size_t retainedHandlerCount = handlerCount - 1;
    constexpr int removedInputValue = 9;
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    Unary<TaggedIntToI32Vertex>(block, phi);
    std::array<BB *, handlerCount> handlers{Block(), Block(), Block()};
    std::array<CallVertex *, retainedHandlerCount> calls{};
    std::array<PhiVertex *, handlerCount> catchPhis{};
    for (size_t i = 0; i < handlers.size(); ++i) {
        auto *handler = handlers[i];
        handler->SetIsExceptionHandler(true);
        auto *boolean = Vertex::New<CallCommonStubVertex>(
            &chunk_, std::array<ValueVertex *, 1>{phi}, CommonStubID::ToBooleanTrue, SideEffectKind::SAFE_CALL);
        boolean->SetOwner(block);
        boolean->LoadCatchBlock(handler, 0);
        block->AddVertex(boolean);
        if (i < calls.size()) {
            calls[i] = LazyFrame(block, phi);
            calls[i]->LoadCatchBlock(handler, 1);
            catchPhis[i] = Phi(handler, Tagged(removedInputValue), phi);
        } else {
            auto *catchPhi = Vertex::New<PhiVertex>(&chunk_, std::array<ValueVertex *, 1>{phi}, VirtualRegister(0));
            catchPhi->SetOwner(handler);
            handler->AddPhiVertex(catchPhi);
            catchPhis[i] = catchPhi;
        }
        handler->GetControlVertex()->SetInput(0, catchPhis[i]);
    }
    SelectRepresentations();
    for (size_t i = 0; i < calls.size(); ++i) {
        EXPECT_EQ(calls[i]->GetCatchPredecessorIndex(), 0U);
        ASSERT_EQ(catchPhis[i]->GetInputCount(), 1U);
        auto *tag = catchPhis[i]->GetInput(0);
        ASSERT_TRUE(tag->Is<I32ToTaggedIntVertex>());
        EXPECT_EQ(tag->GetInput(0), phi);
    }
    EXPECT_TRUE(handlers.back()->GetPhis().empty());
    EXPECT_EQ(
        handlers.back()->GetControlVertex()->GetInput(0), graph_.GetTaggedConstant(JSTaggedValue::VALUE_UNDEFINED));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, StoredCatchIndicesUpdateEveryPhi)
{
    auto *block = Diamond(Block());
    auto *first = Phi(block, Tagged(1), Tagged(2));
    auto *second = Phi(block, Tagged(3), Tagged(4));
    Unary<TaggedIntToI32Vertex>(block, first);
    Unary<TaggedIntToI32Vertex>(block, second);
    auto *handler = Block();
    handler->SetIsExceptionHandler(true);
    auto *left = LazyFrame(block, first);
    auto *right = LazyFrame(block, second);
    left->LoadCatchBlock(handler, 0);
    right->LoadCatchBlock(handler, 1);
    auto *x = Phi(handler, first, second);
    auto *y = Phi(handler, second, first);
    handler->GetControlVertex()->SetInput(0, x);
    LazyFrame(handler, y);

    SelectRepresentations();

    ASSERT_TRUE(first->IsInt32());
    ASSERT_TRUE(second->IsInt32());
    for (auto *phi : {x, y}) {
        for (uint32_t i = 0; i < phi->GetInputCount(); ++i) {
            ASSERT_TRUE(phi->GetInput(i)->Is<I32ToTaggedIntVertex>());
        }
    }
    EXPECT_EQ(x->GetInput(0)->GetInput(0), first);
    EXPECT_EQ(x->GetInput(1)->GetInput(0), second);
    EXPECT_EQ(y->GetInput(0)->GetInput(0), second);
    EXPECT_EQ(y->GetInput(1)->GetInput(0), first);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, EdgeWideningIsSharedBetweenPhis)
{
    auto *entry = Block();
    auto *integer = Unary<TaggedIntToI32Vertex>(entry, Tagged(7));
    auto *number = DynamicDouble(entry);
    auto *merge = Diamond(entry);
    auto *first = Phi(merge, integer, number);
    auto *second = Phi(merge, integer, number);
    NumberUse(merge, first);
    NumberUse(merge, second);
    LazyFrame(merge, first);
    LazyFrame(merge, second);

    SelectRepresentations();

    ASSERT_TRUE(first->IsFloat64());
    ASSERT_TRUE(second->IsFloat64());
    ASSERT_TRUE(first->GetInput(0)->Is<I32ToF64Vertex>());
    EXPECT_EQ(first->GetInput(0), second->GetInput(0));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, CommonBooleanRemovesOnlyCatchInput)
{
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    Unary<TaggedIntToI32Vertex>(block, phi);
    auto *handler = Block();
    handler->SetIsExceptionHandler(true);
    auto *boolean = Vertex::New<CallCommonStubVertex>(
        &chunk_, std::array<ValueVertex *, 1>{phi}, CommonStubID::ToBooleanFalse, SideEffectKind::SAFE_CALL);
    boolean->SetOwner(block);
    boolean->LoadCatchBlock(handler, 0);
    block->AddVertex(boolean);
    auto *catchPhi = Vertex::New<PhiVertex>(&chunk_, std::array<ValueVertex *, 1> {phi}, VirtualRegister(0));
    catchPhi->SetOwner(handler);
    handler->AddPhiVertex(catchPhi);
    handler->GetControlVertex()->SetInput(0, catchPhi);
    SelectRepresentations();
    EXPECT_TRUE(boolean->UnwrapIdentities()->Is<I32ConditionCheckVertex>());
    EXPECT_TRUE(handler->GetPhis().empty());
    EXPECT_EQ(handler->GetControlVertex()->GetInput(0),
              graph_.GetTaggedConstant(JSTaggedValue::VALUE_UNDEFINED));
    CheckNoReference(boolean);
    CheckNoReference(catchPhi);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, NumberToStringKeepsUnboxedIntegerInput)
{
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    phi->MaybeRecordUseReprHint(UseRepresentation::TAGGED_FOR_NUMBER_TO_STRING);
    auto *call = Unary<NumberToStringVertex>(block, phi);
    block->GetControlVertex()->SetInput(0, call);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsInt32());
    EXPECT_EQ(call->GetInput(0), phi);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, NumberToStringKeepsUnboxedDoubleInput)
{
    auto *entry = Block();
    auto *number = DynamicDouble(entry);
    auto *block = Diamond(entry);
    auto *phi = Phi(block, number, Tagged(1));
    phi->MaybeRecordUseReprHint(UseRepresentation::TAGGED_FOR_NUMBER_TO_STRING);
    auto *call = Unary<NumberToStringVertex>(block, phi);
    block->GetControlVertex()->SetInput(0, call);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsFloat64());
    EXPECT_EQ(call->GetInput(0), phi);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, PeeledNumberBackedgeUsesExactCheck)
{
    auto *entry = Block();
    auto *header = Block();
    auto *body = Diamond(header);
    auto *unknown = graph_.GetTaggedConstant(JSTaggedValue::VALUE_UNDEFINED);
    auto *backedgePhi = Phi(body, Tagged(1), unknown);
    header->SetIsLoopHeader(true);
    header->SetHasPeeledIteration(true);
    header->AddPredecessor(entry);
    header->AddPredecessor(body);
    auto *enter = Vertex::New<JumpVertex>(&chunk_, size_t(0), header);
    enter->SetOwner(entry);
    entry->SetControlVertex(enter);
    auto *jump = Vertex::New<JumpLoopVertex>(&chunk_, size_t(0), &chunk_, header);
    jump->SetOwner(body);
    body->SetControlVertex(jump);
    auto *phi = Phi(header, Tagged(1), backedgePhi);
    Unary<TaggedIntToI32Vertex>(header, phi);
    EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk_);
    frame.emplace_back(0, backedgePhi, DeoptTranslationKind::TAGGED);
    jump->SetPhiUntaggingFrameState(&chunk_, frame, BACKEDGE_CHECKPOINT_OFFSET);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsInt32());
    ASSERT_TRUE(backedgePhi->IsTagged());
    auto *check = phi->GetInput(1);
    ASSERT_TRUE(check->Is<CheckedF64ToI32Vertex>());
    EXPECT_EQ(check->Cast<CheckedF64ToI32Vertex>()->GetBytecodeOffset(), BACKEDGE_CHECKPOINT_OFFSET);
    auto *number = check->GetInput(0);
    ASSERT_TRUE(number->Is<CheckedNumberToF64Vertex>());
    EXPECT_EQ(number->GetInput(0), backedgePhi);
    EXPECT_EQ(check->Cast<CheckedF64ToI32Vertex>()->GetDeoptFrameValue(0), backedgePhi);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, UnpeeledBackedgePreservesOriginalCheck)
{
    auto *entry = Block();
    auto *header = Block();
    auto *body = Diamond(header);
    auto *unknown = graph_.GetTaggedConstant(JSTaggedValue::VALUE_UNDEFINED);
    auto *backedgePhi = Phi(body, Tagged(1), unknown);
    header->SetIsLoopHeader(true);
    header->SetHasPeeledIteration(false);
    header->AddPredecessor(entry);
    header->AddPredecessor(body);
    auto *enter = Vertex::New<JumpVertex>(&chunk_, size_t(0), header);
    enter->SetOwner(entry);
    entry->SetControlVertex(enter);
    auto *jump = Vertex::New<JumpLoopVertex>(&chunk_, size_t(0), &chunk_, header);
    jump->SetOwner(body);
    body->SetControlVertex(jump);
    auto *phi = Phi(header, Tagged(1), backedgePhi);
    phi->MaybeRecordUseReprHint(UseRepresentation::INT32);
    auto *originalCheck = Unary<CheckedTaggedIntToI32Vertex>(header, phi, &chunk_, CHECK_BYTECODE_OFFSET);
    EagerDeoptimizableMixin::EagerDeoptFrameState originalFrame(&chunk_);
    originalFrame.emplace_back(0, phi, DeoptTranslationKind::TAGGED);
    originalCheck->SetEagerDeoptFrameState(std::move(originalFrame));
    EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk_);
    frame.emplace_back(0, backedgePhi, DeoptTranslationKind::TAGGED);
    jump->SetPhiUntaggingFrameState(&chunk_, frame, BACKEDGE_CHECKPOINT_OFFSET);
    SelectRepresentations();
    ASSERT_TRUE(phi->IsTagged());
    EXPECT_EQ(phi->GetInput(1), backedgePhi);
    EXPECT_EQ(originalCheck->GetInput(0), phi);
    EXPECT_EQ(originalCheck->GetBytecodeOffset(), CHECK_BYTECODE_OFFSET);
    EXPECT_EQ(originalCheck->GetDeoptFrameValue(0), phi);
    EXPECT_TRUE(originalCheck->Is<CheckedTaggedIntToI32Vertex>());
    for (NonControlVertex *vertex : body->GetVertices()) {
        EXPECT_FALSE(vertex->Is<CheckedNumberToF64Vertex>());
        EXPECT_FALSE(vertex->Is<CheckedF64ToI32Vertex>());
    }
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, ReturnPreventsLossyTruncation)
{
    auto *entry = Block();
    auto *number = DynamicDouble(entry);
    auto *block = Diamond(entry);
    auto *phi = Phi(block, number, Tagged(1));
    auto *use = NumberUse(block, phi, UseRepresentation::TRUNCATED_INT32);
    Unary<DoubleToInt32Vertex>(block, use);
    block->GetControlVertex()->SetInput(0, phi);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsFloat64());
    auto *tag = block->GetControlVertex()->GetInput(0);
    ASSERT_TRUE(tag->Is<NumberToTaggedVertex>());
    EXPECT_EQ(tag->Cast<NumberToTaggedVertex>()->GetKind(), NumberTaggingKind::NUMBER);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, HoistUnknownEntryToExactInt32)
{
    auto [entry, header, body, exit] = Loop();
    auto *input = UnknownParameter(entry);
    CheckpointEntry(entry, input);
    auto *phi = Phi(header, input, Tagged(2));
    Unary<TaggedIntToI32Vertex>(body, phi);
    exit->GetControlVertex()->SetInput(0, phi);
    SelectRepresentations(true);
    ASSERT_TRUE(phi->IsInt32());
    auto *integer = phi->GetInput(0);
    ASSERT_TRUE(integer->Is<CheckedF64ToI32Vertex>());
    EXPECT_EQ(integer->GetOwner(), entry);
    auto *number = integer->GetInput(0);
    ASSERT_TRUE(number->Is<CheckedNumberToF64Vertex>());
    EXPECT_EQ(number->GetInput(0), input);
    EXPECT_EQ(number->GetOwner(), entry);
    auto *check = integer->Cast<CheckedF64ToI32Vertex>();
    EXPECT_EQ(check->GetBytecodeOffset(), ENTRY_CHECKPOINT_OFFSET);
    EXPECT_EQ(check->GetDeoptFrameValue(0), input);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, HoistUnknownEntryToFloat64)
{
    auto [entry, header, body, exit] = Loop();
    auto *input = UnknownParameter(entry);
    CheckpointEntry(entry, input);
    auto *phi = Phi(header, input, DynamicDouble(body));
    NumberUse(body, phi);
    exit->GetControlVertex()->SetInput(0, phi);
    SelectRepresentations(true);
    ASSERT_TRUE(phi->IsFloat64());
    auto *check = phi->GetInput(0);
    ASSERT_TRUE(check->Is<CheckedNumberToF64Vertex>());
    EXPECT_EQ(check->GetInput(0), input);
    EXPECT_EQ(check->GetOwner(), entry);
    EXPECT_EQ(check->Cast<CheckedNumberToF64Vertex>()->GetDeoptFrameValue(0), input);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, HoistUnknownEntryWithTruncatingUse)
{
    auto [entry, header, body, exit] = Loop();
    auto *input = UnknownParameter(entry);
    CheckpointEntry(entry, input);
    auto *phi = Phi(header, input, DynamicDouble(body));
    auto *number = NumberUse(body, phi, UseRepresentation::TRUNCATED_INT32);
    auto *truncated = Unary<DoubleToInt32Vertex>(body, number);
    auto *call = LazyFrame(body, truncated);
    call->SetDeoptFrameValue(0, truncated, DeoptTranslationKind::INT32_TO_TAGGED);
    SelectRepresentations(true);
    ASSERT_TRUE(phi->IsInt32());
    auto *convert = phi->GetInput(0);
    ASSERT_TRUE(convert->Is<DoubleToInt32Vertex>());
    EXPECT_EQ(convert->GetOwner(), entry);
    auto *check = convert->GetInput(0);
    ASSERT_TRUE(check->Is<CheckedNumberToF64Vertex>());
    EXPECT_EQ(check->GetInput(0), input);
    EXPECT_EQ(check->Cast<CheckedNumberToF64Vertex>()->GetDeoptFrameValue(0), input);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, UnknownEntryWithoutCheckpointStaysTagged)
{
    auto [entry, header, body, exit] = Loop();
    auto *input = UnknownParameter(entry);
    auto *phi = Phi(header, input, Tagged(2));
    Unary<TaggedIntToI32Vertex>(body, phi);
    exit->GetControlVertex()->SetInput(0, phi);
    SelectRepresentations(true);
    EXPECT_TRUE(phi->IsTagged());
    EXPECT_EQ(phi->GetInput(0), input);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, EntrySpeculationNeedsNumericEvidence)
{
    auto [entry, header, body, exit] = Loop();
    auto *input = UnknownParameter(entry);
    CheckpointEntry(entry, input);
    auto *phi = Phi(header, input, input);
    phi->SetInput(1, phi);
    Unary<TaggedIntToI32Vertex>(body, phi);
    exit->GetControlVertex()->SetInput(0, phi);
    SelectRepresentations(true);
    EXPECT_TRUE(phi->IsTagged());
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, EntrySpeculationIsDisabledByDefault)
{
    EXPECT_FALSE(graph_.IsSpeculativePhiHoistingEnabled());
    auto [entry, header, body, exit] = Loop();
    auto *input = UnknownParameter(entry);
    CheckpointEntry(entry, input);
    auto *phi = Phi(header, input, Tagged(2));
    Unary<TaggedIntToI32Vertex>(body, phi);
    exit->GetControlVertex()->SetInput(0, phi);
    SelectRepresentations();
    EXPECT_TRUE(phi->IsTagged());
    EXPECT_EQ(phi->GetInput(0), input);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, LargeMergeUsesTransientInputKinds)
{
    constexpr uint32_t inputCount = 10;
    std::array<BB *, inputCount> predecessors;
    for (BB *&predecessor : predecessors) {
        predecessor = Block();
    }
    auto *merge = Block();
    auto *phi = PhiVertex::New(&chunk_, inputCount, VirtualRegister(0));
    phi->SetOwner(merge);
    phi->SetType(NodeType::INT);
    merge->AddPhiVertex(phi);
    for (uint32_t i = 0; i < inputCount; ++i) {
        BB *predecessor = predecessors[i];
        auto *jump = Vertex::New<JumpVertex>(&chunk_, size_t(0), merge);
        jump->SetOwner(predecessor);
        predecessor->SetControlVertex(jump);
        merge->AddPredecessor(predecessor);
        phi->SetInput(i, Tagged(i));
    }
    auto *use = Unary<TaggedIntToI32Vertex>(merge, phi);
    merge->GetControlVertex()->SetInput(0, Unary<I32ToTaggedIntVertex>(merge, use));
    SelectRepresentations();
    ASSERT_TRUE(phi->IsInt32());
    for (uint32_t i = 0; i < inputCount; ++i) {
        EXPECT_TRUE(phi->GetInput(i)->Is<Int32ConstantVertex>());
    }
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, UnconditionalDeoptRepairsMergeAndCatchInputs)
{
    constexpr uint32_t guardBytecodeOffset = 19;
    constexpr int doomedValue = 7;
    constexpr int survivingValue = 9;
    auto *prefix = Diamond(Block());
    auto *numeric = Phi(prefix, Tagged(1), Tagged(2));
    Unary<TaggedIntToI32Vertex>(prefix, numeric);
    auto *join = Diamond(prefix);
    auto *left = join->GetPredecessor(0);
    auto *right = join->GetPredecessor(1);
    auto *guard = Unary<TaggedDoubleToBooleanVertex>(left, numeric, &chunk_, guardBytecodeOffset, true);
    auto *doomedCall = LazyFrame(left, Tagged(doomedValue));
    auto *remainingCall = LazyFrame(right, Tagged(survivingValue));
    auto *handler = Block();
    handler->SetIsExceptionHandler(true);
    doomedCall->LoadCatchBlock(handler, 0);
    remainingCall->LoadCatchBlock(handler, 1);
    auto *caught = Phi(handler, Tagged(doomedValue), Tagged(survivingValue));
    handler->GetControlVertex()->SetInput(0, caught);
    auto *undefined = graph_.GetTaggedConstant(JSTaggedValue::VALUE_UNDEFINED);
    auto *merged = Phi(join, Tagged(doomedValue), undefined);
    join->GetControlVertex()->SetInput(0, merged);
    SelectRepresentations();
    ASSERT_TRUE(left->GetControlVertex()->Is<DeoptVertex>());
    EXPECT_EQ(left->GetControlVertex()->Cast<DeoptVertex>()->GetBytecodeOffset(), guardBytecodeOffset);
    ASSERT_EQ(join->PredecessorCount(), 1U);
    EXPECT_EQ(join->GetPredecessor(0), right);
    EXPECT_EQ(right->GetPredecessorId(), 0U);
    EXPECT_TRUE(join->GetPhis().empty());
    EXPECT_EQ(join->GetControlVertex()->GetInput(0), undefined);
    ASSERT_EQ(caught->GetInputCount(), 1U);
    EXPECT_EQ(caught->GetInput(0), Tagged(survivingValue));
    EXPECT_EQ(remainingCall->GetCatchPredecessorIndex(), 0U);
    CheckNoReference(guard);
    CheckNoReference(doomedCall);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, UnconditionalDeoptRemovesBackedge)
{
    constexpr uint32_t guardBytecodeOffset = 7;
    auto [entry, header, body, exit] = Loop();
    auto *phi = Phi(header, Tagged(1), Tagged(2));
    Unary<TaggedIntToI32Vertex>(body, phi);
    auto *guard = Unary<TaggedDoubleToBooleanVertex>(body, phi, &chunk_, guardBytecodeOffset, true);
    auto *deadCall = LazyFrame(body, phi);
    exit->GetControlVertex()->SetInput(0, phi);
    SelectRepresentations();
    EXPECT_FALSE(header->IsLoopHeader());
    ASSERT_EQ(header->PredecessorCount(), 1U);
    EXPECT_EQ(header->GetPredecessor(0), entry);
    EXPECT_TRUE(header->GetPhis().empty());
    EXPECT_TRUE(body->GetControlVertex()->Is<DeoptVertex>());
    CheckNoReference(guard);
    CheckNoReference(deadCall);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, UnconditionalDeoptRemovesUnreachableTail)
{
    constexpr uint32_t guardBytecodeOffset = 7;
    constexpr int unreachableValue = 9;
    constexpr size_t prefixBlockCount = 4;
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    Unary<TaggedIntToI32Vertex>(block, phi);
    Unary<TaggedDoubleToBooleanVertex>(block, phi, &chunk_, guardBytecodeOffset, true);
    auto *tail = Diamond(block);
    LazyFrame(tail, Tagged(unreachableValue));
    SelectRepresentations();
    EXPECT_EQ(graph_.NumBlocks(), prefixBlockCount);
    EXPECT_EQ(graph_.LastBlock(), block);
    EXPECT_TRUE(block->GetControlVertex()->Is<DeoptVertex>());
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, DeadPureConversionChainIsRemoved)
{
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    auto *untag = Unary<TaggedIntToI32Vertex>(block, phi);
    auto *widen = Unary<I32ToF64Vertex>(block, untag);
    auto *truncate = Unary<DoubleToInt32Vertex>(block, widen);
    auto *tag = Unary<I32ToTaggedIntVertex>(block, truncate);
    SelectRepresentations();
    CheckNoReference(untag);
    CheckNoReference(widen);
    CheckNoReference(truncate);
    CheckNoReference(tag);
    EXPECT_TRUE(block->GetPhis().empty());
    EXPECT_TRUE(block->GetVertices().empty());
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, RemovesMachineTruncationAfterInt32Widening)
{
    auto *block = Diamond(Block());
    auto *phi = Phi(block, Tagged(1), Tagged(2));
    auto *untag = Unary<TaggedIntToI32Vertex>(block, phi);
    auto *widen = Unary<I32ToF64Vertex>(block, untag);
    auto *truncate = Unary<UnsafeF64ToI32Vertex>(block, widen);
    auto *tag = Unary<I32ToTaggedIntVertex>(block, truncate);
    block->GetControlVertex()->SetInput(0, tag);

    SelectRepresentations();

    ASSERT_TRUE(phi->IsInt32());
    EXPECT_EQ(tag->GetInput(0), phi);
    CheckNoReference(untag);
    CheckNoReference(widen);
    CheckNoReference(truncate);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, CoreDceRetainsUnauditedDivision)
{
    auto *block = Block();
    auto *divide = Vertex::New<I32DivVertex>(
        &chunk_, std::array<ValueVertex *, 2> {graph_.GetInt32Constant(11), graph_.GetInt32Constant(2)});
    divide->SetOwner(block);
    block->AddVertex(divide);

    SelectRepresentations();

    ASSERT_EQ(block->GetVertices().size(), 1U);
    EXPECT_EQ(block->GetVertices().front(), divide);
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, RepeatedDeoptSourcesPreserveOriginalPhi)
{
    constexpr uint32_t frameValueCount = 4096;
    auto [entry, header, body, exit] = Loop();
    auto *number = DynamicDouble(entry);
    auto *phi = Phi(header, number, number);
    phi->SetInput(1, phi);
    auto *use = NumberUse(body, phi, UseRepresentation::TRUNCATED_INT32);
    Unary<DoubleToInt32Vertex>(body, use);
    auto *call = Vertex::New<CallVertex>(&chunk_, std::array<ValueVertex *, 3>{Tagged(0), Tagged(0), Tagged(0)}, 0);
    auto *frame = chunk_.New<LazyDeoptimizableMixin::LazyDeoptFrameState>(&chunk_);
    for (uint32_t i = 0; i < frameValueCount; ++i) {
        frame->emplace_back(i, phi, DeoptTranslationKind::TAGGED);
    }
    call->SetLazyDeoptFrameState(frame, 0);
    call->SetOwner(body);
    body->AddVertex(call);

    SelectRepresentations();

    EXPECT_TRUE(phi->IsFloat64());
    EXPECT_TRUE(phi->GetUseReprHints().Contains(UseRepresentation::NON_TRUNCATED));
    EXPECT_EQ(call->GetDeoptFrameValueCount(), frameValueCount);
    EXPECT_EQ(call->GetDeoptFrameValue(0), call->GetDeoptFrameValue(frameValueCount - 1));
}

TEST_F(ArkSteedPhiRepresentationSelectorTest, DeadPhiCycleIsRemovedButDeoptValuesSurvive)
{
    auto [entry, header, body, exit] = Loop();
    auto *dead = Phi(header, Tagged(1), Tagged(2));
    dead->SetInput(1, dead);
    auto *live = Phi(header, Tagged(3), Tagged(4));
    Unary<TaggedIntToI32Vertex>(body, live);
    auto *call = LazyFrame(body, live);
    SelectRepresentations();
    EXPECT_EQ(header->GetPhis().size(), 1U);
    EXPECT_EQ(header->GetPhis().front(), live);
    EXPECT_EQ(call->GetDeoptFrameValue(0), live);
    EXPECT_EQ(call->GetDeoptValueKind(0), DeoptTranslationKind::INT32_TO_TAGGED);
    CheckNoReference(dead);
}
}  // namespace panda::ecmascript::arksteed
