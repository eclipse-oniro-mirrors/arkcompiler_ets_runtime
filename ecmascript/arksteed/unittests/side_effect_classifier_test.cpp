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

#include "ecmascript/arksteed/arksteed_side_effect_classifier.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_object.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/tests/test_helper.h"

namespace panda::ecmascript::arksteed {

// Fixture A of the unit domain contract (arksteed-cpp-unit-test-domains, C8):
// every vertex is built directly through Vertex::New over a Chunk, without a VM
// or a graph builder. Exactly the vertices the classifier reads are populated,
// and every public field of the returned SideEffectDescriptor is inspected.
class ArkSteedSideEffectClassifierTest : public testing::Test {
protected:
    using Classifier = ArkSteedSideEffectClassifier;

    NativeAreaAllocator native_;
    Chunk chunk_ {&native_};

    ValueVertex *NewTagged()
    {
        return Vertex::New<TaggedConstantVertex>(&chunk_, 0, static_cast<uint64_t>(0));
    }

    ValueVertex *NewInt32()
    {
        return Vertex::New<Int32ConstantVertex>(&chunk_, 0, 0);
    }

    ValueVertex *NewInt64()
    {
        return Vertex::New<Int64ConstantVertex>(&chunk_, 0, static_cast<int64_t>(0));
    }

    ValueVertex *NewFloat64()
    {
        return Vertex::New<Float64ConstantVertex>(&chunk_, 0, 0.0);
    }

    // Independent oracle (D2, C8): the classifier is a contract about effect
    // ordering. A vertex that writes a property, an element or an environment
    // slot must carry a write kind; NO_SIDE_EFFECT and SAFE_CALL would let the
    // compiler reorder or eliminate the write and lose the effect order.
    static bool IsWriteKind(SideEffectKind kind)
    {
        switch (kind) {
            case SideEffectKind::FIELD_WRITE:
            case SideEffectKind::ELEMENTS_WRITE:
            case SideEffectKind::TYPED_ARRAY_WRITE:
            case SideEffectKind::ENV_SLOT_WRITE:
            case SideEffectKind::MAP_TRANSITION:
            case SideEffectKind::ELEMENTS_KIND_TRANSITION:
                return true;
            default:
                return false;
        }
    }

    // The two kinds that would let a write be reordered or eliminated.
    static bool IsMisclassifiedWrite(SideEffectKind kind)
    {
        return kind == SideEffectKind::NO_SIDE_EFFECT || kind == SideEffectKind::SAFE_CALL;
    }

    // A write descriptor must report a write kind (never a reorderable one).
    void ExpectWriteKind(const SideEffectDescriptor &effect, SideEffectKind kind)
    {
        EXPECT_TRUE(IsWriteKind(effect.kind))
            << "write vertex classified as a non-write effect: the effect order would be lost";
        EXPECT_FALSE(IsMisclassifiedWrite(effect.kind))
            << "property write classified as NO_SIDE_EFFECT/SAFE_CALL: the effect order would be lost";
        EXPECT_EQ(effect.kind, kind);
    }

    // A write descriptor must report the write kind and address the written
    // location; no other descriptor field may be populated.
    void ExpectWrite(const SideEffectDescriptor &effect, SideEffectKind kind, ValueVertex *receiver,
                     PropertyKey key)
    {
        ExpectWriteKind(effect, kind);
        EXPECT_EQ(effect.receiver, receiver);
        EXPECT_EQ(effect.propertyKey, key);
        EXPECT_EQ(effect.env, nullptr);
        EXPECT_EQ(effect.envSlot, -1);
        EXPECT_EQ(effect.envSlotValue, nullptr);
        EXPECT_EQ(effect.transitionSources, nullptr);
    }

