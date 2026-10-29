/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "ecmascript/ecma_handle_scope.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/napi/include/jsnapi.h"
#include "ecmascript/tagged_array.h"
#include "ecmascript/tests/test_helper.h"
#include "gtest/gtest.h"

using namespace panda;

using namespace panda::ecmascript;

namespace panda::test {
class HandleScopeTest : public BaseTestWithScope<false> {
public:
    void SetUp() override
    {
        JSRuntimeOptions options;
        options.SetEnableForceGC(false);
        options.SetLogLevel("info");
        instance = JSNApi::CreateEcmaVM(options);
        ASSERT_TRUE(instance != nullptr) << "Cannot create EcmaVM";
        thread = instance->GetJSThread();
        thread->ManagedCodeBegin();
        thread->GetEcmaVM()->SetHandleScopeCheckOptionForCurrentVM(true);
        scope = new EcmaHandleScope(thread);
    }
};

static void Check(JSThread *thread, uintptr_t handleAddress)
{
    if (thread->GetEcmaVM()->IsValidOrCachedHandleAddress(handleAddress)) {
        EXPECT_EQ(*reinterpret_cast<JSTaggedType *>(handleAddress), static_cast<JSTaggedType>(g_guardAddress));
    }
}

HWTEST_F_L0(HandleScopeTest, EcmaHandleScopeTest1)
{
#if !ECMASCRIPT_ENABLE_ZAP_MEM
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    uintptr_t handleAddress = 0;
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        JSHandle<TaggedArray> objHandle(factory->NewTaggedArray(16, JSTaggedValue::Undefined(),
                                                                MemSpaceType::SEMI_SPACE));
        handleAddress = objHandle.GetAddress();
    }
    Check(thread, handleAddress);
    std::vector<uintptr_t> handleAddressRecord;
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        for (int i = 0; i < 2000; i++) {
            JSHandle<TaggedArray> objHandle(factory->NewTaggedArray(16, JSTaggedValue::Undefined(),
                                                                    MemSpaceType::SEMI_SPACE));
            handleAddressRecord.push_back(objHandle.GetAddress());
        }
    }
    for (auto address : handleAddressRecord) {
        Check(thread, address);
    }
#endif
}

HWTEST_F_L0(HandleScopeTest, EcmaHandleScopeTest2)
{
#if !ECMASCRIPT_ENABLE_ZAP_MEM
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    [[maybe_unused]] panda::ecmascript::EcmaHandleScope baseScope(thread);
    for (int i = 0; i < 2500; i++) {
        factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE);
    }
    uintptr_t handleAddress = 0;
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        JSHandle<TaggedArray> objHandle(factory->NewTaggedArray(16, JSTaggedValue::Undefined(),
                                                                MemSpaceType::SEMI_SPACE));
        handleAddress = objHandle.GetAddress();
    }
    Check(thread, handleAddress);
    std::vector<uintptr_t> handleAddressRecord;
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        for (int i = 0; i < 50; i++) {
            JSHandle<TaggedArray> objHandle(factory->NewTaggedArray(16, JSTaggedValue::Undefined(),
                                                                    MemSpaceType::SEMI_SPACE));
            handleAddressRecord.push_back(objHandle.GetAddress());
        }
    }
    for (auto address : handleAddressRecord) {
        Check(thread, address);
    }
    handleAddressRecord.clear();
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        for (int i = 0; i < 2000; i++) {
            JSHandle<TaggedArray> objHandle(factory->NewTaggedArray(16, JSTaggedValue::Undefined(),
                                                                    MemSpaceType::SEMI_SPACE));
            handleAddressRecord.push_back(objHandle.GetAddress());
        }
    }
    for (auto address : handleAddressRecord) {
        Check(thread, address);
    }
    handleAddressRecord.clear();
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        for (int i = 0; i < 4000; i++) {
            JSHandle<TaggedArray> objHandle(factory->NewTaggedArray(16, JSTaggedValue::Undefined(),
                                                                    MemSpaceType::SEMI_SPACE));
            handleAddressRecord.push_back(objHandle.GetAddress());
        }
    }
    for (auto address : handleAddressRecord) {
        Check(thread, address);
    }
#endif
}

