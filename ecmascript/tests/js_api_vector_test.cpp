/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "ecmascript/containers/containers_private.h"
#include "ecmascript/ecma_string.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_api/js_api_vector.h"
#include "ecmascript/js_api/js_api_vector_iterator.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_iterator.h"
#include "ecmascript/js_object-inl.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda;

using namespace panda::ecmascript;

using namespace panda::ecmascript::containers;

namespace panda::test {
class JSAPIVectorTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        GTEST_LOG_(INFO) << "SetUpTestCase";
    }

    static void TearDownTestCase()
    {
        GTEST_LOG_(INFO) << "TearDownCase";
    }

    void SetUp() override
    {
        TestHelper::CreateEcmaVMWithScope(instance, thread, scope);
    }

    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(instance, scope);
    }

    EcmaVM *instance {nullptr};
    ecmascript::EcmaHandleScope *scope {nullptr};
    JSThread *thread {nullptr};

protected:
    JSAPIVector *CreateVector()
    {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();

        JSHandle<JSTaggedValue> globalObject = env->GetJSGlobalObject();
        JSHandle<JSTaggedValue> key(factory->NewFromASCII("ArkPrivate"));
        JSHandle<JSTaggedValue> value =
            JSObject::GetProperty(thread, JSHandle<JSTaggedValue>(globalObject), key).GetValue();

        auto objCallInfo = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue::Undefined(), 6);
        objCallInfo->SetFunction(JSTaggedValue::Undefined());
        objCallInfo->SetThis(value.GetTaggedValue());
        objCallInfo->SetCallArg(0, JSTaggedValue(static_cast<int>(containers::ContainerTag::Vector)));

        [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, objCallInfo);
        JSTaggedValue result = containers::ContainersPrivate::Load(objCallInfo);
        TestHelper::TearDownFrame(thread, prev);

        JSHandle<JSTaggedValue> constructor(thread, result);
        JSHandle<JSAPIVector> vector(factory->NewJSObjectByConstructor(JSHandle<JSFunction>(constructor), constructor));
        vector->SetLength(0);
        return *vector;
    }
};

HWTEST_F_L0(JSAPIVectorTest, vectorCreate)
{
    JSAPIVector *vector = CreateVector();
    EXPECT_TRUE(vector != nullptr);
}

HWTEST_F_L0(JSAPIVectorTest, AddAndGet)
{
    constexpr uint32_t NODE_NUMBERS = 9;
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSMutableHandle<JSTaggedValue> value(thread, JSTaggedValue::Undefined());

    JSHandle<JSAPIVector> toor(thread, CreateVector());

    std::string myValue("myvalue");
    for (uint32_t i = 0; i < NODE_NUMBERS; i++) {
        std::string ivalue = myValue + std::to_string(i);
        value.Update(factory->NewFromStdString(ivalue).GetTaggedValue());
        bool result = JSAPIVector::Add(thread, toor, value);
        EXPECT_TRUE(result);
        EXPECT_EQ(JSAPIVector::Get(thread, toor, i), value.GetTaggedValue());
    }
    EXPECT_EQ(static_cast<uint32_t>(toor->GetSize()), NODE_NUMBERS);

    toor->Dump();
}

HWTEST_F_L0(JSAPIVectorTest, RemoveByIndexAndRemove)
{
    constexpr uint32_t NODE_NUMBERS = 9;
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSMutableHandle<JSTaggedValue> value(thread, JSTaggedValue::Undefined());

    JSHandle<JSAPIVector> toor(thread, CreateVector());

    std::string myValue("myvalue");
    for (uint32_t i = 0; i < NODE_NUMBERS; i++) {
        std::string ivalue = myValue + std::to_string(i);
        value.Update(factory->NewFromStdString(ivalue).GetTaggedValue());
        bool result = JSAPIVector::Add(thread, toor, value);
        EXPECT_TRUE(result);
    }

    for (int32_t i = NODE_NUMBERS - 1; i > 0; i--) {
        std::string ivalue = myValue + std::to_string(i);
        value.Update(factory->NewFromStdString(ivalue).GetTaggedValue());
        JSTaggedValue gValue = JSAPIVector::RemoveByIndex(thread, toor, i);
        EXPECT_EQ(gValue, value.GetTaggedValue());
        bool delResult = JSAPIVector::Remove(thread, toor, value);
        EXPECT_FALSE(delResult);
    }

    toor->Dump();
}

HWTEST_F_L0(JSAPIVectorTest, ClearAndisEmpty)
{
    constexpr uint32_t NODE_NUMBERS = 9;
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSMutableHandle<JSTaggedValue> value(thread, JSTaggedValue::Undefined());

    JSHandle<JSAPIVector> toor(thread, CreateVector());

    std::string myValue("myvalue");
    for (uint32_t i = 0; i < NODE_NUMBERS; i++) {
        std::string ivalue = myValue + std::to_string(i);
        value.Update(factory->NewFromStdString(ivalue).GetTaggedValue());
        bool result = JSAPIVector::Add(thread, toor, value);
        EXPECT_TRUE(result);
        EXPECT_EQ(toor->IsEmpty(), false);
    }

    JSAPIVector::Clear(thread, toor);
    EXPECT_EQ(toor->IsEmpty(), true);

    toor->Dump();
}

