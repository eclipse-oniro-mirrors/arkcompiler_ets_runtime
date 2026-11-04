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

#include <array>
#include <string>

#include "ecmascript/js_array.h"
#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/js_object-inl.h"
#include "ecmascript/layout_info-inl.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/runtime.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {
class ArkSteedObjectLiteralTemplateTest : public BaseTestWithScope<false> {
public:
    void SetUp() override
    {
        BaseTestWithScope<false>::SetUp();
        // Match Jit::SetEnableOrDisable: unboxed layouts use the non-evacuating nonmovable-space mode.
        // Keep forced allocation collections and the explicit full collections enabled.
        Runtime::GetInstance()->DisableEvacuateNonMovableSpace();
    }
};

// A clone must detach COW storage before writes; neither its fields nor its HClass
// transitions may change the private template's allocation size or length slot.
HWTEST_F_L0(ArkSteedObjectLiteralTemplateTest, ArrayClonePreservesTemplateLayoutAndCOW)
{
    auto* factory = instance->GetFactory();
    JSHandle<TaggedArray> elements(factory->NewCOWTaggedArray(3));
    for (uint32_t i = 0; i < 3; ++i) {
        elements->Set(thread, i, JSTaggedValue(static_cast<int32_t>(i + 1)));
    }
    auto literal = JSArray::CreateArrayFromList(thread, elements);
    JSHandle<JSHClass> hclass(thread, literal->GetJSHClass());
    const uint32_t size = hclass->GetObjectSize();
    const uint32_t start = hclass->GetInlinedPropsStartSize();
    EXPECT_EQ(start, JSArray::SIZE);
    EXPECT_FALSE(hclass->IsObjSizeTrackingInProgress());
    auto clone = factory->CloneArrayLiteral(literal);
    auto sibling = factory->CloneArrayLiteral(literal);
#if defined(ENABLE_COW_ARRAY)
    EXPECT_EQ(clone->GetElements(thread), elements.GetTaggedValue());
#endif
    JSHandle<JSTaggedValue> value(thread, JSTaggedValue(1.25));
    JSArray::FastSetPropertyByValue(thread, JSHandle<JSTaggedValue>(clone), 0, value);
    JSArray::SetCapacity(thread, JSHandle<JSObject>(clone), 3, 1);
    ASSERT_FALSE(thread->HasPendingException());
    EXPECT_NE(clone->GetElements(thread), elements.GetTaggedValue());
    instance->CollectGarbage(TriggerGCType::FULL_GC);
    auto fresh = factory->CloneArrayLiteral(literal);
    EXPECT_NE(*fresh, *clone);
    EXPECT_EQ(literal->GetJSHClass(), *hclass);
    EXPECT_EQ(hclass->GetObjectSize(), size);
    EXPECT_EQ(hclass->GetInlinedPropsStartSize(), start);
    EXPECT_EQ(literal->GetElements(thread), elements.GetTaggedValue());
    EXPECT_TRUE(elements.GetTaggedValue().IsCOWArray());
    EXPECT_EQ(literal->GetArrayLength(), 3U);
    EXPECT_EQ(sibling->GetArrayLength(), 3U);
    EXPECT_EQ(fresh->GetArrayLength(), 3U);
    EXPECT_EQ(clone->GetArrayLength(), 1U);
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(elements->Get(thread, i), JSTaggedValue(static_cast<int32_t>(i + 1)));
    }
    auto layout = LayoutInfo::Cast(hclass->GetLayout(thread).GetTaggedObject());
    auto attr = layout->GetAttr(thread, JSArray::LENGTH_INLINE_PROPERTY_INDEX);
    EXPECT_TRUE(attr.IsInlinedProps() && attr.IsTaggedRep() && attr.IsAccessor());
    EXPECT_EQ(attr.GetOffset(), JSArray::LENGTH_INLINE_PROPERTY_INDEX);
}

