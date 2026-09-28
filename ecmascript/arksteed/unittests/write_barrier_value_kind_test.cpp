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

#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_write_barrier_value_kind_pass.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/tests/test_helper.h"

#include <cstdint>
#include <initializer_list>

namespace panda::ecmascript::arksteed {

// Unit domain for matrix row G4.3u (write-barrier value kind pass). The cases use
// graph fixture A: a real NativeAreaAllocator/Chunk/Graph with real IR vertices and
// no EcmaVM. They pin the domain contract C7 of the wiki page
// `arksteed-cpp-unit-test-domains`:
//
//   * `ClassifyDirectWriteBarrierValueKind` classifies every direct value kind:
//     NonHeap only for scalars, HeapObject for heap tagged constants and
//     HeapConstantVertex, Unknown for everything else and for nullptr.
//   * `WriteBarrierValueKindPass::Run()` rewrites a NonHeap store (field/element)
//     into the variant without a barrier, keeps the barrier for HeapObject/Unknown,
//     records the resolved value kind on the kept barrier store, and removes a
//     SetValueWithBarrier whose value is NonHeap.
//   * Phi value kinds follow the all-inputs algebra: a Phi is classified only when
//     every input can be the target kind, and a Phi that mixes scalar and tagged
//     inputs stays Unknown.
//
// Independent oracle (D2): the tagged bit patterns used below come from the
// nan-boxing layout documented in `ecmascript/js_tagged_value_internals.h`, not from
// the classifier implementation; the store expectations come from the pass contract
// above, not from the production constants.
class ArkSteedWriteBarrierValueKindTest : public testing::Test {
protected:
    using Kind = ArkSteedWriteBarrierValueKind;

    // A heap object keeps TAG_HEAPOBJECT_MASK (TAG_MARK | TAG_SPECIAL | TAG_BOOLEAN,
    // 0xFFFF000000000006) clear. 0x1000 has all of those bits clear and is neither int,
    // double, special nor boolean, so the classifier sees a heap object by tag bits
    // alone; no object is dereferenced.
    static constexpr uint64_t kHeapObjectTaggedRaw = 0x1000ULL;

    NativeAreaAllocator nativeArea_;
    Chunk chunk_ {&nativeArea_};
    Graph graph_ {&chunk_};

    ValueVertex *IntScalar()
    {
        return graph_.GetTaggedConstant(JSTaggedValue(7).GetRawData());
    }

    ValueVertex *DoubleScalar()
    {
        return graph_.GetTaggedConstant(JSTaggedValue(1.5).GetRawData());
    }

    ValueVertex *TrueScalar()
    {
        return graph_.GetTaggedConstant(JSTaggedValue::True().GetRawData());
    }

    ValueVertex *FalseScalar()
    {
        return graph_.GetTaggedConstant(JSTaggedValue::False().GetRawData());
    }

    ValueVertex *UndefinedScalar()
    {
        return graph_.GetTaggedConstant(JSTaggedValue::Undefined().GetRawData());
    }

    ValueVertex *NullScalar()
    {
        return graph_.GetTaggedConstant(JSTaggedValue::Null().GetRawData());
    }

    ValueVertex *HoleScalar()
    {
        return graph_.GetTaggedConstant(JSTaggedValue::Hole().GetRawData());
    }

    ValueVertex *HeapTaggedValue()
    {
        return graph_.GetTaggedConstant(kHeapObjectTaggedRaw);
    }

    ValueVertex *HeapConstant()
    {
        return graph_.GetHeapConstant(0, 0);
    }

    ValueVertex *Receiver()
    {
        return graph_.GetHeapConstant(1, 0);
    }

    ValueVertex *Glue()
    {
        return graph_.GetInt64Constant(0);
    }

    ValueVertex *Int32Constant()
    {
        return graph_.GetInt32Constant(3);
    }

    ValueVertex *I32ToTagged()
    {
        std::initializer_list<ValueVertex *> inputs {graph_.GetInt32Constant(1)};
        return Vertex::New<I32ToTaggedIntVertex>(&chunk_, inputs);
    }

    ValueVertex *CheckedNonNegativeI32ToTagged()
    {
        std::initializer_list<ValueVertex *> inputs {graph_.GetInt32Constant(1)};
        return Vertex::New<CheckedNonNegativeI32ToTaggedIntVertex>(&chunk_, inputs, &chunk_, 0U);
    }