    // The conjunction of every assertion a positive write case applies, as a
    // pure predicate: the mutation control can feed it a mutated expectation.
    static bool MatchesWriteExpectation(const SideEffectDescriptor &effect, SideEffectKind expected,
                                        ValueVertex *receiver, PropertyKey key)
    {
        return effect.kind == expected && IsWriteKind(effect.kind) && !IsMisclassifiedWrite(effect.kind) &&
               effect.receiver == receiver && effect.propertyKey == key && effect.env == nullptr &&
               effect.envSlot == -1 && effect.envSlotValue == nullptr && effect.transitionSources == nullptr;
    }

    // A descriptor that records only the effect kind (non-write, typed-array,
    // call or allocation vertex) keeps every location field at its default.
    void ExpectEffectWithoutLocation(const SideEffectDescriptor &effect, SideEffectKind kind)
    {
        EXPECT_EQ(effect.kind, kind);
        EXPECT_EQ(effect.receiver, nullptr);
        EXPECT_EQ(effect.propertyKey, PropertyKey::Unknown());
        EXPECT_EQ(effect.env, nullptr);
        EXPECT_EQ(effect.envSlot, -1);
        EXPECT_EQ(effect.envSlotValue, nullptr);
        EXPECT_EQ(effect.transitionSources, nullptr);
    }
};

// --- Field stores ---------------------------------------------------------

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreTaggedFieldNamedProperty)
{
    ValueVertex *object = NewTagged();
    ValueVertex *value = NewTagged();
    auto *vertex = Vertex::New<StoreTaggedFieldVertex>(
        &chunk_, static_cast<size_t>(StoreTaggedFieldVertex::NUM_INPUTS), static_cast<int32_t>(16),
        static_cast<uint32_t>(7));
    vertex->SetInput(StoreTaggedFieldVertex::OBJECT_INDEX, object);
    vertex->SetInput(StoreTaggedFieldVertex::VALUE_INDEX, value);

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::FIELD_WRITE, object, PropertyKey::Named(7));
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreTaggedFieldUnknownProperty)
{
    ValueVertex *object = NewTagged();
    ValueVertex *value = NewTagged();
    auto *vertex = Vertex::New<StoreTaggedFieldVertex>(
        &chunk_, static_cast<size_t>(StoreTaggedFieldVertex::NUM_INPUTS), static_cast<int32_t>(16),
        static_cast<uint32_t>(StoreTaggedFieldVertex::UNKNOWN_PROPERTY_ID));
    vertex->SetInput(StoreTaggedFieldVertex::OBJECT_INDEX, object);
    vertex->SetInput(StoreTaggedFieldVertex::VALUE_INDEX, value);

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::FIELD_WRITE, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreTaggedFieldWithBarrier)
{
    ValueVertex *object = NewTagged();
    ValueVertex *value = NewTagged();
    auto *vertex = Vertex::New<StoreTaggedFieldWithBarrierVertex>(
        &chunk_, static_cast<size_t>(StoreTaggedFieldWithBarrierVertex::NUM_INPUTS), static_cast<int32_t>(16),
        ArkSteedWriteBarrierValueKind::HeapObject);
    vertex->SetInput(StoreTaggedFieldWithBarrierVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(StoreTaggedFieldWithBarrierVertex::OBJECT_INDEX, object);
    vertex->SetInput(StoreTaggedFieldWithBarrierVertex::VALUE_INDEX, value);

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::FIELD_WRITE, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreSharedFieldWithBarrier)
{
    ValueVertex *object = NewTagged();
    ValueVertex *value = NewTagged();
    auto *vertex = Vertex::New<StoreSharedFieldWithBarrierVertex>(
        &chunk_, static_cast<size_t>(StoreSharedFieldWithBarrierVertex::NUM_INPUTS), static_cast<int32_t>(16),
        ArkSteedWriteBarrierValueKind::HeapObject);
    vertex->SetInput(StoreSharedFieldWithBarrierVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(StoreSharedFieldWithBarrierVertex::OBJECT_INDEX, object);
    vertex->SetInput(StoreSharedFieldWithBarrierVertex::VALUE_INDEX, value);

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::FIELD_WRITE, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreTaggedFieldByHClass)
{
    ValueVertex *object = NewTagged();
    ValueVertex *value = NewTagged();
    std::vector<StoreTaggedFieldByHClassCase> cases {StoreTaggedFieldByHClassCase {3, 16, false}};
    auto *vertex = Vertex::New<StoreTaggedFieldByHClassVertex>(
        &chunk_, static_cast<size_t>(StoreTaggedFieldByHClassVertex::NUM_INPUTS), &chunk_, cases,
        ArkSteedWriteBarrierValueKind::HeapObject, static_cast<uint32_t>(0));
    vertex->SetInput(StoreTaggedFieldByHClassVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(StoreTaggedFieldByHClassVertex::OBJECT_INDEX, object);
    vertex->SetInput(StoreTaggedFieldByHClassVertex::VALUE_INDEX, value);

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::FIELD_WRITE, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreInt32Field)
{
    ValueVertex *object = NewTagged();
    auto *vertex = Vertex::New<StoreInt32FieldVertex>(
        &chunk_, static_cast<size_t>(StoreInt32FieldVertex::NUM_INPUTS), static_cast<int32_t>(16));
    vertex->SetInput(StoreInt32FieldVertex::STORE_TARGET_INDEX, object);
    vertex->SetInput(StoreInt32FieldVertex::VALUE_INDEX, NewInt32());

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::FIELD_WRITE, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreDoubleField)
{
    ValueVertex *object = NewTagged();
    auto *vertex = Vertex::New<StoreDoubleFieldVertex>(
        &chunk_, static_cast<size_t>(StoreDoubleFieldVertex::NUM_INPUTS), static_cast<int32_t>(16));
    vertex->SetInput(StoreDoubleFieldVertex::STORE_TARGET_INDEX, object);
    vertex->SetInput(StoreDoubleFieldVertex::VALUE_INDEX, NewFloat64());

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::FIELD_WRITE, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreInt32FieldWithRep)
{
    ValueVertex *object = NewTagged();
    auto *vertex = Vertex::New<StoreInt32FieldWithRepVertex>(
        &chunk_, static_cast<size_t>(StoreInt32FieldWithRepVertex::NUM_INPUTS), &chunk_, static_cast<int32_t>(16),
        static_cast<uint32_t>(0));
    vertex->SetInput(StoreInt32FieldWithRepVertex::STORE_TARGET_INDEX, object);
    vertex->SetInput(StoreInt32FieldWithRepVertex::VALUE_INDEX, NewTagged());

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::FIELD_WRITE, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreDoubleFieldWithRep)
{
    ValueVertex *object = NewTagged();
    auto *vertex = Vertex::New<StoreDoubleFieldWithRepVertex>(
        &chunk_, static_cast<size_t>(StoreDoubleFieldWithRepVertex::NUM_INPUTS), &chunk_, static_cast<int32_t>(16),
        static_cast<uint32_t>(0));
    vertex->SetInput(StoreDoubleFieldWithRepVertex::STORE_TARGET_INDEX, object);
    vertex->SetInput(StoreDoubleFieldWithRepVertex::VALUE_INDEX, NewTagged());

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::FIELD_WRITE, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, SetValueWithBarrierIsMetadataOnly)
{
    auto *vertex = Vertex::New<SetValueWithBarrierVertex>(
        &chunk_, static_cast<size_t>(SetValueWithBarrierVertex::NUM_INPUTS), static_cast<int32_t>(16));
    vertex->SetInput(SetValueWithBarrierVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(SetValueWithBarrierVertex::OBJECT_INDEX, NewTagged());
    vertex->SetInput(SetValueWithBarrierVertex::VALUE_INDEX, NewTagged());

    // StoreEnvSlot records the write; this node only maintains GC metadata.
    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::SAFE_CALL);
}

// --- Element stores -------------------------------------------------------

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreTaggedElement)
{
    ValueVertex *object = NewTagged();
    auto *vertex =
        Vertex::New<StoreTaggedElementVertex>(&chunk_, static_cast<size_t>(StoreTaggedElementVertex::NUM_INPUTS));
    vertex->SetInput(StoreTaggedElementVertex::OBJECT_INDEX, object);
    vertex->SetInput(StoreTaggedElementVertex::INDEX_INDEX, NewInt32());
    vertex->SetInput(StoreTaggedElementVertex::VALUE_INDEX, NewTagged());

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::ELEMENTS_WRITE, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreTaggedElementWithBarrier)
{
    ValueVertex *object = NewTagged();
    auto *vertex = Vertex::New<StoreTaggedElementWithBarrierVertex>(
        &chunk_, static_cast<size_t>(StoreTaggedElementWithBarrierVertex::NUM_INPUTS),
        ArkSteedWriteBarrierValueKind::HeapObject);
    vertex->SetInput(StoreTaggedElementWithBarrierVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(StoreTaggedElementWithBarrierVertex::OBJECT_INDEX, object);
    vertex->SetInput(StoreTaggedElementWithBarrierVertex::INDEX_INDEX, NewInt32());
    vertex->SetInput(StoreTaggedElementWithBarrierVertex::VALUE_INDEX, NewTagged());

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::ELEMENTS_WRITE, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, EnsureWritableElements)
{
    ValueVertex *object = NewTagged();
    auto *vertex = Vertex::New<EnsureWritableElementsVertex>(
        &chunk_, static_cast<size_t>(EnsureWritableElementsVertex::NUM_INPUTS));
    vertex->SetInput(EnsureWritableElementsVertex::ELEMENTS_INDEX, NewTagged());
    vertex->SetInput(EnsureWritableElementsVertex::OBJECT_INDEX, object);

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::FIELD_WRITE, object, PropertyKey::Elements());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, EnsurePropertiesCapacity)
{
    auto *vertex = Vertex::New<EnsurePropertiesCapacityVertex>(
        &chunk_, static_cast<size_t>(EnsurePropertiesCapacityVertex::NUM_INPUTS), static_cast<int32_t>(0));
    vertex->SetInput(EnsurePropertiesCapacityVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(EnsurePropertiesCapacityVertex::OBJECT_INDEX, NewTagged());

    // Growth copies values without changing the HClass; the property write is
    // recorded by the following store.
    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::SAFE_CALL);
}

// --- HClass transition, env slot, typed arrays ----------------------------

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, TransitionHClassWithBarrier)
{
    ValueVertex *object = NewTagged();
    auto *vertex = Vertex::New<TransitionHClassWithBarrierVertex>(
        &chunk_, static_cast<size_t>(TransitionHClassWithBarrierVertex::NUM_INPUTS));
    vertex->SetInput(TransitionHClassWithBarrierVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(TransitionHClassWithBarrierVertex::OBJECT_INDEX, object);
    vertex->SetInput(TransitionHClassWithBarrierVertex::HCLASS_INDEX, NewTagged());

    ExpectWrite(Classifier::Classify(vertex), SideEffectKind::MAP_TRANSITION, object, PropertyKey::Unknown());
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreEnvSlot)
{
    ValueVertex *env = NewTagged();
    ValueVertex *value = NewTagged();
    auto *vertex =
        Vertex::New<StoreEnvSlotVertex>(&chunk_, static_cast<size_t>(StoreEnvSlotVertex::NUM_INPUTS),
                                        static_cast<int32_t>(42));
    vertex->SetInput(StoreEnvSlotVertex::ENV_INDEX, env);
    vertex->SetInput(StoreEnvSlotVertex::VALUE_INDEX, value);

    SideEffectDescriptor effect = Classifier::Classify(vertex);
    ExpectWriteKind(effect, SideEffectKind::ENV_SLOT_WRITE);
    EXPECT_EQ(effect.receiver, nullptr);
    EXPECT_EQ(effect.propertyKey, PropertyKey::Unknown());
    EXPECT_EQ(effect.transitionSources, nullptr);
    EXPECT_EQ(effect.env, env);
    EXPECT_EQ(effect.envSlot, 42);
    EXPECT_EQ(effect.envSlotValue, value);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreIntTypedArrayElement)
{
    auto *vertex = Vertex::New<StoreIntTypedArrayElementVertex>(
        &chunk_, static_cast<size_t>(StoreIntTypedArrayElementVertex::NUM_INPUTS), JSType::JS_INT32_ARRAY,
        OnHeapMode::ON_HEAP);
    vertex->SetInput(StoreIntTypedArrayElementVertex::RECEIVER_INDEX, NewTagged());
    vertex->SetInput(StoreIntTypedArrayElementVertex::INDEX_INDEX, NewInt32());
    vertex->SetInput(StoreIntTypedArrayElementVertex::VALUE_INDEX, NewInt32());

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::TYPED_ARRAY_WRITE);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StoreFloatTypedArrayElement)
{
    auto *vertex = Vertex::New<StoreFloatTypedArrayElementVertex>(
        &chunk_, static_cast<size_t>(StoreFloatTypedArrayElementVertex::NUM_INPUTS), JSType::JS_FLOAT64_ARRAY,
        OnHeapMode::ON_HEAP);
    vertex->SetInput(StoreFloatTypedArrayElementVertex::RECEIVER_INDEX, NewTagged());
    vertex->SetInput(StoreFloatTypedArrayElementVertex::INDEX_INDEX, NewInt32());
    vertex->SetInput(StoreFloatTypedArrayElementVertex::VALUE_INDEX, NewFloat64());

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::TYPED_ARRAY_WRITE);
}