HWTEST_F_L0(JSAPIVectorTest, GetIndexOf)
{
    constexpr uint32_t NODE_NUMBERS = 9;
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSMutableHandle<JSTaggedValue> value(thread, JSTaggedValue::Undefined());

    JSHandle<JSAPIVector> toor(thread, CreateVector());

    std::string myValue("myvalue");
    for (uint32_t i = 0; i < NODE_NUMBERS; i++) {
        std::string ivalue = myValue + std::to_string(i);
        value.Update(factory->NewFromStdString(ivalue).GetTaggedValue());
        bool result = JSAPIVector::Add(thread, toor, value);
        EXPECT_TRUE(result);
        EXPECT_EQ(JSAPIVector::GetIndexOf(thread, toor, value), static_cast<int>(i));
    }

    toor->Dump();
}

HWTEST_F_L0(JSAPIVectorTest, GetOwnProperty)
{
    constexpr uint32_t DEFAULT_LENGTH = 8;
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSMutableHandle<JSTaggedValue> value(thread, JSTaggedValue::Undefined());
    JSHandle<JSAPIVector> toor(thread, CreateVector());

    std::string vectorvalue("vectorvalue");
    for (uint32_t i = 0; i < DEFAULT_LENGTH; i++) {
        std::string ivalue = vectorvalue + std::to_string(i);
        value.Update(factory->NewFromStdString(ivalue).GetTaggedValue());
        JSAPIVector::Add(thread, toor, value);
    }
    // test GetOwnProperty
    int testInt = 1;
    JSHandle<JSTaggedValue> vectorKey1(thread, JSTaggedValue(testInt));
    EXPECT_TRUE(JSAPIVector::GetOwnProperty(thread, toor, vectorKey1));
    testInt = 20;
    JSHandle<JSTaggedValue> vectorKey2(thread, JSTaggedValue(testInt));
    EXPECT_FALSE(JSAPIVector::GetOwnProperty(thread, toor, vectorKey2));
}

/**
 * @tc.name: GetProperty
 * @tc.desc:
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JSAPIVectorTest, GetProperty)
{
    JSHandle<JSAPIVector> toor(thread, CreateVector());
    uint32_t elementsNums = 8;
    for (uint32_t i = 0; i < elementsNums; i++) {
        JSHandle<JSTaggedValue> value(thread, JSTaggedValue(i));
        JSAPIVector::Add(thread, toor, value);
    }
    for (uint32_t i = 0; i < elementsNums; i++) {
        JSHandle<JSTaggedValue> key(thread, JSTaggedValue(i));
        OperationResult getPropertyRes = JSAPIVector::GetProperty(thread, toor, key);
        EXPECT_EQ(getPropertyRes.GetValue().GetTaggedValue(), JSTaggedValue(i));
    }
}

/**
 * @tc.name: SetProperty
 * @tc.desc:
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JSAPIVectorTest, SetProperty)
{
    JSHandle<JSAPIVector> toor(thread, CreateVector());
    uint32_t elementsNums = 8;
    for (uint32_t i = 0; i < elementsNums; i++) {
        JSHandle<JSTaggedValue> value(thread, JSTaggedValue(i));
        JSAPIVector::Add(thread, toor, value);
    }
    for (uint32_t i = 0; i < elementsNums; i++) {
        JSHandle<JSTaggedValue> key(thread, JSTaggedValue(i));
        JSHandle<JSTaggedValue> value(thread, JSTaggedValue(i * 2)); // 2 : It means double
        bool setPropertyRes = JSAPIVector::SetProperty(thread, toor, key, value);
        EXPECT_EQ(setPropertyRes, true);
    }
}

/**
 * @tc.name: TrimToCurrentLength
 * @tc.desc:
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(JSAPIVectorTest, IncreaseCapacityToTrimToCurrentLength)
{
    JSHandle<JSAPIVector> toor(thread, CreateVector());
    uint32_t elementsNums = 20;
    for (uint32_t i = 0; i < elementsNums; i++) {
        JSHandle<JSTaggedValue> value(thread, JSTaggedValue(i));
        JSAPIVector::Add(thread, toor, value);
    }
    JSAPIVector::IncreaseCapacityTo(thread, toor, 80);
    JSHandle<TaggedArray> elementData(thread, toor->GetElements());
    EXPECT_EQ(static_cast<int>(elementData->GetLength()), 80);
    JSAPIVector::TrimToCurrentLength(thread, toor);
    JSHandle<TaggedArray> newElementData(thread, toor->GetElements());
    EXPECT_EQ(newElementData->GetLength(), elementsNums);
}
}  // namespace panda::test