    ValueVertex *F64ToTagged()
    {
        std::initializer_list<ValueVertex *> inputs {graph_.GetFloat64Constant(1.0)};
        return Vertex::New<F64ToTaggedDoubleVertex>(&chunk_, inputs);
    }

    PhiVertex *NewPhi(uint32_t inputCount)
    {
        return Vertex::New<PhiVertex>(&chunk_, static_cast<size_t>(inputCount),
                                      VirtualRegister(static_cast<uint32_t>(inputCount)));
    }

    // The owner is set here exactly as the graph builder's NewVertex does, so a
    // rewritten store carries the same owner block as the original barrier store.
    StoreTaggedFieldWithBarrierVertex *NewFieldStore(BB *owner, ValueVertex *value, int32_t offset)
    {
        std::initializer_list<ValueVertex *> inputs {Glue(), Receiver(), value};
        auto *store = Vertex::New<StoreTaggedFieldWithBarrierVertex>(&chunk_, inputs, offset);
        store->SetOwner(owner);
        return store;
    }

    StoreTaggedElementWithBarrierVertex *NewElementStore(BB *owner, ValueVertex *value)
    {
        std::initializer_list<ValueVertex *> inputs {
            Glue(), Receiver(), graph_.GetInt32Constant(0), value};
        auto *store = Vertex::New<StoreTaggedElementWithBarrierVertex>(&chunk_, inputs);
        store->SetOwner(owner);
        return store;
    }

    SetValueWithBarrierVertex *NewSetValueWithBarrier(BB *owner, ValueVertex *value, int32_t offset)
    {
        std::initializer_list<ValueVertex *> inputs {Glue(), Receiver(), value};
        auto *store = Vertex::New<SetValueWithBarrierVertex>(&chunk_, inputs, offset);
        store->SetOwner(owner);
        return store;
    }

