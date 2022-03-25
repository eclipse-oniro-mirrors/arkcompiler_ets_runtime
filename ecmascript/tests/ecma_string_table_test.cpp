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

#include "ecmascript/ecma_string_table.h"
#include "ecmascript/tests/test_helper.h"

#include "ecmascript/mem/semi_space_collector-inl.h"

#include "ecmascript/ecma_vm.h"
#include "ecmascript/mem/clock_scope.h"
#include "ecmascript/mem/concurrent_marker.h"
#include "ecmascript/mem/mem_manager.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/mem/object_xray-inl.h"
#include "ecmascript/mem/mark_stack.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/mem/parallel_marker-inl.h"
#include "ecmascript/mem/space-inl.h"
#include "ecmascript/mem/tlab_allocator-inl.h"
#include "ecmascript/runtime_call_id.h"
#include "ecmascript/vmstat/runtime_stat.h"
using namespace panda::ecmascript;

namespace panda::test {
class EcmaStringTableTest : public testing::Test {
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

    PandaVM *instance {nullptr};
    EcmaHandleScope *scope {nullptr};
    JSThread *thread {nullptr};
};

/**
 * @tc.name: InternEmptyString
 * @tc.desc: Write empty string emptyStr to the Intern pool and takes the hash code as its index.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(EcmaStringTableTest, InternEmptyString)
{
    EcmaStringTable *table = thread->GetEcmaVM()->GetEcmaStringTable();

    JSHandle<EcmaString> emptyEcmaStrHandle(thread, EcmaString::CreateEmptyString(thread->GetEcmaVM()));
    EXPECT_TRUE(!emptyEcmaStrHandle->IsInternString());

    table->InternEmptyString(*emptyEcmaStrHandle);
    EXPECT_TRUE(emptyEcmaStrHandle->IsInternString());
}

/**
 * @tc.name: GetOrInternString
 * @tc.desc: Obtain EcmaString string from utf8 encoded data. If the string already exists in the detention pool,
             it will be returned directly. If not, it will be added to the detention pool and then returned.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(EcmaStringTableTest, GetOrInternString_utf8Data)
{
    EcmaVM *vm = thread->GetEcmaVM();
    EcmaStringTable *table = vm->GetEcmaStringTable();

    uint8_t utf8Data[] = {0x68, 0x65, 0x6c, 0x6c, 0x6f}; // " hello "
    EcmaString *ecmaStrCreatePtr = EcmaString::CreateFromUtf8(utf8Data, sizeof(utf8Data), vm, true);
    EXPECT_TRUE(!ecmaStrCreatePtr->IsInternString());

    EcmaString *ecmaStrGetPtr = table->GetOrInternString(utf8Data, sizeof(utf8Data), true);
    EXPECT_STREQ(CString(ecmaStrGetPtr->GetCString().get()).c_str(), "hello");
    EXPECT_TRUE(ecmaStrGetPtr->IsInternString());
}

/**
 * @tc.name: GetOrInternString
 * @tc.desc: Obtain EcmaString string from utf16 encoded data. If the string already exists in the detention pool,
             it will be returned directly. If not, it will be added to the detention pool and then returned.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(EcmaStringTableTest, GetOrInternString_utf16Data)
{
    EcmaVM *vm = thread->GetEcmaVM();
    EcmaStringTable *table = vm->GetEcmaStringTable();

    uint16_t utf16Data[] = {0x7F16, 0x7801, 0x89E3, 0x7801}; // “ 编码解码 ”
    EcmaString *ecmaStrCreatePtr = EcmaString::CreateFromUtf16(utf16Data, sizeof(utf16Data) / sizeof(uint16_t), vm, false);
    EXPECT_TRUE(!ecmaStrCreatePtr->IsInternString());

    EcmaString *ecmaStrGetPtr = table->GetOrInternString(utf16Data, sizeof(utf16Data) / sizeof(uint16_t), false);
    EXPECT_STREQ(ecmaStrGetPtr->GetCString().get(), "编码解码");
    EXPECT_TRUE(ecmaStrGetPtr->IsInternString());
}

/**
 * @tc.name: GetOrInternString
 * @tc.desc: Obtain EcmaString string from another EcmaString. If the string already exists in the detention pool,
             it will be returned directly. If not, it will be added to the detention pool and then returned.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(EcmaStringTableTest, GetOrInternString_EcmaString)
{
    EcmaVM *vm = thread->GetEcmaVM();
    ObjectFactory *factory = vm->GetFactory();
    EcmaStringTable *table = vm->GetEcmaStringTable();

    JSHandle<EcmaString> ecmaStrCreateHandle = factory->NewFromString("hello world");
    EXPECT_TRUE(ecmaStrCreateHandle->IsInternString());

    EcmaString *ecmaStrGetPtr = table->GetOrInternString(*ecmaStrCreateHandle);
    EXPECT_STREQ(ecmaStrGetPtr->GetCString().get(), "hello world");
    EXPECT_TRUE(ecmaStrGetPtr->IsInternString());
}

/**
 * @tc.name: SweepWeakReference
 * @tc.desc: Scan all string constants in the intern pool, erase the constants that are not referenced by the
             object, and convert the referenced object type that is not EcmaString to EcmaString type.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F_L0(EcmaStringTableTest, SweepWeakReference) // using WeakRootVisitor = std::function<TaggedObject *(TaggedObject *p)>;
{
    EcmaVM *vm = thread->GetEcmaVM();
    EcmaStringTable *table = vm->GetEcmaStringTable();
    ObjectFactory *factory = vm->GetFactory();
    auto heap = thread->GetEcmaVM()->GetHeap();

    size_t oldSizebase = heap->GetOldSpace()->GetHeapObjectSize();
    uint8_t utf8Data1[] = {0x68, 0x65, 0x6c, 0x6c, 0x6f}; // " hello "
    uint8_t utf8Data2[] = {0x77, 0x6f, 0x72, 0x6c, 0x64}; // " world "
    EcmaString *ecmaStrHandle1 = table->GetOrInternString(utf8Data1, sizeof(utf8Data1), true);
    EcmaString *ecmaStrHandle2 = table->GetOrInternString(utf8Data2, sizeof(utf8Data2), true);
    EXPECT_TRUE(ecmaStrHandle1->IsInternString() && ecmaStrHandle2->IsInternString());

    for (int i = 0; i < 1024; i++) {
        factory->NewFromUtf8(utf8Data1, sizeof(utf8Data1));
    }
    size_t oldSizeBefore = 0;
    oldSizeBefore = heap->GetOldSpace()->GetHeapObjectSize();
    EXPECT_EQ(oldSizeBefore, oldSizebase);

    WeakRootVisitor visitor = [&](TaggedObject *header) {
        return reinterpret_cast<TaggedObject *>(ToUintPtr(nullptr));
    };
    ecmaStrHandle2 = nullptr;
    table->SweepWeakReference(visitor);
    size_t oldSizeAfter = heap->GetOldSpace()->GetHeapObjectSize();
    EXPECT_EQ(oldSizeBefore, oldSizeAfter);
}
}  // namespace panda::test