HWTEST_F_L0(HandleScopeTest, LocalScopeTest)
{
#if !ECMASCRIPT_ENABLE_ZAP_MEM
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    [[maybe_unused]] panda::LocalScope baseScope(thread->GetEcmaVM());
    for (int i = 0; i < 2500; i++) {
        factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE);
    }
    uintptr_t localAddress = 0;
    {
        [[maybe_unused]] panda::LocalScope innerScope(thread->GetEcmaVM());
        Local<JSValueRef> objRef = JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
            factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE)));
        localAddress = reinterpret_cast<uintptr_t>(*objRef);
    }
    Check(thread, localAddress);
    std::vector<uintptr_t> localAddressRecord;
    {
        [[maybe_unused]] panda::LocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 50; i++) {
            Local<JSValueRef> objRef = JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
                factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE)));
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
    localAddressRecord.clear();
    {
        [[maybe_unused]] panda::LocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 2000; i++) {
            Local<JSValueRef> objRef = JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
                factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE)));
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
    localAddressRecord.clear();
    {
        [[maybe_unused]] panda::LocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 4000; i++) {
            Local<JSValueRef> objRef = JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
                factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE)));
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
#endif
}

HWTEST_F_L0(HandleScopeTest, EscapeLocalScopeTest)
{
#if !ECMASCRIPT_ENABLE_ZAP_MEM
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    [[maybe_unused]] panda::EscapeLocalScope baseScope(thread->GetEcmaVM());
    for (int i = 0; i < 2500; i++) {
        factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE);
    }
    uintptr_t localAddress = 0;
    {
        [[maybe_unused]] panda::EscapeLocalScope innerScope(thread->GetEcmaVM());
        Local<JSValueRef> objRef = JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
            factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE)));
        localAddress = reinterpret_cast<uintptr_t>(*objRef);
    }
    Check(thread, localAddress);
    std::vector<uintptr_t> localAddressRecord;
    Local<JSValueRef> escapeObjRef;
    {
        [[maybe_unused]] panda::EscapeLocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 50; i++) {
            Local<JSValueRef> objRef = JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
                factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE)));
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
        escapeObjRef = innerScope.Escape(JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
            factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE))));
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
    EXPECT_TRUE(JSNApiHelper::ToJSHandle(escapeObjRef)->IsTaggedArray());
    localAddressRecord.clear();
    {
        [[maybe_unused]] panda::EscapeLocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 2000; i++) {
            Local<JSValueRef> objRef = JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
                factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE)));
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
        escapeObjRef = innerScope.Escape(JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
            factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE))));
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
    EXPECT_TRUE(JSNApiHelper::ToJSHandle(escapeObjRef)->IsTaggedArray());
    localAddressRecord.clear();
    {
        [[maybe_unused]] panda::EscapeLocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 4000; i++) {
            Local<JSValueRef> objRef = JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
                factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE)));
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
        escapeObjRef = innerScope.Escape(JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(
            factory->NewTaggedArray(16, JSTaggedValue::Undefined(), MemSpaceType::SEMI_SPACE))));
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
    EXPECT_TRUE(JSNApiHelper::ToJSHandle(escapeObjRef)->IsTaggedArray());
#endif
}

HWTEST_F_L0(HandleScopeTest, EcmaHandleScopePrimitiveTest1)
{
#if !ECMASCRIPT_ENABLE_ZAP_MEM
    uintptr_t primAddress = 0;
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        JSHandle<JSTaggedValue> primHandle(thread, JSTaggedValue(42), true);
        primAddress = primHandle.GetAddress();
    }
    Check(thread, primAddress);
    std::vector<uintptr_t> primAddressRecord;
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        for (int i = 0; i < 2000; i++) {
            JSHandle<JSTaggedValue> primHandle(thread, JSTaggedValue(i), true);
            primAddressRecord.push_back(primHandle.GetAddress());
        }
    }
    for (auto address : primAddressRecord) {
        Check(thread, address);
    }
#endif
}

HWTEST_F_L0(HandleScopeTest, EcmaHandleScopePrimitiveTest2)
{
#if !ECMASCRIPT_ENABLE_ZAP_MEM
    [[maybe_unused]] panda::ecmascript::EcmaHandleScope baseScope(thread);
    for (int i = 0; i < 2500; i++) {
        JSHandle<JSTaggedValue> primHandle(thread, JSTaggedValue(i), true);
    }
    uintptr_t primAddress = 0;
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        JSHandle<JSTaggedValue> primHandle(thread, JSTaggedValue(42), true);
        primAddress = primHandle.GetAddress();
    }
    Check(thread, primAddress);
    std::vector<uintptr_t> primAddressRecord;
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        for (int i = 0; i < 50; i++) {
            JSHandle<JSTaggedValue> primHandle(thread, JSTaggedValue(i), true);
            primAddressRecord.push_back(primHandle.GetAddress());
        }
    }
    for (auto address : primAddressRecord) {
        Check(thread, address);
    }
    primAddressRecord.clear();
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        for (int i = 0; i < 2000; i++) {
            JSHandle<JSTaggedValue> primHandle(thread, JSTaggedValue(i), true);
            primAddressRecord.push_back(primHandle.GetAddress());
        }
    }
    for (auto address : primAddressRecord) {
        Check(thread, address);
    }
    primAddressRecord.clear();
    {
        [[maybe_unused]] panda::ecmascript::EcmaHandleScope innerScope(thread);
        for (int i = 0; i < 4000; i++) {
            JSHandle<JSTaggedValue> primHandle(thread, JSTaggedValue(i), true);
            primAddressRecord.push_back(primHandle.GetAddress());
        }
    }
    for (auto address : primAddressRecord) {
        Check(thread, address);
    }