    void RunPass()
    {
        WriteBarrierValueKindPass pass(&graph_);
        pass.Run();
    }
};

/**
 * @tc.name: ArkSteedWriteBarrierValueKindTest.DirectValueKindBranches
 * @tc.desc: every branch of ClassifyDirectWriteBarrierValueKind returns the documented kind
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedWriteBarrierValueKindTest, DirectValueKindBranches)
{
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(nullptr), Kind::Unknown)
        << "nullptr must not be classified as a barrier-elidable value";

    // Scalars: int, double, boolean and the special values are all NonHeap.
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(IntScalar()), Kind::NonHeap);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(DoubleScalar()), Kind::NonHeap);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(TrueScalar()), Kind::NonHeap);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(FalseScalar()), Kind::NonHeap);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(UndefinedScalar()), Kind::NonHeap);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(NullScalar()), Kind::NonHeap);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(HoleScalar()), Kind::NonHeap);

    // Tagged conversions that always produce a scalar are NonHeap.
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(I32ToTagged()), Kind::NonHeap);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(CheckedNonNegativeI32ToTagged()), Kind::NonHeap);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(F64ToTagged()), Kind::NonHeap);

    // Heap values keep the barrier: a heap tagged constant and a HeapConstantVertex.
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(HeapTaggedValue()), Kind::HeapObject);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(HeapConstant()), Kind::HeapObject);
    EXPECT_NE(ClassifyDirectWriteBarrierValueKind(HeapTaggedValue()), Kind::NonHeap);
    EXPECT_NE(ClassifyDirectWriteBarrierValueKind(HeapConstant()), Kind::NonHeap);

    // Values without a known representation are Unknown, never NonHeap.
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(Int32Constant()), Kind::Unknown);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(graph_.GetInt64Constant(0)), Kind::Unknown);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(graph_.GetFloat64Constant(2.0)), Kind::Unknown);
}

/**
 * @tc.name: ArkSteedWriteBarrierValueKindTest.RunRewritesNonHeapStoresOnly
 * @tc.desc: Run() drops the barrier only for NonHeap field/element stores and records the kept kinds
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedWriteBarrierValueKindTest, RunRewritesNonHeapStoresOnly)
{
    ValueVertex *object = Receiver();
    ValueVertex *scalar = IntScalar();
    ValueVertex *heap = HeapConstant();
    ValueVertex *unknown = Int32Constant();
    ValueVertex *index = graph_.GetInt32Constant(0);

    BB *block = BB::New(&chunk_);
    StoreTaggedFieldWithBarrierVertex *fieldScalar = NewFieldStore(block, scalar, 16);
    StoreTaggedFieldWithBarrierVertex *fieldHeap = NewFieldStore(block, heap, 20);
    StoreTaggedFieldWithBarrierVertex *fieldUnknown = NewFieldStore(block, unknown, 24);
    StoreTaggedElementWithBarrierVertex *elementScalar = NewElementStore(block, scalar);
    StoreTaggedElementWithBarrierVertex *elementHeap = NewElementStore(block, heap);
    StoreTaggedElementWithBarrierVertex *elementUnknown = NewElementStore(block, unknown);
    block->AddVertex(fieldScalar);
    block->AddVertex(fieldHeap);
    block->AddVertex(fieldUnknown);
    block->AddVertex(elementScalar);
    block->AddVertex(elementHeap);
    block->AddVertex(elementUnknown);
    graph_.Add(block);

    const size_t verticesBefore = block->GetVertexCount();
    RunPass();

    // Rewriting a store in place does not change the number or the order of vertices.
    EXPECT_EQ(block->GetVertexCount(), verticesBefore);
    const ChunkVector<NonControlVertex *> &vertices = block->GetVertices();

    // Field store with a scalar: rewritten to the barrier-free variant, same inputs/offset.
    auto *plainField = vertices[0]->TryCast<StoreTaggedFieldVertex>();
    ASSERT_NE(plainField, nullptr) << "NonHeap field store must lose the barrier";
    EXPECT_EQ(plainField->GetOwner(), block);
    EXPECT_EQ(plainField->GetOffset(), 16);
    EXPECT_EQ(plainField->GetInput(StoreTaggedFieldVertex::OBJECT_INDEX), object);
    EXPECT_EQ(plainField->GetInput(StoreTaggedFieldVertex::VALUE_INDEX), scalar);

    // Field store with a heap value: barrier kept, kind recorded.
    auto *keptFieldHeap = vertices[1]->TryCast<StoreTaggedFieldWithBarrierVertex>();
    ASSERT_NE(keptFieldHeap, nullptr) << "HeapObject field store must keep the barrier";
    EXPECT_EQ(keptFieldHeap->GetValueKind(), Kind::HeapObject);
    EXPECT_EQ(keptFieldHeap->GetOffset(), 20);
    EXPECT_EQ(keptFieldHeap->GetInput(StoreTaggedFieldWithBarrierVertex::VALUE_INDEX), heap);

    // Field store with an unknown value: barrier kept, kind stays Unknown.
    auto *keptFieldUnknown = vertices[2]->TryCast<StoreTaggedFieldWithBarrierVertex>();
    ASSERT_NE(keptFieldUnknown, nullptr) << "Unknown field store must keep the barrier";
    EXPECT_EQ(keptFieldUnknown->GetValueKind(), Kind::Unknown);

    // Element store with a scalar: rewritten to the barrier-free variant.
    auto *plainElement = vertices[3]->TryCast<StoreTaggedElementVertex>();
    ASSERT_NE(plainElement, nullptr) << "NonHeap element store must lose the barrier";
    EXPECT_EQ(plainElement->GetOwner(), block);
    EXPECT_EQ(plainElement->GetInput(StoreTaggedElementVertex::OBJECT_INDEX), object);
    EXPECT_EQ(plainElement->GetInput(StoreTaggedElementVertex::INDEX_INDEX), index);
    EXPECT_EQ(plainElement->GetInput(StoreTaggedElementVertex::VALUE_INDEX), scalar);

    auto *keptElementHeap = vertices[4]->TryCast<StoreTaggedElementWithBarrierVertex>();
    ASSERT_NE(keptElementHeap, nullptr) << "HeapObject element store must keep the barrier";
    EXPECT_EQ(keptElementHeap->GetValueKind(), Kind::HeapObject);

    auto *keptElementUnknown = vertices[5]->TryCast<StoreTaggedElementWithBarrierVertex>();
    ASSERT_NE(keptElementUnknown, nullptr) << "Unknown element store must keep the barrier";
    EXPECT_EQ(keptElementUnknown->GetValueKind(), Kind::Unknown);
}

/**
 * @tc.name: ArkSteedWriteBarrierValueKindTest.RunRemovesSetValueWithBarrierForNonHeapOnly
 * @tc.desc: Run() erases SetValueWithBarrier for NonHeap and keeps it for heap values
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedWriteBarrierValueKindTest, RunRemovesSetValueWithBarrierForNonHeapOnly)
{
    BB *block = BB::New(&chunk_);
    SetValueWithBarrierVertex *kept = NewSetValueWithBarrier(block, HeapConstant(), 8);
    SetValueWithBarrierVertex *removed = NewSetValueWithBarrier(block, IntScalar(), 12);
    block->AddVertex(kept);
    block->AddVertex(removed);
    graph_.Add(block);

    RunPass();

    ASSERT_EQ(block->GetVertexCount(), 1U) << "only the NonHeap SetValueWithBarrier is removed";
    EXPECT_TRUE(block->GetVertices()[0]->Is<SetValueWithBarrierVertex>());
    EXPECT_EQ(block->GetVertices()[0], kept);
}

/**
 * @tc.name: ArkSteedWriteBarrierValueKindTest.RunPreservesBlockShapeAndVertexOrder
 * @tc.desc: Run() keeps block order and non-store vertex identity while rewriting stores in place
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedWriteBarrierValueKindTest, RunPreservesBlockShapeAndVertexOrder)
{
    BB *first = BB::New(&chunk_);
    BB *second = BB::New(&chunk_);

    ValueVertex *object = Receiver();
    std::initializer_list<ValueVertex *> loadInputs {object};
    auto *load = Vertex::New<LoadTaggedFieldVertex>(&chunk_, loadInputs, 8);
    load->SetOwner(first);
    auto *fieldScalar = NewFieldStore(first, IntScalar(), 16);
    auto *fieldHeap = NewFieldStore(first, HeapConstant(), 20);
    first->AddVertex(load);
    first->AddVertex(fieldScalar);
    first->AddVertex(fieldHeap);

    auto *setValueHeap = NewSetValueWithBarrier(second, HeapConstant(), 24);
    second->AddVertex(setValueHeap);

    graph_.Add(first);
    graph_.Add(second);

    RunPass();

    EXPECT_EQ(graph_.NumBlocks(), 2U);
    EXPECT_EQ(graph_[0], first);
    EXPECT_EQ(graph_[1], second);

    ASSERT_EQ(first->GetVertexCount(), 3U);
    EXPECT_EQ(first->GetVertices()[0], load) << "non-store vertices keep identity and position";
    EXPECT_NE(first->GetVertices()[1]->TryCast<StoreTaggedFieldVertex>(), nullptr);
    auto *kept = first->GetVertices()[2]->TryCast<StoreTaggedFieldWithBarrierVertex>();
    ASSERT_NE(kept, nullptr);
    EXPECT_EQ(kept->GetValueKind(), Kind::HeapObject);

    ASSERT_EQ(second->GetVertexCount(), 1U);
    EXPECT_EQ(second->GetVertices()[0], setValueHeap);
}

/**
 * @tc.name: ArkSteedWriteBarrierValueKindTest.PhiKindsUseAllInputAlgebra
 * @tc.desc: Phi value kinds require all inputs to be the target kind; mixed Phis stay Unknown
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedWriteBarrierValueKindTest, PhiKindsUseAllInputAlgebra)
{
    ValueVertex *scalarA = IntScalar();
    ValueVertex *scalarB = DoubleScalar();
    ValueVertex *heap = HeapConstant();

    BB *block = BB::New(&chunk_);

    // All inputs scalar -> NonHeap.
    PhiVertex *scalarPhi = NewPhi(2);
    scalarPhi->SetPredecessor(0, scalarA);
    scalarPhi->SetPredecessor(1, scalarB);

    // Mixed scalar/tagged inputs -> Unknown (no target kind holds for every input).
    PhiVertex *mixedPhi = NewPhi(2);
    mixedPhi->SetPredecessor(0, scalarA);
    mixedPhi->SetPredecessor(1, heap);

    // All inputs heap -> HeapObject.
    PhiVertex *heapPhi = NewPhi(2);
    heapPhi->SetPredecessor(0, heap);
    heapPhi->SetPredecessor(1, HeapTaggedValue());

    // Chained Phi: the non-heap kind propagates through the second Phi.
    PhiVertex *chainPhi = NewPhi(2);
    chainPhi->SetPredecessor(0, scalarPhi);
    chainPhi->SetPredecessor(1, scalarB);

    // Boundary: a Phi without inputs cannot be proven, so it stays Unknown.
    PhiVertex *emptyPhi = NewPhi(0);

    block->AddPhiVertex(scalarPhi);
    block->AddPhiVertex(mixedPhi);
    block->AddPhiVertex(heapPhi);
    block->AddPhiVertex(chainPhi);
    block->AddPhiVertex(emptyPhi);

    auto *storeScalarPhi = NewFieldStore(block, scalarPhi, 16);
    auto *storeMixedPhi = NewFieldStore(block, mixedPhi, 20);
    auto *storeHeapPhi = NewFieldStore(block, heapPhi, 24);
    auto *storeChainPhi = NewFieldStore(block, chainPhi, 28);
    auto *storeEmptyPhi = NewFieldStore(block, emptyPhi, 32);
    block->AddVertex(storeScalarPhi);
    block->AddVertex(storeMixedPhi);
    block->AddVertex(storeHeapPhi);
    block->AddVertex(storeChainPhi);
    block->AddVertex(storeEmptyPhi);
    graph_.Add(block);

    RunPass();

    const ChunkVector<NonControlVertex *> &vertices = block->GetVertices();
    ASSERT_EQ(vertices.size(), 5U);

    EXPECT_NE(vertices[0]->TryCast<StoreTaggedFieldVertex>(), nullptr)
        << "all-scalar Phi drives barrier elision";

    auto *keptMixed = vertices[1]->TryCast<StoreTaggedFieldWithBarrierVertex>();
    ASSERT_NE(keptMixed, nullptr) << "mixed scalar/heap Phi must keep the barrier";
    EXPECT_EQ(keptMixed->GetValueKind(), Kind::Unknown);

    auto *keptHeap = vertices[2]->TryCast<StoreTaggedFieldWithBarrierVertex>();
    ASSERT_NE(keptHeap, nullptr) << "all-heap Phi keeps the barrier and is classified HeapObject";
    EXPECT_EQ(keptHeap->GetValueKind(), Kind::HeapObject);

    EXPECT_NE(vertices[3]->TryCast<StoreTaggedFieldVertex>(), nullptr)
        << "chained all-scalar Phi propagates NonHeap";

    auto *keptEmpty = vertices[4]->TryCast<StoreTaggedFieldWithBarrierVertex>();
    ASSERT_NE(keptEmpty, nullptr) << "input-less Phi must not be proven NonHeap";
    EXPECT_EQ(keptEmpty->GetValueKind(), Kind::Unknown);
}

/**
 * @tc.name: ArkSteedWriteBarrierValueKindTest.MutationControlRejectsFalseNonHeapForHeapValue
 * @tc.desc: negative control (H6/D5): a heap value must never be classified NonHeap
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedWriteBarrierValueKindTest, MutationControlRejectsFalseNonHeapForHeapValue)
{
    // Mutation control (H6, D5). A false NonHeap for a heap value makes the pass drop
    // the write barrier and risks memory corruption (P0). The expectations below are
    // the detector: substituting Kind::NonHeap for Kind::HeapObject (or inverting the
    // EXPECT_NE assertions) turns this case red. The mutation was executed once during
    // development for the G4.3u closure entry of matrix.md.
    ValueVertex *heapTagged = HeapTaggedValue();
    ValueVertex *heapConst = HeapConstant();
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(heapTagged), Kind::HeapObject);
    EXPECT_EQ(ClassifyDirectWriteBarrierValueKind(heapConst), Kind::HeapObject);
    EXPECT_NE(ClassifyDirectWriteBarrierValueKind(heapTagged), Kind::NonHeap);
    EXPECT_NE(ClassifyDirectWriteBarrierValueKind(heapConst), Kind::NonHeap);

    // The pass must retain the barrier for a heap value, both for a field and an
    // element store, and must record HeapObject on the kept store.
    BB *block = BB::New(&chunk_);
    auto *fieldHeap = NewFieldStore(block, heapConst, 16);
    auto *elementHeap = NewElementStore(block, heapTagged);
    block->AddVertex(fieldHeap);
    block->AddVertex(elementHeap);
    graph_.Add(block);

    RunPass();

    ASSERT_EQ(block->GetVertexCount(), 2U);
    auto *keptField = block->GetVertices()[0]->TryCast<StoreTaggedFieldWithBarrierVertex>();
    ASSERT_NE(keptField, nullptr) << "heap field store must keep the barrier";
    EXPECT_EQ(keptField->GetValueKind(), Kind::HeapObject);
    auto *keptElement = block->GetVertices()[1]->TryCast<StoreTaggedElementWithBarrierVertex>();
    ASSERT_NE(keptElement, nullptr) << "heap element store must keep the barrier";
    EXPECT_EQ(keptElement->GetValueKind(), Kind::HeapObject);
}

}  // namespace panda::ecmascript::arksteed