// --- Calls and allocations ------------------------------------------------

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, CallIsUnknown)
{
    auto *vertex = Vertex::New<CallVertex>(&chunk_, static_cast<size_t>(0), static_cast<uint32_t>(0));

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::UNKNOWN_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, CallKnownJSFunctionIsUnknown)
{
    auto *vertex = Vertex::New<CallKnownJSFunctionVertex>(
        &chunk_, static_cast<size_t>(0), static_cast<uint32_t>(0));

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::UNKNOWN_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, CallSelfIsUnknown)
{
    auto *vertex = Vertex::New<CallSelfVertex>(
        &chunk_, static_cast<size_t>(0), static_cast<uint32_t>(0), static_cast<uint32_t>(0));

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::UNKNOWN_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, CallKnownBuiltinIsUnknown)
{
    auto *vertex = Vertex::New<CallKnownBuiltinVertex>(
        &chunk_, static_cast<size_t>(0), static_cast<uint32_t>(0),
        kungfu::BuiltinsStubCSigns::ID::BooleanConstructor, BuiltinCallEntryKind::NATIVE_CPP);

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::UNKNOWN_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, CallRuntimeUnknown)
{
    auto *vertex = Vertex::New<CallRuntimeVertex>(
        &chunk_, static_cast<size_t>(0), RuntimeStubID::ID_CallRuntime, SideEffectKind::UNKNOWN_CALL);

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::UNKNOWN_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, CallRuntimeSafe)
{
    auto *vertex = Vertex::New<CallRuntimeVertex>(
        &chunk_, static_cast<size_t>(0), RuntimeStubID::ID_CallRuntime, SideEffectKind::SAFE_CALL);

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::SAFE_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, CallCommonStubUnknown)
{
    auto *vertex = Vertex::New<CallCommonStubVertex>(
        &chunk_, static_cast<size_t>(0), CommonStubID::Add, SideEffectKind::UNKNOWN_CALL);

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::UNKNOWN_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, CallCommonStubSafe)
{
    auto *vertex = Vertex::New<CallCommonStubVertex>(
        &chunk_, static_cast<size_t>(0), CommonStubID::Add, SideEffectKind::SAFE_CALL);

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::SAFE_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, AllocateInYoungHasNoSideEffect)
{
    auto* vertex
        = Vertex::New<AllocateInYoungVertex>(&chunk_, static_cast<size_t>(AllocateInYoungVertex::NUM_INPUTS));
    vertex->SetInput(AllocateInYoungVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(AllocateInYoungVertex::SIZE_INDEX, NewInt64());

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::NO_SIDE_EFFECT);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, UninitializedAllocationRuntimeInvalidatesFacts)
{
    auto* size = NewTagged();
    auto* hclass = NewTagged();
    auto* sizeOnly = Vertex::New<CallRuntimeVertex>(
        &chunk_, std::initializer_list<ValueVertex*> {size}, RTSTUB_ID(AllocateInYoung));
    auto* withHClass = Vertex::New<CallRuntimeVertex>(
        &chunk_, std::initializer_list<ValueVertex*> {size, hclass}, RTSTUB_ID(AllocateInYoung));
    for (auto* vertex: {sizeOnly, withHClass}) {
        EXPECT_TRUE(vertex->ReturnsUninitializedAllocation());
        EXPECT_TRUE(vertex->IsCall());
        EXPECT_TRUE(vertex->CanAllocate());
        EXPECT_TRUE(vertex->CanThrow());
        EXPECT_TRUE(vertex->CanLazyDeopt());
        ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::UNKNOWN_CALL);
    }
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, ArgumentsInitializersAreSafe)
{
    auto* elements = Vertex::New<InitializeUnmappedArgumentsElementsVertex>(
        &chunk_, static_cast<size_t>(InitializeUnmappedArgumentsElementsVertex::NUM_INPUTS));
    auto* arguments = Vertex::New<InitializeUnmappedArgumentsVertex>(
        &chunk_, static_cast<size_t>(InitializeUnmappedArgumentsVertex::NUM_INPUTS));
    ExpectEffectWithoutLocation(Classifier::Classify(elements), SideEffectKind::SAFE_CALL);
    ExpectEffectWithoutLocation(Classifier::Classify(arguments), SideEffectKind::SAFE_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, InitializeEmptyArrayIsSafe)
{
    auto* vertex = Vertex::New<InitializeEmptyArrayVertex>(
        &chunk_,
        static_cast<size_t>(InitializeEmptyArrayVertex::NUM_INPUTS),
        static_cast<uint32_t>(JSArray::SIZE + JSTaggedValue::TaggedTypeSize()));
    vertex->SetInput(InitializeEmptyArrayVertex::ALLOCATION_INDEX, NewTagged());
    vertex->SetInput(InitializeEmptyArrayVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(InitializeEmptyArrayVertex::GLOBAL_ENV_INDEX, NewTagged());
    vertex->SetInput(InitializeEmptyArrayVertex::HCLASS_INDEX, NewTagged());

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::SAFE_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, InitializeEmptyObjectIsSafe)
{
    auto* vertex
        = Vertex::New<InitializeEmptyObjectVertex>(&chunk_,
                                                   static_cast<size_t>(InitializeEmptyObjectVertex::NUM_INPUTS),
                                                   static_cast<uint32_t>(JSObject::SIZE));
    vertex->SetInput(InitializeEmptyObjectVertex::ALLOCATION_INDEX, NewTagged());
    vertex->SetInput(InitializeEmptyObjectVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(InitializeEmptyObjectVertex::HCLASS_INDEX, NewTagged());
    vertex->SetInput(InitializeEmptyObjectVertex::SIZE_INDEX, NewInt64());

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::SAFE_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, PrepareSharedStoreFieldIsSafe)
{
    auto *vertex = Vertex::New<PrepareSharedStoreFieldVertex>(
        &chunk_, static_cast<size_t>(PrepareSharedStoreFieldVertex::NUM_INPUTS), static_cast<uint64_t>(0));
    vertex->SetInput(PrepareSharedStoreFieldVertex::VALUE_INDEX, NewTagged());

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::SAFE_CALL);
}

HWTEST_F_L0(ArkSteedSideEffectClassifierTest, StringEqualIsSafe)
{
    auto *vertex = Vertex::New<StringEqualVertex>(&chunk_, static_cast<size_t>(StringEqualVertex::NUM_INPUTS));
    vertex->SetInput(StringEqualVertex::GLUE_INDEX, NewInt64());
    vertex->SetInput(StringEqualVertex::LEFT_INDEX, NewTagged());
    vertex->SetInput(StringEqualVertex::RIGHT_INDEX, NewTagged());
    vertex->SetInput(StringEqualVertex::GLOBAL_ENV_INDEX, NewTagged());

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::SAFE_CALL);
}

// The header closes its overload set with a constrained write-vertex fallback
// (arksteed_side_effect_classifier.h:229-234). A write vertex without a specific
// overload must still be treated as an unknown call, never as no-effect.
HWTEST_F_L0(ArkSteedSideEffectClassifierTest, UnclassifiedWriteVertexFallsBackToUnknownCall)
{
    auto *vertex =
        Vertex::New<StoreTaggedToAddressVertex>(&chunk_,
                                                static_cast<size_t>(StoreTaggedToAddressVertex::NUM_INPUTS), 16);
    vertex->SetInput(StoreTaggedToAddressVertex::OBJECT_INDEX, NewInt64());
    vertex->SetInput(StoreTaggedToAddressVertex::VALUE_INDEX, NewTagged());

    ExpectEffectWithoutLocation(Classifier::Classify(vertex), SideEffectKind::UNKNOWN_CALL);
}

// Negative control (H6, D5): the effect-ordering oracle must be sensitive. The
// classifier reports FIELD_WRITE for a property store; the same detector that
// is applied by every positive case recognizes a descriptor that was mutated to
// NO_SIDE_EFFECT/SAFE_CALL. If the production classifier returned such a kind
// for a property store, every positive case above would be red.
HWTEST_F_L0(ArkSteedSideEffectClassifierTest, MutationControlRejectsMisclassifiedWrite)
{
    ValueVertex *object = NewTagged();
    auto *vertex = Vertex::New<StoreTaggedFieldVertex>(
        &chunk_, static_cast<size_t>(StoreTaggedFieldVertex::NUM_INPUTS), static_cast<int32_t>(16),
        static_cast<uint32_t>(7));
    vertex->SetInput(StoreTaggedFieldVertex::OBJECT_INDEX, object);
    vertex->SetInput(StoreTaggedFieldVertex::VALUE_INDEX, NewTagged());

    SideEffectDescriptor effect = Classifier::Classify(vertex);
    EXPECT_FALSE(IsMisclassifiedWrite(effect.kind))
        << "detector accepts NO_SIDE_EFFECT/SAFE_CALL for a property write";

    SideEffectDescriptor mutated = effect;
    mutated.kind = SideEffectKind::SAFE_CALL;
    EXPECT_TRUE(IsMisclassifiedWrite(mutated.kind))
        << "mutation control insensitive: misclassified write is not detected";
    mutated.kind = SideEffectKind::NO_SIDE_EFFECT;
    EXPECT_TRUE(IsMisclassifiedWrite(mutated.kind))
        << "mutation control insensitive: NO_SIDE_EFFECT is not detected";

    // Mutating the expected kind to a reorderable kind makes the write check
    // red (non-tautology): the predicate below is the conjunction of every
    // positive-case assertion and returns false for a mismatched expectation.
    EXPECT_TRUE(MatchesWriteExpectation(effect, SideEffectKind::FIELD_WRITE, object, PropertyKey::Named(7)));
    EXPECT_FALSE(MatchesWriteExpectation(effect, SideEffectKind::SAFE_CALL, object, PropertyKey::Named(7)))
        << "mutating the expected kind to SAFE_CALL did not turn the write check red";
    EXPECT_FALSE(MatchesWriteExpectation(effect, SideEffectKind::NO_SIDE_EFFECT, object, PropertyKey::Named(7)))
        << "mutating the expected kind to NO_SIDE_EFFECT did not turn the write check red";
}

}  // namespace panda::ecmascript::arksteed
