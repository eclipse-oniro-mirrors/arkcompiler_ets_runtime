/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include <thread>

#include "ecmascript/ecma_vm.h"
#include "ecmascript/ic/invoke_cache.h"
#include "ecmascript/interpreter/interpreter-inl.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;
namespace panda::test {
class ICInvokeTest : public testing::Test {
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
        ecmaVm = EcmaVM::Cast(instance);
    }

    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(instance, scope);
    }

    PandaVM *instance {nullptr};
    EcmaVM *ecmaVm = nullptr;
    EcmaHandleScope *scope {nullptr};
    JSThread *thread {nullptr};
};

HWTEST_F_L0(ICInvokeTest, SetMonoConstuctCacheSlot)
{
    auto globalEnv = ecmaVm->GetGlobalEnv();
    auto factory = ecmaVm->GetFactory();
    JSHandle<JSFunction> func = factory->NewJSFunction(globalEnv);
    func.GetTaggedValue().GetTaggedObject()->GetClass()->SetClassConstructor(true);

    JSHandle<TaggedArray> array = factory->NewTaggedArray(10);
    uint32_t slotId = 5;
    bool setResult = InvokeCache::SetMonoConstuctCacheSlot(
        thread, static_cast<ProfileTypeInfo *>(*array), slotId, func.GetTaggedValue(), JSTaggedValue(123));
    ASSERT_TRUE(setResult);
    ASSERT_EQ(array->Get(thread, slotId), func.GetTaggedValue());
    ASSERT_EQ(array->Get(thread, slotId + 1), JSTaggedValue(123));
}

HWTEST_F_L0(ICInvokeTest, SetPolyConstuctCacheSlot)
{
    auto globalEnv = ecmaVm->GetGlobalEnv();
    auto factory = ecmaVm->GetFactory();
    JSHandle<TaggedArray> array1 = factory->NewTaggedArray(3);
    JSHandle<TaggedArray> array2 = factory->NewTaggedArray(3);

    JSHandle<JSFunction> func0 = factory->NewJSFunction(globalEnv);
    func0.GetTaggedValue().GetTaggedObject()->GetClass()->SetClassConstructor(true);
    array1->Set(thread, 0, func0.GetTaggedValue());
    array2->Set(thread, 0, JSTaggedValue(123));
    JSHandle<JSFunction> func1 = factory->NewJSFunction(globalEnv);
    func1.GetTaggedValue().GetTaggedObject()->GetClass()->SetClassConstructor(true);
    array1->Set(thread, 1, func1.GetTaggedValue());
    array2->Set(thread, 1, JSTaggedValue(456));
    JSHandle<JSFunction> func2 = factory->NewJSFunction(globalEnv);
    func2.GetTaggedValue().GetTaggedObject()->GetClass()->SetClassConstructor(true);
    array1->Set(thread, 2, func2.GetTaggedValue());
    array2->Set(thread, 2, JSTaggedValue(789));

    JSHandle<TaggedArray> array = factory->NewTaggedArray(10);
    uint32_t slotId = 5;
    bool setResult = InvokeCache::SetPolyConstuctCacheSlot(
        thread, static_cast<ProfileTypeInfo *>(*array), slotId, 3, array1.GetTaggedValue(), array2.GetTaggedValue());
    ASSERT_TRUE(setResult);
    JSTaggedValue slot = array->Get(thread, slotId);
    ASSERT_TRUE(slot.IsTaggedArray());
    JSHandle<TaggedArray> slotArray(thread, slot);
    ASSERT_EQ(slotArray->Get(thread, 0), func0.GetTaggedValue());
    ASSERT_EQ(slotArray->Get(thread, 1), JSTaggedValue(123));
    ASSERT_EQ(slotArray->Get(thread, 2), func1.GetTaggedValue());
    ASSERT_EQ(slotArray->Get(thread, 3), JSTaggedValue(456));
    ASSERT_EQ(slotArray->Get(thread, 4), func2.GetTaggedValue());
    ASSERT_EQ(slotArray->Get(thread, 5), JSTaggedValue(789));
    ASSERT_EQ(array->Get(thread, slotId + 1), JSTaggedValue::Hole());
}
}