#endif
}

HWTEST_F_L0(HandleScopeTest, LocalScopePrimitiveTest)
{
#if !ECMASCRIPT_ENABLE_ZAP_MEM
    [[maybe_unused]] panda::LocalScope baseScope(thread->GetEcmaVM());
    for (int i = 0; i < 2500; i++) {
        JSHandle<JSTaggedValue> primHandle(thread, JSTaggedValue(i), true);
    }
    uintptr_t localAddress = 0;
    {
        [[maybe_unused]] panda::LocalScope innerScope(thread->GetEcmaVM());
        JSHandle<JSTaggedValue> number(thread, JSTaggedValue(42), true);
        Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
        localAddress = reinterpret_cast<uintptr_t>(*objRef);
    }
    Check(thread, localAddress);
    std::vector<uintptr_t> localAddressRecord;
    {
        [[maybe_unused]] panda::LocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 50; i++) {
            JSHandle<JSTaggedValue> number(thread, JSTaggedValue(i), true);
            Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
    localAddressRecord.clear();
    {
        [[maybe_unused]] panda::LocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 2000; i++) {
            JSHandle<JSTaggedValue> number(thread, JSTaggedValue(i), true);
            Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
    localAddressRecord.clear();
    {
        [[maybe_unused]] panda::LocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 4000; i++) {
            JSHandle<JSTaggedValue> number(thread, JSTaggedValue(i), true);
            Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
#endif
}

HWTEST_F_L0(HandleScopeTest, EscapeLocalPrimitiveScopeTest)
{
#if !ECMASCRIPT_ENABLE_ZAP_MEM
    [[maybe_unused]] panda::EscapeLocalScope baseScope(thread->GetEcmaVM());
    for (int i = 0; i < 2500; i++) {
        JSHandle<JSTaggedValue> primHandle(thread, JSTaggedValue(i), true);
    }
    uintptr_t localAddress = 0;
    {
        [[maybe_unused]] panda::EscapeLocalScope innerScope(thread->GetEcmaVM());
        JSHandle<JSTaggedValue> number(thread, JSTaggedValue(42), true);
        Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
        localAddress = reinterpret_cast<uintptr_t>(*objRef);
    }
    Check(thread, localAddress);
    std::vector<uintptr_t> localAddressRecord;
    Local<NumberRef> escapeObjRef;
    {
        [[maybe_unused]] panda::EscapeLocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 50; i++) {
            JSHandle<JSTaggedValue> number(thread, JSTaggedValue(i), true);
            Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
        JSHandle<JSTaggedValue> number(thread, JSTaggedValue(42), true);
        Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
        escapeObjRef = innerScope.Escape(objRef);
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
    EXPECT_TRUE(JSNApiHelper::ToJSHandle(escapeObjRef)->IsNumber());
    localAddressRecord.clear();
    {
        [[maybe_unused]] panda::EscapeLocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 2000; i++) {
            JSHandle<JSTaggedValue> number(thread, JSTaggedValue(i), true);
            Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
        JSHandle<JSTaggedValue> number(thread, JSTaggedValue(42), true);
        Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
        escapeObjRef = innerScope.Escape(objRef);
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
    EXPECT_TRUE(JSNApiHelper::ToJSHandle(escapeObjRef)->IsNumber());
    localAddressRecord.clear();
    {
        [[maybe_unused]] panda::EscapeLocalScope innerScope(thread->GetEcmaVM());
        for (int i = 0; i < 4000; i++) {
            JSHandle<JSTaggedValue> number(thread, JSTaggedValue(i), true);
            Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
            localAddressRecord.push_back(reinterpret_cast<uintptr_t>(*objRef));
        }
        JSHandle<JSTaggedValue> number(thread, JSTaggedValue(42), true);
        Local<NumberRef> objRef = JSNApiHelper::ToLocal<NumberRef>(number);
        escapeObjRef = innerScope.Escape(objRef);
    }
    for (auto address : localAddressRecord) {
        Check(thread, address);
    }
    EXPECT_TRUE(JSNApiHelper::ToJSHandle(escapeObjRef)->IsNumber());
#endif
}
}  // namespace panda::test