// This exercises the runtime isolation relied on by constant initialization. It does not
// create a constant pool, run the broker, or execute generated allocation code.
HWTEST_F_L0(ArkSteedObjectLiteralTemplateTest, RawMixedCloneRepresentationIsolation)
{
    const std::array<uint64_t, 9> bits = {
        0x0000000000000000ULL, // Raw DOUBLE +0.
        0x8000000000000000ULL, // Raw DOUBLE -0.
        0x0000000000000001ULL, // Raw DOUBLE smallest positive subnormal.
        42ULL, // Raw INT, not a tagged integer.
        JSTaggedValue(42).GetRawData(),
        JSTaggedValue(1.25).GetRawData(),
        JSTaggedValue::True().GetRawData(),
        JSTaggedValue::Null().GetRawData(),
        JSTaggedValue::Undefined().GetRawData(),
    };
    constexpr uint32_t FIELD_COUNT = 9;
    constexpr uint32_t RAW_FIELD_COUNT = 4;
    const std::array<Representation, FIELD_COUNT> reps = {
        Representation::DOUBLE,
        Representation::DOUBLE,
        Representation::DOUBLE,
        Representation::INT,
        Representation::TAGGED,
        Representation::TAGGED,
        Representation::TAGGED,
        Representation::TAGGED,
        Representation::TAGGED,
    };
    auto* factory = instance->GetFactory();
    JSHandle<JSTaggedValue> nullProto(thread, JSTaggedValue::Null());
    auto parent = factory->NewEcmaHClass(JSObject::SIZE, 0, JSType::JS_OBJECT, nullProto);
    auto hclass = factory->NewEcmaHClass(JSObject::SIZE, FIELD_COUNT, JSType::JS_OBJECT, nullProto);
    auto layout = factory->CreateLayoutInfo(FIELD_COUNT);
    for (uint32_t i = 0; i < FIELD_COUNT; ++i) {
        auto key = factory->NewFromASCII(("field" + std::to_string(i)).c_str());
        auto attr = PropertyAttributes::Default();
        attr.SetIsInlinedProps(true);
        attr.SetOffset(i);
        attr.SetRepresentation(reps[i]);
        layout->AddKey(thread, i, key.GetTaggedValue(), attr);
    }
    hclass->SetLayout(thread, layout.GetTaggedValue());
    hclass->SetNumberOfProps(FIELD_COUNT);
    hclass->SetIsAllTaggedProp(false);
    // A nonempty parent makes accidental retention of the transition ancestry observable.
    hclass->SetParent(thread, parent.GetTaggedValue());
    auto literal = factory->NewJSObject(hclass);
    for (uint32_t i = 0; i < FIELD_COUNT; ++i) {
        literal->SetPropertyInlinedPropsWithRep(thread, i, JSTaggedValue(bits[i]));
    }
    const uint32_t objectSize = hclass->GetObjectSize();
    std::array<uint64_t, FIELD_COUNT> attributes;
    for (uint32_t i = 0; i < FIELD_COUNT; ++i) {
        attributes[i] = layout->GetAttr(thread, i).GetValue();
    }
    JSHandle<JSTaggedValue> lexicalEnv(thread, JSTaggedValue::Undefined());
    auto sibling = factory->CloneObjectLiteral(literal, lexicalEnv);
    auto checkTemplate = [&](const JSHandle<JSObject>& object) {
        EXPECT_EQ(object->GetJSHClass(), *hclass);
        EXPECT_EQ(hclass->GetObjectSize(), objectSize);
        EXPECT_EQ(hclass->NumberOfProps(), FIELD_COUNT);
        EXPECT_EQ(hclass->GetLayout(thread), layout.GetTaggedValue());
        EXPECT_EQ(hclass->GetParent(thread), parent.GetTaggedValue());
        EXPECT_FALSE(hclass->IsAllTaggedProp());
        for (uint32_t i = 0; i < FIELD_COUNT; ++i) {
            EXPECT_EQ(object->GetPropertyInlinedProps(thread, i).GetRawData(), bits[i]) << "field " << i;
            EXPECT_EQ(layout->GetAttr(thread, i).GetValue(), attributes[i]) << "field " << i;
        }
    };
    checkTemplate(literal);
    checkTemplate(sibling);

    // Transition each raw representation separately, including bit patterns that resemble
    // null pointers or heap addresses when incorrectly classified as tagged values.
    for (uint32_t changed = 0; changed < RAW_FIELD_COUNT; ++changed) {
        auto clone = factory->CloneObjectLiteral(literal, lexicalEnv);
        ASSERT_NE(*clone, *literal);
        checkTemplate(clone);
        JSHandle<JSTaggedValue> key(thread, layout->GetKey(thread, changed));
        auto attr = layout->GetAttr(thread, changed);
        attr.SetRepresentation(Representation::TAGGED);
        JSHClass::TransitionForRepChange(thread, clone, key, attr);
        // No safepoint between the representation transition and replacement of raw bits.
        clone->SetPropertyInlinedPropsWithRep(thread, changed, JSTaggedValue::False());
        JSHandle<JSHClass> changedClass(thread, clone->GetJSHClass());
        JSHandle<LayoutInfo> changedLayout(thread, changedClass->GetLayout(thread));
        ASSERT_NE(*changedClass, *hclass);
        ASSERT_NE(*changedLayout, *layout);
        EXPECT_EQ(changedClass->GetObjectSize(), objectSize);
        EXPECT_TRUE(changedClass->GetParent(thread).IsUndefined());
        EXPECT_TRUE(changedLayout->GetAttr(thread, changed).IsTaggedRep());

        instance->CollectGarbage(TriggerGCType::FULL_GC);
        checkTemplate(literal);
        checkTemplate(sibling);
        auto fresh = factory->CloneObjectLiteral(literal, lexicalEnv);
        checkTemplate(fresh);
        EXPECT_NE(*fresh, *clone);
        EXPECT_EQ(clone->GetJSHClass(), *changedClass);
        EXPECT_EQ(changedClass->GetLayout(thread), changedLayout.GetTaggedValue());
        EXPECT_TRUE(changedClass->GetParent(thread).IsUndefined());
        for (uint32_t i = 0; i < FIELD_COUNT; ++i) {
            auto expected = i == changed ? JSTaggedValue::False().GetRawData() : bits[i];
            EXPECT_EQ(clone->GetPropertyInlinedProps(thread, i).GetRawData(), expected) << "field " << i;
            EXPECT_EQ(changedLayout->GetAttr(thread, i).GetRepresentation(),
                      i == changed ? Representation::TAGGED : reps[i]);
        }
        EXPECT_FALSE(thread->HasPendingException());
    }
}
} // namespace panda::test
