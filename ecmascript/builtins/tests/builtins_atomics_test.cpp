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

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <thread>

#include "builtin_test_util.h"
#include "ecmascript/base/atomic_helper.h"
#include "ecmascript/builtins/builtins_array.h"
#include "ecmascript/builtins/builtins_atomics.h"
#include "ecmascript/builtins/builtins_shared_typedarray.h"
#include "ecmascript/builtins/builtins_sharedarraybuffer.h"
#include "ecmascript/builtins/builtins_typedarray.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_arraybuffer.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_tagged_value_wrapper.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/js_typed_array.h"
#include "ecmascript/tests/atomics_test_helper.h"
#include "ecmascript/tests/test_helper.h"
#include "ecmascript/utils/collections_atomics.h"

using namespace panda::ecmascript;
using namespace panda::ecmascript::builtins;

namespace panda::test {
using TypedArray = ecmascript::builtins::BuiltinsTypedArray;
namespace {
constexpr uint32_t SENDABLE_BUFFER_SIZE = 16;
constexpr uint32_t SENDABLE_ARRAY_LENGTH = 4;
constexpr uint32_t SMALL_WAITABLE_ARRAY_LENGTH = 2;
}

class BuiltinsAtomicsTest : public BaseTestWithScope<false> {
};

JSTypedArray *CreateTypedArray(JSThread *thread, const JSHandle<TaggedArray> &array, DataViewType type)
{
    auto vm = thread->GetEcmaVM();
    auto env = vm->GetGlobalEnv();
    JSHandle<JSTaggedValue> jsarray(JSArray::CreateArrayFromList(thread, array));
    JSHandle<JSObject> globalObject(thread, env->GetGlobalObject());
    JSHandle<JSFunction> arrayFunc;
    JSTaggedValue result = JSTaggedValue::Hole();
    switch (type) {
        case DataViewType::BIGINT64: {
            arrayFunc = JSHandle<JSFunction>(env->GetBigInt64ArrayFunction());
            auto ecmaRuntimeCallInfo1 = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue(*arrayFunc), 6);
            ecmaRuntimeCallInfo1->SetFunction(JSTaggedValue(*arrayFunc));
            ecmaRuntimeCallInfo1->SetThis(JSTaggedValue(*globalObject));
            ecmaRuntimeCallInfo1->SetCallArg(0, jsarray.GetTaggedValue());

            [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo1);
            result = TypedArray::BigInt64ArrayConstructor(ecmaRuntimeCallInfo1);
            TestHelper::TearDownFrame(thread, prev);
            break;
        }
        case DataViewType::BIGUINT64: {
            arrayFunc = JSHandle<JSFunction>(env->GetBigUint64ArrayFunction());
            auto ecmaRuntimeCallInfo1 = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue(*arrayFunc), 6);
            ecmaRuntimeCallInfo1->SetFunction(JSTaggedValue(*arrayFunc));
            ecmaRuntimeCallInfo1->SetThis(JSTaggedValue(*globalObject));
            ecmaRuntimeCallInfo1->SetCallArg(0, jsarray.GetTaggedValue());

            [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo1);
            result = TypedArray::BigUint64ArrayConstructor(ecmaRuntimeCallInfo1);
            TestHelper::TearDownFrame(thread, prev);
            break;
        }
        case DataViewType::INT16: {
            arrayFunc = JSHandle<JSFunction>(env->GetInt16ArrayFunction());
            auto ecmaRuntimeCallInfo1 = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue(*arrayFunc), 6);
            ecmaRuntimeCallInfo1->SetFunction(JSTaggedValue(*arrayFunc));
            ecmaRuntimeCallInfo1->SetThis(JSTaggedValue(*globalObject));
            ecmaRuntimeCallInfo1->SetCallArg(0, jsarray.GetTaggedValue());

            [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo1);
            result = TypedArray::Int16ArrayConstructor(ecmaRuntimeCallInfo1);
            TestHelper::TearDownFrame(thread, prev);
            break;
        }
        case DataViewType::INT32: {
            arrayFunc = JSHandle<JSFunction>(env->GetInt32ArrayFunction());
            auto ecmaRuntimeCallInfo1 = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue(*arrayFunc), 6);
            ecmaRuntimeCallInfo1->SetFunction(JSTaggedValue(*arrayFunc));
            ecmaRuntimeCallInfo1->SetThis(JSTaggedValue(*globalObject));
            ecmaRuntimeCallInfo1->SetCallArg(0, jsarray.GetTaggedValue());

            [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo1);
            result = TypedArray::Int32ArrayConstructor(ecmaRuntimeCallInfo1);
            TestHelper::TearDownFrame(thread, prev);
            break;
        }
        case DataViewType::INT8: {
            arrayFunc = JSHandle<JSFunction>(env->GetInt8ArrayFunction());
            auto ecmaRuntimeCallInfo1 = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue(*arrayFunc), 6);
            ecmaRuntimeCallInfo1->SetFunction(JSTaggedValue(*arrayFunc));
            ecmaRuntimeCallInfo1->SetThis(JSTaggedValue(*globalObject));
            ecmaRuntimeCallInfo1->SetCallArg(0, jsarray.GetTaggedValue());

            [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo1);
            result = TypedArray::Int8ArrayConstructor(ecmaRuntimeCallInfo1);
            TestHelper::TearDownFrame(thread, prev);
            break;
        }
        case DataViewType::UINT16: {
            arrayFunc = JSHandle<JSFunction>(env->GetUint16ArrayFunction());
            auto ecmaRuntimeCallInfo1 = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue(*arrayFunc), 6);
            ecmaRuntimeCallInfo1->SetFunction(JSTaggedValue(*arrayFunc));
            ecmaRuntimeCallInfo1->SetThis(JSTaggedValue(*globalObject));
            ecmaRuntimeCallInfo1->SetCallArg(0, jsarray.GetTaggedValue());

            [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo1);
            result = TypedArray::Uint16ArrayConstructor(ecmaRuntimeCallInfo1);
            TestHelper::TearDownFrame(thread, prev);
            break;
        }
        case DataViewType::UINT32: {
            arrayFunc = JSHandle<JSFunction>(env->GetUint32ArrayFunction());
            auto ecmaRuntimeCallInfo1 = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue(*arrayFunc), 6);
            ecmaRuntimeCallInfo1->SetFunction(JSTaggedValue(*arrayFunc));
            ecmaRuntimeCallInfo1->SetThis(JSTaggedValue(*globalObject));
            ecmaRuntimeCallInfo1->SetCallArg(0, jsarray.GetTaggedValue());

            [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo1);
            result = TypedArray::Uint32ArrayConstructor(ecmaRuntimeCallInfo1);
            TestHelper::TearDownFrame(thread, prev);
            break;
        }
        case DataViewType::UINT8: {
            arrayFunc = JSHandle<JSFunction>(env->GetUint8ArrayFunction());
            auto ecmaRuntimeCallInfo1 = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue(*arrayFunc), 6);
            ecmaRuntimeCallInfo1->SetFunction(JSTaggedValue(*arrayFunc));
            ecmaRuntimeCallInfo1->SetThis(JSTaggedValue(*globalObject));
            ecmaRuntimeCallInfo1->SetCallArg(0, jsarray.GetTaggedValue());

            [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo1);
            result = TypedArray::Uint8ArrayConstructor(ecmaRuntimeCallInfo1);
            TestHelper::TearDownFrame(thread, prev);
            break;
        }
        default: {
            JSHandle<JSTaggedValue> undefined(thread, JSTaggedValue::Undefined());
            arrayFunc = JSHandle<JSFunction>(undefined);
            break;
        }
    }
    EXPECT_TRUE(result.IsECMAObject());
    JSTypedArray *arr = JSTypedArray::Cast(reinterpret_cast<TaggedObject *>(result.GetRawData()));
    return arr;
}

JSTypedArray *CreateInt32TypedArray(JSThread *thread, const JSHandle<JSArrayBuffer> &arrBuf)
{
    auto ecmaVM = thread->GetEcmaVM();
    JSHandle<GlobalEnv> env = ecmaVM->GetGlobalEnv();

    JSHandle<JSFunction> int32_array(env->GetInt32ArrayFunction());
    JSHandle<JSObject> globalObject(thread, env->GetGlobalObject());
    //  6 : test case
    auto ecmaRuntimeCallInfo1 = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue(*int32_array), 6);
    ecmaRuntimeCallInfo1->SetFunction(JSTaggedValue(*int32_array));
    ecmaRuntimeCallInfo1->SetThis(JSTaggedValue(*globalObject));
    ecmaRuntimeCallInfo1->SetCallArg(0, arrBuf.GetTaggedValue());

    [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo1);
    JSTaggedValue result = TypedArray::Int32ArrayConstructor(ecmaRuntimeCallInfo1);
    TestHelper::TearDownFrame(thread, prev);

    EXPECT_TRUE(result.IsECMAObject());
    JSTypedArray *int32arr = JSTypedArray::Cast(reinterpret_cast<TaggedObject *>(result.GetRawData()));
    return int32arr;
}

JSSharedTypedArray *CreateSharedTypedArray(JSThread *thread, DataViewType type)
{
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSTaggedValue> constructor;
    switch (type) {
        case DataViewType::INT8:
            constructor = env->GetSharedInt8ArrayFunction();
            break;
        case DataViewType::UINT8:
            constructor = env->GetSharedUint8ArrayFunction();
            break;
        case DataViewType::INT16:
            constructor = env->GetSharedInt16ArrayFunction();
            break;
        case DataViewType::UINT16:
            constructor = env->GetSharedUint16ArrayFunction();
            break;
        case DataViewType::INT32:
            constructor = env->GetSharedInt32ArrayFunction();
            break;
        case DataViewType::UINT32:
            constructor = env->GetSharedUint32ArrayFunction();
            break;
        default:
            UNREACHABLE();
    }
    JSHandle<JSSharedTypedArray> array = JSHandle<JSSharedTypedArray>::Cast(
        factory->NewJSObjectByConstructor(JSHandle<JSFunction>::Cast(constructor), constructor));
    JSHandle<JSSendableArrayBuffer> buffer = factory->NewJSSendableArrayBuffer(SENDABLE_BUFFER_SIZE);
    array->SetViewedArrayBufferOrByteArray(thread, buffer.GetTaggedValue());
    array->SetContentType(type);
    array->SetByteLength(SENDABLE_BUFFER_SIZE);
    array->SetByteOffset(0);
    array->SetArrayLength(SENDABLE_ARRAY_LENGTH);
    return *array;
}

enum class AlgorithmType {
    ALGORITHM_AND,
    ALGORITHM_ADD,
    ALGORITHM_SUB,
    ALGORITHM_OR,
    ALGORITHM_XOR,
    ALGORITHM_LOAD,
    ALGORITHM_STORE,
    ALGORITHM_WAIT,
    ALGORITHM_NOTIFY,
    ALGORITHM_EXCHANGE,
    ALGORITHM_COMP_EXCHANGE,
};

using AtomicBuiltin = JSTaggedValue (*)(EcmaRuntimeCallInfo *);

static AtomicBuiltin SelectAtomicBuiltin(AtomicBuiltin builtin, bool sendable)
{
    if (!sendable) {
        return builtin;
    }
    const std::array standard = {BuiltinsAtomics::Add, BuiltinsAtomics::And,
        BuiltinsAtomics::CompareExchange, BuiltinsAtomics::Exchange, BuiltinsAtomics::Load,
        BuiltinsAtomics::Notify, BuiltinsAtomics::Or, BuiltinsAtomics::Store, BuiltinsAtomics::Sub,
        BuiltinsAtomics::Wait, BuiltinsAtomics::Xor};
    const std::array sendableMethods = {collections::CollectionsAtomics::Add,
        collections::CollectionsAtomics::And, collections::CollectionsAtomics::CompareExchange,
        collections::CollectionsAtomics::Exchange, collections::CollectionsAtomics::Load,
        collections::CollectionsAtomics::Notify, collections::CollectionsAtomics::Or,
        collections::CollectionsAtomics::Store, collections::CollectionsAtomics::Sub,
        collections::CollectionsAtomics::Wait, collections::CollectionsAtomics::Xor};
    for (size_t i = 0; i < standard.size(); i++) {
        if (builtin == standard[i]) {
            return sendableMethods[i];
        }
    }
    return builtin;
}

static AtomicBuiltin GetAtomicBuiltin(AlgorithmType type)
{
    switch (type) {
        case AlgorithmType::ALGORITHM_AND:
            return BuiltinsAtomics::And;
        case AlgorithmType::ALGORITHM_ADD:
            return BuiltinsAtomics::Add;
        case AlgorithmType::ALGORITHM_SUB:
            return BuiltinsAtomics::Sub;
        case AlgorithmType::ALGORITHM_LOAD:
            return BuiltinsAtomics::Load;
        case AlgorithmType::ALGORITHM_STORE:
            return BuiltinsAtomics::Store;
        case AlgorithmType::ALGORITHM_COMP_EXCHANGE:
            return BuiltinsAtomics::CompareExchange;
        case AlgorithmType::ALGORITHM_EXCHANGE:
            return BuiltinsAtomics::Exchange;
        case AlgorithmType::ALGORITHM_OR:
            return BuiltinsAtomics::Or;
        case AlgorithmType::ALGORITHM_XOR:
            return BuiltinsAtomics::Xor;
        case AlgorithmType::ALGORITHM_WAIT:
            return BuiltinsAtomics::Wait;
        case AlgorithmType::ALGORITHM_NOTIFY:
            return BuiltinsAtomics::Notify;
        default:
            return nullptr;
    }
}

static JSTaggedValue AtomicsAlgorithm(JSThread *thread, JSHandle<JSTaggedValue>& obj, std::vector<int32_t>& vals,
    uint32_t argLen = 8, AlgorithmType type = AlgorithmType::ALGORITHM_LOAD)
{
    auto ecmaRuntimeCallInfos = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue::Undefined(), argLen);
    ecmaRuntimeCallInfos->SetFunction(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfos->SetThis(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfos->SetCallArg(0, obj.GetTaggedValue());
    for (size_t i = 0; i < vals.size(); i++) {
        ecmaRuntimeCallInfos->SetCallArg(i+1, JSTaggedValue(vals[i]));
    }
    auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfos);
    JSTaggedValue result {JSTaggedValue::Undefined()};
    AtomicBuiltin builtin = GetAtomicBuiltin(type);
    if (builtin != nullptr) {
        result = SelectAtomicBuiltin(builtin, obj->IsSharedTypedArray())(ecmaRuntimeCallInfos);
    }
    TestHelper::TearDownFrame(thread, prev);
    return result;
}

static JSHandle<TaggedArray> CreateArrayList(JSThread *thread, std::vector<int32_t> vals, size_t maxLen)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<TaggedArray> array(factory->NewTaggedArray(maxLen));
    for (size_t i = 0; i < vals.size(); i++) {
        array->Set(thread, i, JSTaggedValue(vals[i]));
    }
    return  array;
}

HWTEST_F_L0(BuiltinsAtomicsTest, SharedIntegerTypedArrayOperations)
{
    const std::array<DataViewType, 6> types = {DataViewType::INT8, DataViewType::UINT8, DataViewType::INT16,
        DataViewType::UINT16, DataViewType::INT32, DataViewType::UINT32};
    for (DataViewType type : types) {
        JSHandle<JSTaggedValue> array(thread, CreateSharedTypedArray(thread, type));
        std::vector<int32_t> args {0, 6};
        EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 10, AlgorithmType::ALGORITHM_STORE).GetNumber(), 6);
        args = {0, 2};
        EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 10, AlgorithmType::ALGORITHM_ADD).GetInt(), 6);
        args = {0, 3};
        EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 10, AlgorithmType::ALGORITHM_SUB).GetInt(), 8);
        args = {0, 3};
        EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 10, AlgorithmType::ALGORITHM_AND).GetInt(), 5);
        args = {0, 4};
        EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 10, AlgorithmType::ALGORITHM_OR).GetInt(), 1);
        args = {0, 1};
        EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 10, AlgorithmType::ALGORITHM_XOR).GetInt(), 5);
        args = {0, 9};
        EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 10, AlgorithmType::ALGORITHM_EXCHANGE).GetInt(), 4);
        args = {0, 8, 7};
        EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 12,
            AlgorithmType::ALGORITHM_COMP_EXCHANGE).GetInt(), 9);
        args = {0, 9, 7};
        EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 12,
            AlgorithmType::ALGORITHM_COMP_EXCHANGE).GetInt(), 9);
        args = {0};
        EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 8, AlgorithmType::ALGORITHM_LOAD).GetInt(), 7);
        EXPECT_FALSE(thread->HasPendingException());
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, SharedIntegerTypedArrayWraparound)
{
    JSHandle<JSTaggedValue> int8Array(thread, CreateSharedTypedArray(thread, DataViewType::INT8));
    std::vector<int32_t> args {0, 127};
    EXPECT_EQ(AtomicsAlgorithm(thread, int8Array, args, 10, AlgorithmType::ALGORITHM_STORE).GetNumber(), 127);
    args = {0, 1};
    EXPECT_EQ(AtomicsAlgorithm(thread, int8Array, args, 10, AlgorithmType::ALGORITHM_ADD).GetInt(), 127);
    args = {0};
    EXPECT_EQ(AtomicsAlgorithm(thread, int8Array, args, 8, AlgorithmType::ALGORITHM_LOAD).GetInt(), -128);

    JSHandle<JSTaggedValue> uint8Array(thread, CreateSharedTypedArray(thread, DataViewType::UINT8));
    args = {0, 255};
    EXPECT_EQ(AtomicsAlgorithm(thread, uint8Array, args, 10, AlgorithmType::ALGORITHM_STORE).GetNumber(), 255);
    args = {0, 1};
    EXPECT_EQ(AtomicsAlgorithm(thread, uint8Array, args, 10, AlgorithmType::ALGORITHM_ADD).GetInt(), 255);
    args = {0};
    EXPECT_EQ(AtomicsAlgorithm(thread, uint8Array, args, 8, AlgorithmType::ALGORITHM_LOAD).GetInt(), 0);
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(BuiltinsAtomicsTest, Add_1)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{7, 8, 9};
    auto array = CreateArrayList(thread, arrVals, 3);

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array, DataViewType::UINT8));
    std::vector<int32_t> vals{0, 5};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_ADD);
    ASSERT_EQ(result.GetInt(), 7);
}

static JSTaggedValue AddCommon(JSThread *thread, DataViewType type, JSHandle<JSTaggedValue>& obj)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<TaggedArray> array(factory->NewTaggedArray(10)); // 10： array len

    obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array, type));
    std::vector<int32_t> vals{0, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_ADD);
    return result;
}

HWTEST_F_L0(BuiltinsAtomicsTest, Add_2)
{
    ASSERT_NE(thread, nullptr);
    JSHandle<JSTaggedValue> obj;
    auto result = AddCommon(thread, DataViewType::INT8, obj);
    ASSERT_EQ(result.GetInt(), 0);
}

HWTEST_F_L0(BuiltinsAtomicsTest, Add_3)
{
    ASSERT_NE(thread, nullptr);
    JSHandle<JSTaggedValue> obj;
    auto result = AddCommon(thread, DataViewType::UINT16, obj);
    ASSERT_EQ(result.GetInt(), 0);
    std::vector<int32_t> vals{0, 2};
    result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_ADD);
    ASSERT_EQ(result.GetInt(), 2);
}

HWTEST_F_L0(BuiltinsAtomicsTest, SubAndAdd_1)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{5, 0, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array, DataViewType::INT16));
    auto ecmaRuntimeCallInfo = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue::Undefined(), 10);
    ecmaRuntimeCallInfo->SetFunction(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetThis(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetCallArg(0, obj.GetTaggedValue());
    ecmaRuntimeCallInfo->SetCallArg(1, JSTaggedValue(static_cast<int32_t>(0)));
    ecmaRuntimeCallInfo->SetCallArg(2, JSTaggedValue(static_cast<int32_t>(2)));

    [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo);
    BuiltinsAtomics::Sub(ecmaRuntimeCallInfo);
    JSTaggedValue addResult = BuiltinsAtomics::Add(ecmaRuntimeCallInfo);
    TestHelper::TearDownFrame(thread, prev);
    ASSERT_EQ(addResult.GetInt(), 3);
}

HWTEST_F_L0(BuiltinsAtomicsTest, And_1)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{7, 0, 0};
    auto array = CreateArrayList(thread, arrVals, 10);

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array, DataViewType::INT32));
    std::vector<int32_t> vals{0, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_AND);
    ASSERT_EQ(result.GetInt(), 7);
}

HWTEST_F_L0(BuiltinsAtomicsTest, And_2)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{7, 0, 0};
    auto array = CreateArrayList(thread, arrVals, 10);

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array,
                                                                                   DataViewType::UINT32));
    std::vector<int32_t> vals{0, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_AND);
    ASSERT_EQ(result.GetInt(), 7);

    std::vector<int32_t> storeVals{0};
    result = AtomicsAlgorithm(thread, obj, storeVals, 8, AlgorithmType::ALGORITHM_LOAD);
    ASSERT_EQ(result.GetInt(), 2);
}

HWTEST_F_L0(BuiltinsAtomicsTest, CompareExchange_1)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{5, 0, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array, DataViewType::UINT8));
    std::vector<int32_t> vals{0, 5, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 12, AlgorithmType::ALGORITHM_COMP_EXCHANGE);
    ASSERT_EQ(result.GetInt(), 5);
}

HWTEST_F_L0(BuiltinsAtomicsTest, CompareExchange_2)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{5, 0, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array, DataViewType::INT8));
    std::vector<int32_t> vals{0, 5, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 12, AlgorithmType::ALGORITHM_COMP_EXCHANGE);
    ASSERT_EQ(result.GetInt(), 5);

    std::vector<int32_t> loadVals{0};
    result = AtomicsAlgorithm(thread, obj, loadVals, 8, AlgorithmType::ALGORITHM_LOAD);
    ASSERT_EQ(result.GetInt(), 2);
}

HWTEST_F_L0(BuiltinsAtomicsTest, TypedArrayCover)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{2, 0, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());
    // UINT16
    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array,
                                                                                   DataViewType::UINT16));
    std::vector<int32_t> vals{0, 2, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 12, AlgorithmType::ALGORITHM_COMP_EXCHANGE);
    ASSERT_EQ(result.GetInt(), 2);
    // INT16
    obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array, DataViewType::INT16));
    result = AtomicsAlgorithm(thread, obj, vals, 12, AlgorithmType::ALGORITHM_COMP_EXCHANGE);
    ASSERT_EQ(result.GetInt(), 2);
    // UINT32
    obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array, DataViewType::UINT32));
    result = AtomicsAlgorithm(thread, obj, vals, 12, AlgorithmType::ALGORITHM_COMP_EXCHANGE);
    ASSERT_EQ(result.GetInt(), 2);
    // INT32
    obj = JSHandle<JSTaggedValue>(thread, CreateTypedArray(thread, array, DataViewType::INT32));
    result = AtomicsAlgorithm(thread, obj, vals, 12, AlgorithmType::ALGORITHM_COMP_EXCHANGE);
    ASSERT_EQ(result.GetInt(), 2);

    // Detached Buffer
    JSTaggedValue tagged = BuiltTestUtil::CreateBuiltinsSharedArrayBuffer(thread, 0);
    JSHandle<JSArrayBuffer> arrBuf(thread, JSArrayBuffer::Cast(reinterpret_cast<TaggedObject *>(tagged.GetRawData())));
    obj = JSHandle<JSTaggedValue>(thread, CreateInt32TypedArray(thread, arrBuf));
    arrBuf->SetArrayBufferData(thread, JSTaggedValue::Null());
    result = AtomicsAlgorithm(thread, obj, vals, 12, AlgorithmType::ALGORITHM_COMP_EXCHANGE);
    EXPECT_TRUE(thread->HasPendingException());
    EXPECT_EQ(result, JSTaggedValue::Exception());
    thread->ClearException();
}

HWTEST_F_L0(BuiltinsAtomicsTest, Exchange_1)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{3, 0, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, BuiltTestUtil::CreateTypedArray(thread, array));
    std::vector<int32_t> vals{0, 6};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_EXCHANGE);
    ASSERT_EQ(result.GetInt(), 3);
}

HWTEST_F_L0(BuiltinsAtomicsTest, Exchange_2)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{3, 0, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, BuiltTestUtil::CreateTypedArray(thread, array));
    std::vector<int32_t> vals{0, 6};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_EXCHANGE);
    ASSERT_EQ(result.GetInt(), 3);

    std::vector<int32_t> loadVals{0};
    result = AtomicsAlgorithm(thread, obj, loadVals, 8, AlgorithmType::ALGORITHM_LOAD);
    ASSERT_EQ(result.GetInt(), 6);
}

HWTEST_F_L0(BuiltinsAtomicsTest, Or_1)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{5, 0, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, BuiltTestUtil::CreateTypedArray(thread, array));
    std::vector<int32_t> vals{0, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_OR);
    ASSERT_EQ(result.GetInt(), 5);
}

HWTEST_F_L0(BuiltinsAtomicsTest, Or_2)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{5, 0, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, BuiltTestUtil::CreateTypedArray(thread, array));
    std::vector<int32_t> vals{0, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 12, AlgorithmType::ALGORITHM_OR);
    ASSERT_EQ(result.GetInt(), 5);

    std::vector<int32_t> loadVals{0};
    result = AtomicsAlgorithm(thread, obj, loadVals, 8, AlgorithmType::ALGORITHM_LOAD);
    ASSERT_EQ(result.GetInt(), 7);
}

HWTEST_F_L0(BuiltinsAtomicsTest, Sub_1)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{5, 0, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, BuiltTestUtil::CreateTypedArray(thread, array));
    std::vector<int32_t> vals{0, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_SUB);
    ASSERT_EQ(result.GetInt(), 5);
}

HWTEST_F_L0(BuiltinsAtomicsTest, Sub_2)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{0, 5, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, BuiltTestUtil::CreateTypedArray(thread, array));
    std::vector<int32_t> vals{1, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_SUB);
    ASSERT_EQ(result.GetInt(), 5);

    std::vector<int32_t> loadVals{1};
    result = AtomicsAlgorithm(thread, obj, loadVals, 8, AlgorithmType::ALGORITHM_LOAD);
    ASSERT_EQ(result.GetInt(), 3);
}

HWTEST_F_L0(BuiltinsAtomicsTest, Xor_1)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{5, 7, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, BuiltTestUtil::CreateTypedArray(thread, array));
    std::vector<int32_t> vals{1, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_XOR);
    ASSERT_EQ(result.GetInt(), 7);
}

HWTEST_F_L0(BuiltinsAtomicsTest, Xor_2)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{5, 7, 0};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, BuiltTestUtil::CreateTypedArray(thread, array));
    std::vector<int32_t> vals{1, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_XOR);
    ASSERT_EQ(result.GetInt(), 7);

    std::vector<int32_t> loadVals{1};
    result = AtomicsAlgorithm(thread, obj, loadVals, 8, AlgorithmType::ALGORITHM_LOAD);
    ASSERT_EQ(result.GetInt(), 5);
}

HWTEST_F_L0(BuiltinsAtomicsTest, IsLockFree_1)
{
    auto ecmaRuntimeCallInfo = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue::Undefined(), 6);
    ecmaRuntimeCallInfo->SetFunction(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetThis(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetCallArg(0, JSTaggedValue(static_cast<int32_t>(1)));

    [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo);
    JSTaggedValue result = BuiltinsAtomics::IsLockFree(ecmaRuntimeCallInfo);
    TestHelper::TearDownFrame(thread, prev);
    ASSERT_TRUE(result.ToBoolean());
}

HWTEST_F_L0(BuiltinsAtomicsTest, IsLockFree_2)
{
    auto ecmaRuntimeCallInfo = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue::Undefined(), 6);
    ecmaRuntimeCallInfo->SetFunction(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetThis(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetCallArg(0, JSTaggedValue(static_cast<int32_t>(2)));

    [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo);
    JSTaggedValue result = BuiltinsAtomics::IsLockFree(ecmaRuntimeCallInfo);
    TestHelper::TearDownFrame(thread, prev);
    ASSERT_TRUE(result.ToBoolean());
}

HWTEST_F_L0(BuiltinsAtomicsTest, IsLockFree_3)
{
    auto ecmaRuntimeCallInfo = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue::Undefined(), 6);
    ecmaRuntimeCallInfo->SetFunction(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetThis(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetCallArg(0, JSTaggedValue(static_cast<int32_t>(4)));

    [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo);
    JSTaggedValue result = BuiltinsAtomics::IsLockFree(ecmaRuntimeCallInfo);
    TestHelper::TearDownFrame(thread, prev);
    ASSERT_TRUE(result.ToBoolean());
}

HWTEST_F_L0(BuiltinsAtomicsTest, IsLockFree_4)
{
    auto ecmaRuntimeCallInfo = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue::Undefined(), 6);
    ecmaRuntimeCallInfo->SetFunction(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetThis(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetCallArg(0, JSTaggedValue(static_cast<int32_t>(-3)));

    [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo);
    JSTaggedValue result = BuiltinsAtomics::IsLockFree(ecmaRuntimeCallInfo);
    TestHelper::TearDownFrame(thread, prev);
    ASSERT_FALSE(result.ToBoolean());
}

HWTEST_F_L0(BuiltinsAtomicsTest, IsLockFree_5)
{
    auto ecmaRuntimeCallInfo = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue::Undefined(), 6);
    ecmaRuntimeCallInfo->SetFunction(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetThis(JSTaggedValue::Undefined());
    ecmaRuntimeCallInfo->SetCallArg(0, JSTaggedValue(static_cast<int32_t>(8)));

    [[maybe_unused]] auto prev = TestHelper::SetupFrame(thread, ecmaRuntimeCallInfo);
    JSTaggedValue result = BuiltinsAtomics::IsLockFree(ecmaRuntimeCallInfo);
    TestHelper::TearDownFrame(thread, prev);
    ASSERT_TRUE(result.ToBoolean());
}

HWTEST_F_L0(BuiltinsAtomicsTest, Store_1)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{5, 6, 7};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, BuiltTestUtil::CreateTypedArray(thread, array));
    std::vector<int32_t> vals{0, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_STORE);
    ASSERT_EQ(result.GetDouble(), 2);
}

HWTEST_F_L0(BuiltinsAtomicsTest, Store_2)
{
    ASSERT_NE(thread, nullptr);
    std::vector<int32_t> arrVals{5, 6, 7};
    auto array = CreateArrayList(thread, arrVals, arrVals.size());

    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, BuiltTestUtil::CreateTypedArray(thread, array));
    std::vector<int32_t> vals{0, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_STORE);
    ASSERT_EQ(result.GetDouble(), 2);

    std::vector<int32_t> addVals{0};
    result = AtomicsAlgorithm(thread, obj, addVals, 8, AlgorithmType::ALGORITHM_ADD);
    ASSERT_EQ(result.GetInt(), 2);
}

HWTEST_F_L0(BuiltinsAtomicsTest, Wait)
{
    ASSERT_NE(thread, nullptr);
    JSTaggedValue tagged = BuiltTestUtil::CreateBuiltinsSharedArrayBuffer(thread, 4);
    JSHandle<JSArrayBuffer> arrBuf(thread, JSArrayBuffer::Cast(reinterpret_cast<TaggedObject *>(tagged.GetRawData())));
    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, CreateInt32TypedArray(thread, arrBuf));

    // Not Equal
    std::vector<int32_t> vals{0, 2, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 12, AlgorithmType::ALGORITHM_WAIT); // 12 : arg max len
    ASSERT_EQ(result, thread->GlobalConstants()->GetNotEqualString());

    // timeout
    vals[1] = 0;
    vals[2] = 100;
    result = AtomicsAlgorithm(thread, obj, vals, 12, AlgorithmType::ALGORITHM_WAIT); // 12 : arg max len
    ASSERT_EQ(result, thread->GlobalConstants()->GetTimeoutString());
}

HWTEST_F_L0(BuiltinsAtomicsTest, Notify)
{
    ASSERT_NE(thread, nullptr);
    JSTaggedValue tagged = BuiltTestUtil::CreateBuiltinsSharedArrayBuffer(thread, 4);
    JSHandle<JSArrayBuffer> arrBuf(thread, JSArrayBuffer::Cast(reinterpret_cast<TaggedObject *>(tagged.GetRawData())));
    JSHandle<JSTaggedValue> obj = JSHandle<JSTaggedValue>(thread, CreateInt32TypedArray(thread, arrBuf));

    std::vector<int32_t> vals{0, 2};
    auto result = AtomicsAlgorithm(thread, obj, vals, 10, AlgorithmType::ALGORITHM_NOTIFY);
    ASSERT_EQ(result, JSTaggedValue(0));
}

HWTEST_F_L0(BuiltinsAtomicsTest, SharedInt32WaitAndNotify)
{
    JSHandle<JSTaggedValue> array(thread, CreateSharedTypedArray(thread, DataViewType::INT32));
    std::vector<int32_t> args {0, 1, 0};
    EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 12, AlgorithmType::ALGORITHM_WAIT),
        thread->GlobalConstants()->GetNotEqualString());
    EXPECT_FALSE(thread->GetEcmaVM()->GetWaiterListNode()->waiting_);

    args = {0, 0, 0};
    EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 12, AlgorithmType::ALGORITHM_WAIT),
        thread->GlobalConstants()->GetTimeoutString());
    EXPECT_FALSE(thread->GetEcmaVM()->GetWaiterListNode()->waiting_);
    EXPECT_EQ(thread->GetEcmaVM()->GetWaiterListNode()->prev_, nullptr);
    EXPECT_EQ(thread->GetEcmaVM()->GetWaiterListNode()->next_, nullptr);
    args = {0, 1};
    EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 10, AlgorithmType::ALGORITHM_NOTIFY), JSTaggedValue(0));
    args = {0, -1};
    EXPECT_EQ(AtomicsAlgorithm(thread, array, args, 10, AlgorithmType::ALGORITHM_NOTIFY), JSTaggedValue(0));

    JSHandle<JSTaggedValue> uintArray(thread, CreateSharedTypedArray(thread, DataViewType::UINT32));
    args = {0, 0, 0};
    EXPECT_TRUE(AtomicsAlgorithm(thread, uintArray, args, 12, AlgorithmType::ALGORITHM_WAIT).IsException());
    EXPECT_TRUE(thread->HasPendingException());
    thread->ClearException();

    thread->GetEcmaVM()->SetAgentCanSuspend(false);
    EXPECT_TRUE(AtomicsAlgorithm(thread, array, args, 12, AlgorithmType::ALGORITHM_WAIT).IsException());
    EXPECT_TRUE(thread->HasPendingException());
    thread->ClearException();
    thread->GetEcmaVM()->SetAgentCanSuspend(true);
}

namespace {
JSTaggedValue CallAtomicBuiltin(JSThread *thread, AtomicBuiltin builtin, const std::vector<JSTaggedValue> &args)
{
    auto *info = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue::Undefined(), (args.size() + 2) * 2);
    info->SetFunction(JSTaggedValue::Undefined());
    info->SetThis(JSTaggedValue::Undefined());
    for (size_t i = 0; i < args.size(); i++) {
        info->SetCallArg(i, args[i]);
    }
    auto *prev = TestHelper::SetupFrame(thread, info);
    AtomicBuiltin selected = SelectAtomicBuiltin(builtin, !args.empty() && args[0].IsSharedTypedArray());
    JSTaggedValue result = selected(info);
    TestHelper::TearDownFrame(thread, prev);
    return result;
}

HWTEST_F_L0(BuiltinsAtomicsTest, CollectionsAtomicsRejectOrdinaryTypedArrays)
{
    auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, false);
    for (const base::BuiltinFunctionEntry &entry : collections::CollectionsAtomics::GetAtomicsFunctions()) {
        if (entry.GetName() == "isLockFree") {
            continue;
        }
        SCOPED_TRACE(entry.GetName().data());
        JSTaggedValue result = CallAtomicBuiltin(thread, entry.GetEntrypoint(),
            {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(0), JSTaggedValue(0)});
        EXPECT_TRUE(result.IsException());
        EXPECT_TRUE(thread->HasPendingException());
        thread->ClearException();
    }
}

void CheckCollectionsAtomicMethod(JSThread *thread, const JSHandle<JSTaggedValue> &atomics)
{
    constexpr int ADD_ARGUMENT_COUNT = 3;
    constexpr int ADD_CALL_INFO_LENGTH = 10;
    constexpr int32_t ADD_TEST_VALUE = 7;
    auto *factory = thread->GetEcmaVM()->GetFactory();
    auto methodKey = factory->NewFromASCII("add");
    auto method = JSObject::GetProperty(thread, atomics, JSHandle<JSTaggedValue>::Cast(methodKey)).GetValue();
    ASSERT_TRUE(method->IsJSFunction());
    auto lengthKey = factory->NewFromASCII("length");
    auto length = JSObject::GetProperty(thread, method, JSHandle<JSTaggedValue>::Cast(lengthKey)).GetValue();
    EXPECT_EQ(length->GetInt(), ADD_ARGUMENT_COUNT);
    auto nameKey = factory->NewFromASCII("name");
    auto name = JSObject::GetProperty(thread, method, JSHandle<JSTaggedValue>::Cast(nameKey)).GetValue();
    EXPECT_TRUE(EcmaStringAccessor::StringsAreEqual(thread,
        EcmaString::Cast(name->GetTaggedObject()), *methodKey));
    auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, true);
    auto *info = TestHelper::CreateEcmaRuntimeCallInfo(thread, JSTaggedValue::Undefined(), ADD_CALL_INFO_LENGTH);
    info->SetFunction(method.GetTaggedValue());
    info->SetThis(atomics.GetTaggedValue());
    info->SetCallArg(0, array.GetTaggedValue());
    info->SetCallArg(1, JSTaggedValue(0));
    info->SetCallArg(base::BuiltinsBase::ArgsPosition::THIRD, JSTaggedValue(ADD_TEST_VALUE));
    auto *prev = TestHelper::SetupFrame(thread, info);
    EXPECT_EQ(JSFunction::Call(info).GetInt(), 0);
    EXPECT_FALSE(thread->HasPendingException());
    TestHelper::TearDownFrame(thread, prev);
    JSHandle<JSTaggedValue> zero(thread, JSTaggedValue(0));
    EXPECT_EQ(collections::CollectionsAtomicHelper::AtomicLoad(thread, array, zero).GetInt(), ADD_TEST_VALUE);
}

HWTEST_F_L0(BuiltinsAtomicsTest, SendableAtomicsGlobalIsIndependentObject)
{
    auto env = thread->GetEcmaVM()->GetGlobalEnv();
    JSHandle<JSObject> global(thread, env->GetGlobalObject());
    auto *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSTaggedValue> key(factory->NewFromASCII("SendableAtomics"));
    PropertyDescriptor descriptor(thread);
    ASSERT_TRUE(JSObject::GetOwnProperty(thread, global, key, descriptor));
    EXPECT_TRUE(descriptor.IsWritable());
    EXPECT_FALSE(descriptor.IsEnumerable());
    EXPECT_TRUE(descriptor.IsConfigurable());
    auto atomics = descriptor.GetValue();
    ASSERT_TRUE(atomics->IsJSObject());
    EXPECT_NE(atomics.GetTaggedValue(), env->GetAtomicsFunction().GetTaggedValue());
    EXPECT_EQ(JSObject::GetProperty(thread, JSHandle<JSTaggedValue>(global), key).GetValue().GetTaggedValue(),
        atomics.GetTaggedValue());
    CheckCollectionsAtomicMethod(thread, atomics);
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(BuiltinsAtomicsTest, SendableAtomicsGlobalExistsInAnotherVM)
{
    std::thread worker([]() {
        EcmaVM *workerVm = nullptr;
        JSThread *workerThread = nullptr;
        EcmaHandleScope *workerScope = nullptr;
        TestHelper::CreateEcmaVMWithScope(workerVm, workerThread, workerScope);
        {
            auto env = workerVm->GetGlobalEnv();
            JSHandle<JSObject> global(workerThread, env->GetGlobalObject());
            JSHandle<JSTaggedValue> key(workerVm->GetFactory()->NewFromASCII("SendableAtomics"));
            PropertyDescriptor descriptor(workerThread);
            bool found = JSObject::GetOwnProperty(workerThread, global, key, descriptor);
            EXPECT_TRUE(found);
            if (found && descriptor.GetValue()->IsJSObject()) {
                EXPECT_TRUE(descriptor.IsWritable());
                EXPECT_FALSE(descriptor.IsEnumerable());
                EXPECT_TRUE(descriptor.IsConfigurable());
                CheckCollectionsAtomicMethod(workerThread, descriptor.GetValue());
            } else {
                ADD_FAILURE() << "SendableAtomics global object is missing in another VM";
            }
            EXPECT_FALSE(workerThread->HasPendingException());
        }
        TestHelper::DestroyEcmaVMWithScope(workerVm, workerScope);
    });
    ThreadNativeScope nativeScope(thread);
    worker.join();
}

HWTEST_F_L0(BuiltinsAtomicsTest, SendableAtomicsGlobalMethodsPreserveMetadata)
{
    auto env = thread->GetEcmaVM()->GetGlobalEnv();
    JSHandle<JSTaggedValue> global(thread, env->GetGlobalObject());
    auto *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSTaggedValue> key(factory->NewFromASCII("SendableAtomics"));
    auto atomics = JSObject::GetProperty(thread, global, key).GetValue();
    ASSERT_TRUE(atomics->IsJSObject());
    auto tagKey = thread->GlobalConstants()->GetHandledToStringTagSymbol();
    auto tag = JSObject::GetProperty(thread, atomics, tagKey).GetValue();
    auto expectedTag = factory->NewFromASCII("SendableAtomics");
    EXPECT_TRUE(JSTaggedValue::SameValue(thread, tag.GetTaggedValue(), expectedTag.GetTaggedValue()));
    auto originalTag = JSObject::GetProperty(thread, env->GetAtomicsFunction(), tagKey).GetValue();
    auto expectedOriginalTag = factory->NewFromASCII("Atomics");
    EXPECT_TRUE(JSTaggedValue::SameValue(thread, originalTag.GetTaggedValue(), expectedOriginalTag.GetTaggedValue()));
    for (const auto &entry : collections::CollectionsAtomics::GetAtomicsFunctions()) {
        JSHandle<JSTaggedValue> methodKey(factory->NewFromASCII(entry.GetName().data()));
        PropertyDescriptor descriptor(thread);
        ASSERT_TRUE(JSObject::GetOwnProperty(thread, JSHandle<JSObject>::Cast(atomics), methodKey, descriptor));
        EXPECT_TRUE(descriptor.IsWritable());
        EXPECT_FALSE(descriptor.IsEnumerable());
        EXPECT_TRUE(descriptor.IsConfigurable());
        auto method = descriptor.GetValue();
        ASSERT_TRUE(method->IsJSFunction());
        auto original = JSObject::GetProperty(thread, env->GetAtomicsFunction(), methodKey).GetValue();
        EXPECT_NE(method.GetTaggedValue(), original.GetTaggedValue());
        auto length = JSObject::GetProperty(thread, method, thread->GlobalConstants()->GetHandledLengthString());
        EXPECT_EQ(length.GetValue()->GetInt(), entry.GetLength());
        auto name = JSObject::GetProperty(thread, method, thread->GlobalConstants()->GetHandledNameString());
        EXPECT_TRUE(EcmaStringAccessor::StringsAreEqual(thread,
            EcmaString::Cast(name.GetValue()->GetTaggedObject()), EcmaString::Cast(methodKey->GetTaggedObject())));
    }
    EXPECT_FALSE(thread->HasPendingException());
}

JSHandle<JSTaggedValue> CreateSmallWaitableArray(JSThread *thread, bool fromOf)
{
    auto constructor = thread->GetEcmaVM()->GetGlobalEnv()->GetSharedInt32ArrayFunction();
    auto *info = TestHelper::CreateEcmaRuntimeCallInfo(thread, constructor.GetTaggedValue(), fromOf ? 8 : 6);
    info->SetFunction(constructor.GetTaggedValue());
    info->SetThis(fromOf ? constructor.GetTaggedValue() : JSTaggedValue::Undefined());
    info->SetCallArg(0, JSTaggedValue(fromOf ? 0 : SMALL_WAITABLE_ARRAY_LENGTH));
    if (fromOf) {
        info->SetCallArg(1, JSTaggedValue(0));
    }
    auto *prev = TestHelper::SetupFrame(thread, info);
    JSTaggedValue result = fromOf ? BuiltinsSharedTypedArray::Of(info) :
        BuiltinsSharedTypedArray::Int32ArrayConstructor(info);
    TestHelper::TearDownFrame(thread, prev);
    return JSHandle<JSTaggedValue>(thread, result);
}
}

HWTEST_F_L0(BuiltinsAtomicsTest, WaitableArrayKeepsOnHeapStorage)
{
    for (DataViewType type : {DataViewType::INT32, DataViewType::UINT32}) {
        auto constructor = base::TypedArrayHelper::GetSharedConstructorFromType(thread, type);
        const uint32_t boundary = JSTypedArray::MAX_ONHEAP_LENGTH / sizeof(int32_t);
        for (uint32_t length : {0U, 1U, boundary, boundary + 1}) {
            auto array = base::TypedArrayHelper::AllocateSharedTypedArray(thread,
                JSHandle<JSTaggedValue>::Cast(constructor), length, type);
            ASSERT_FALSE(thread->HasPendingException());
            auto buffer = AtomicsTestHelper::GetBuffer(thread, JSHandle<JSTaggedValue>::Cast(array));
            EXPECT_EQ(buffer->IsByteArray(), length <= boundary);
        }
    }
    auto constructor = thread->GetEcmaVM()->GetGlobalEnv()->GetInt32ArrayFunction();
    auto array = base::TypedArrayHelper::AllocateTypedArray(thread, constructor, 1, DataViewType::INT32);
    EXPECT_TRUE(AtomicsTestHelper::GetBuffer(thread, JSHandle<JSTaggedValue>::Cast(array))->IsByteArray());
}

HWTEST_F_L0(BuiltinsAtomicsTest, SmallSharedInt32ArrayWaitResults)
{
    for (bool fromOf : {false, true}) {
        auto array = CreateSmallWaitableArray(thread, fromOf);
        ASSERT_FALSE(thread->HasPendingException());
        ASSERT_TRUE(array->IsSharedTypedArray());
        EXPECT_TRUE(AtomicsTestHelper::GetBuffer(thread, array)->IsByteArray());
        EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
            {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(1), JSTaggedValue(0)}),
            thread->GlobalConstants()->GetNotEqualString());
        ASSERT_FALSE(thread->HasPendingException());
        EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
            {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(0), JSTaggedValue(1)}),
            thread->GlobalConstants()->GetTimeoutString());
        EXPECT_EQ(thread->GetEcmaVM()->GetWaiterListNode()->arrayHandle_, 0U);
        EXPECT_FALSE(thread->GetEcmaVM()->GetWaiterListNode()->waiting_);
        {
            MutexGuard guard(Singleton<Mutex>::GetInstance());
            EXPECT_TRUE(Singleton<WaiterList>::GetInstance()->sharedWaiters_.empty());
        }
        EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
            {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(1)}), JSTaggedValue(0));
        EXPECT_FALSE(thread->HasPendingException());
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, ReadModifyWriteRejectsInvalidArrayTypesAndIndices)
{
    const std::array<AtomicBuiltin, 7> operations = {BuiltinsAtomics::Add, BuiltinsAtomics::Sub,
        BuiltinsAtomics::And, BuiltinsAtomics::Or, BuiltinsAtomics::Xor,
        BuiltinsAtomics::Exchange, BuiltinsAtomics::CompareExchange};
    for (AtomicBuiltin operation : operations) {
        EXPECT_TRUE(CallAtomicBuiltin(thread, operation,
            {JSTaggedValue::Null(), JSTaggedValue(0), JSTaggedValue(1), JSTaggedValue(2)}).IsException());
        EXPECT_EXCEPTION();
        for (bool sendable : {false, true}) {
            auto invalid = AtomicsTestHelper::CreateArray(thread, DataViewType::FLOAT32, sendable);
            EXPECT_TRUE(CallAtomicBuiltin(thread, operation,
                {invalid.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(1), JSTaggedValue(2)}).IsException());
            EXPECT_EXCEPTION();
            auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, sendable);
            for (JSTaggedValue index : {JSTaggedValue(2), thread->GlobalConstants()->GetIteratorSymbol()}) {
                EXPECT_TRUE(CallAtomicBuiltin(thread, operation,
                    {array.GetTaggedValue(), index, JSTaggedValue(1), JSTaggedValue(2)}).IsException());
                EXPECT_EXCEPTION();
            }
        }
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, ReadModifyWriteRejectsDetachmentDuringIndexConversion)
{
    const std::array<AtomicBuiltin, 7> operations = {BuiltinsAtomics::Add, BuiltinsAtomics::Sub,
        BuiltinsAtomics::And, BuiltinsAtomics::Or, BuiltinsAtomics::Xor,
        BuiltinsAtomics::Exchange, BuiltinsAtomics::CompareExchange};
    JSHandle<JSTaggedValue> zero(thread, JSTaggedValue(0));
    for (bool sendable : {false, true}) {
        for (AtomicBuiltin operation : operations) {
            auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, sendable);
            auto index = AtomicsTestHelper::CreateDetachingValue(thread, array, zero);
            EXPECT_TRUE(CallAtomicBuiltin(thread, operation,
                {array.GetTaggedValue(), index.GetTaggedValue(), JSTaggedValue(1), JSTaggedValue(2)}).IsException());
            EXPECT_EXCEPTION();
        }
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, CompareExchangeRejectsDetachmentDuringReplacementConversion)
{
    const std::array<DataViewType, 8> types = {DataViewType::INT8, DataViewType::UINT8,
        DataViewType::INT16, DataViewType::UINT16, DataViewType::INT32, DataViewType::UINT32,
        DataViewType::BIGINT64, DataViewType::BIGUINT64};
    for (bool sendable : {false, true}) {
        for (DataViewType type : types) {
            bool bigInt = type == DataViewType::BIGINT64 || type == DataViewType::BIGUINT64;
            if (sendable && bigInt) {
                continue;
            }
            auto array = AtomicsTestHelper::CreateArray(thread, type, sendable);
            JSHandle<JSTaggedValue> zero = bigInt ? JSHandle<JSTaggedValue>(BigInt::Int64ToBigInt(thread, 0)) :
                JSHandle<JSTaggedValue>(thread, JSTaggedValue(0));
            auto replacement = AtomicsTestHelper::CreateDetachingValue(thread, array, zero);
            EXPECT_TRUE(CallAtomicBuiltin(thread, BuiltinsAtomics::CompareExchange,
                {array.GetTaggedValue(), JSTaggedValue(0), zero.GetTaggedValue(),
                replacement.GetTaggedValue()}).IsException());
            EXPECT_EXCEPTION();
        }
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, ReadModifyWriteRejectsInitiallyDetachedBuffers)
{
    for (bool sendable : {false, true}) {
        auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, sendable);
        AtomicsTestHelper::DetachBuffer(thread, array);
        EXPECT_TRUE(CallAtomicBuiltin(thread, BuiltinsAtomics::Add,
            {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(1)}).IsException());
        EXPECT_EXCEPTION();
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, WaitNotifyBufferAndArgumentCombinations)
{
    for (bool sendable : {false, true}) {
        auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, sendable);
        auto result = CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
            {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(1), JSTaggedValue(0)});
        if (sendable) {
            EXPECT_EQ(result, thread->GlobalConstants()->GetNotEqualString());
            EXPECT_FALSE(thread->HasPendingException());
            EXPECT_TRUE(CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
                {array.GetTaggedValue(), JSTaggedValue(2), JSTaggedValue(0), JSTaggedValue(0)}).IsException());
            EXPECT_EXCEPTION();
            EXPECT_TRUE(CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
                {array.GetTaggedValue(), JSTaggedValue(0), thread->GlobalConstants()->GetIteratorSymbol(),
                JSTaggedValue(0)}).IsException());
            EXPECT_EXCEPTION();
            EXPECT_TRUE(CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
                {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(0),
                thread->GlobalConstants()->GetIteratorSymbol()}).IsException());
            EXPECT_EXCEPTION();
        } else {
            EXPECT_TRUE(result.IsException());
            EXPECT_EXCEPTION();
        }
        EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
            {array.GetTaggedValue(), JSTaggedValue(0)}), JSTaggedValue(0));
        EXPECT_TRUE(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
            {array.GetTaggedValue(), JSTaggedValue(2), JSTaggedValue(1)}).IsException());
        EXPECT_EXCEPTION();
        EXPECT_TRUE(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
            {array.GetTaggedValue(), JSTaggedValue(0), thread->GlobalConstants()->GetIteratorSymbol()}).IsException());
        EXPECT_EXCEPTION();
    }
    for (AtomicBuiltin operation : {BuiltinsAtomics::Wait, BuiltinsAtomics::Notify}) {
        EXPECT_TRUE(CallAtomicBuiltin(thread, operation,
            {JSTaggedValue::Null(), JSTaggedValue(0), JSTaggedValue(0), JSTaggedValue(0)}).IsException());
        EXPECT_EXCEPTION();
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, SharedWaitTimeoutAndMismatchCombinations)
{
    auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, true);
    const std::array<JSTaggedValue, 5> timeouts = {JSTaggedValue::Undefined(),
        JSTaggedValue(base::NAN_VALUE), JSTaggedValue(base::POSITIVE_INFINITY),
        JSTaggedValue(-base::POSITIVE_INFINITY), JSTaggedValue(-1)};
    for (JSTaggedValue timeout : timeouts) {
        EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
            {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(1), timeout}),
            thread->GlobalConstants()->GetNotEqualString());
        EXPECT_FALSE(thread->GetEcmaVM()->GetWaiterListNode()->waiting_);
        EXPECT_FALSE(thread->HasPendingException());
    }
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
        {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(0), JSTaggedValue(-1)}),
        thread->GlobalConstants()->GetTimeoutString());
    EXPECT_FALSE(thread->GetEcmaVM()->GetWaiterListNode()->waiting_);
    EXPECT_EQ(thread->GetEcmaVM()->GetWaiterListNode()->prev_, nullptr);
    EXPECT_EQ(thread->GetEcmaVM()->GetWaiterListNode()->next_, nullptr);
}

HWTEST_F_L0(BuiltinsAtomicsTest, BigIntReadModifyWriteOperations)
{
    const std::array<AtomicBuiltin, 7> operations = {BuiltinsAtomics::Add, BuiltinsAtomics::Sub,
        BuiltinsAtomics::And, BuiltinsAtomics::Or, BuiltinsAtomics::Xor,
        BuiltinsAtomics::Exchange, BuiltinsAtomics::CompareExchange};
    const std::array<int64_t, 7> expected = {1, -1, 0, 1, 1, 1, 1};
    JSHandle<JSTaggedValue> zero(BigInt::Int64ToBigInt(thread, 0));
    JSHandle<JSTaggedValue> one(BigInt::Int64ToBigInt(thread, 1));
    for (DataViewType type : {DataViewType::BIGINT64, DataViewType::BIGUINT64}) {
        for (size_t i = 0; i < operations.size(); i++) {
            auto array = AtomicsTestHelper::CreateArray(thread, type, false);
            JSHandle<JSTaggedValue> operand = operations[i] == BuiltinsAtomics::CompareExchange ? zero : one;
            std::vector<JSTaggedValue> args = {array.GetTaggedValue(), JSTaggedValue(0), operand.GetTaggedValue()};
            if (operations[i] == BuiltinsAtomics::CompareExchange) {
                args.push_back(one.GetTaggedValue());
            }
            JSHandle<JSTaggedValue> oldValue(thread, CallAtomicBuiltin(thread, operations[i], args));
            ASSERT_TRUE(oldValue->IsBigInt());
            EXPECT_EQ(JSHandle<BigInt>::Cast(oldValue)->ToInt64(), 0);
            JSHandle<JSTaggedValue> newValue(thread, CallAtomicBuiltin(thread, BuiltinsAtomics::Load,
                {array.GetTaggedValue(), JSTaggedValue(0)}));
            ASSERT_TRUE(newValue->IsBigInt());
            EXPECT_EQ(JSHandle<BigInt>::Cast(newValue)->ToInt64(), expected[i]);
            EXPECT_FALSE(thread->HasPendingException());
        }
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, ReadModifyWriteOperandConversionErrors)
{
    const std::array<DataViewType, 8> types = {DataViewType::INT8, DataViewType::UINT8,
        DataViewType::INT16, DataViewType::UINT16, DataViewType::INT32, DataViewType::UINT32,
        DataViewType::BIGINT64, DataViewType::BIGUINT64};
    const std::array<AtomicBuiltin, 7> operations = {BuiltinsAtomics::Add, BuiltinsAtomics::Sub,
        BuiltinsAtomics::And, BuiltinsAtomics::Or, BuiltinsAtomics::Xor,
        BuiltinsAtomics::Exchange, BuiltinsAtomics::CompareExchange};
    JSHandle<JSTaggedValue> symbol(thread, thread->GlobalConstants()->GetIteratorSymbol());
    for (bool sendable : {false, true}) {
        for (DataViewType type : types) {
            bool bigInt = type == DataViewType::BIGINT64 || type == DataViewType::BIGUINT64;
            if (sendable && bigInt) {
                continue;
            }
            auto array = AtomicsTestHelper::CreateArray(thread, type, sendable);
            JSHandle<JSTaggedValue> zero = bigInt ? JSHandle<JSTaggedValue>(BigInt::Int64ToBigInt(thread, 0)) :
                JSHandle<JSTaggedValue>(thread, JSTaggedValue(0));
            for (AtomicBuiltin operation : operations) {
                EXPECT_TRUE(CallAtomicBuiltin(thread, operation,
                    {array.GetTaggedValue(), JSTaggedValue(0), symbol.GetTaggedValue()}).IsException());
                EXPECT_EXCEPTION();
            }
            EXPECT_TRUE(CallAtomicBuiltin(thread, BuiltinsAtomics::CompareExchange,
                {array.GetTaggedValue(), JSTaggedValue(0), zero.GetTaggedValue(),
                symbol.GetTaggedValue()}).IsException());
            EXPECT_EXCEPTION();
            JSHandle<JSTaggedValue> unchanged(thread, CallAtomicBuiltin(thread, BuiltinsAtomics::Load,
                {array.GetTaggedValue(), JSTaggedValue(0)}));
            if (bigInt) {
                ASSERT_TRUE(unchanged->IsBigInt());
                EXPECT_EQ(JSHandle<BigInt>::Cast(unchanged)->ToInt64(), 0);
            } else {
                EXPECT_EQ(unchanged->GetNumber(), 0);
            }
        }
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, BigIntReadModifyWriteRejectsOperandDetachment)
{
    JSHandle<JSTaggedValue> zero(BigInt::Int64ToBigInt(thread, 0));
    for (DataViewType type : {DataViewType::BIGINT64, DataViewType::BIGUINT64}) {
        auto array = AtomicsTestHelper::CreateArray(thread, type, false);
        auto operand = AtomicsTestHelper::CreateDetachingValue(thread, array, zero);
        EXPECT_TRUE(CallAtomicBuiltin(thread, BuiltinsAtomics::Add,
            {array.GetTaggedValue(), JSTaggedValue(0), operand.GetTaggedValue()}).IsException());
        EXPECT_EXCEPTION();
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, BigIntWaitAndNotify)
{
    auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::BIGINT64, false);
    auto *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSArrayBuffer> buffer = factory->NewJSSharedArrayBuffer(2 * sizeof(int64_t));
    JSHandle<JSTypedArray>::Cast(array)->SetViewedArrayBufferOrByteArray(thread, buffer.GetTaggedValue());
    JSHandle<JSTaggedValue> zero(BigInt::Int64ToBigInt(thread, 0));
    JSHandle<JSTaggedValue> one(BigInt::Int64ToBigInt(thread, 1));
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
        {array.GetTaggedValue(), JSTaggedValue(0), one.GetTaggedValue()}),
        thread->GlobalConstants()->GetNotEqualString());
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
        {array.GetTaggedValue(), JSTaggedValue(0), zero.GetTaggedValue(), JSTaggedValue(0)}),
        thread->GlobalConstants()->GetTimeoutString());
    EXPECT_TRUE(CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
        {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(0), JSTaggedValue(0)}).IsException());
    EXPECT_EXCEPTION();
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
        {array.GetTaggedValue(), JSTaggedValue(0)}), JSTaggedValue(0));
    EXPECT_FALSE(thread->GetEcmaVM()->GetWaiterListNode()->waiting_);
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(BuiltinsAtomicsTest, NotifyHonorsCountAndSkipsInactiveWaiters)
{
    auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, false);
    auto sharedBuffer = thread->GetEcmaVM()->GetFactory()->NewJSSharedArrayBuffer(8);
    JSHandle<JSTypedArray>::Cast(array)->SetViewedArrayBufferOrByteArray(thread, sharedBuffer.GetTaggedValue());
    JSTaggedValue buffer = JSHandle<JSTypedArray>::Cast(array)->GetViewedArrayBufferOrByteArray(thread);
    void *data = BuiltinsArrayBuffer::GetDataPointFromBuffer(thread, buffer);
    std::array<WaiterListNode, 3> nodes;
    auto *waiters = Singleton<WaiterList>::GetInstance();
    auto *mutex = Singleton<Mutex>::GetInstance();
    {
        MutexGuard guard(mutex);
        for (size_t i = 0; i < nodes.size(); i++) {
            nodes[i].date_ = data;
            nodes[i].index_ = 0;
            nodes[i].waitPointer_ = static_cast<int8_t *>(data);
            nodes[i].waiting_ = i != 1;
            waiters->AddNode(&nodes[i]);
        }
    }
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
        {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(0)}), JSTaggedValue(0));
    EXPECT_TRUE(nodes[0].waiting_);
    EXPECT_TRUE(nodes[2].waiting_);
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
        {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(1)}), JSTaggedValue(1));
    EXPECT_FALSE(nodes[0].waiting_);
    EXPECT_TRUE(nodes[2].waiting_);
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
        {array.GetTaggedValue(), JSTaggedValue(0)}), JSTaggedValue(1));
    EXPECT_FALSE(nodes[2].waiting_);
    {
        MutexGuard guard(mutex);
        for (auto &node : nodes) {
            waiters->DeleteNode(&node);
        }
        EXPECT_EQ(waiters->locationListMap_.find(static_cast<int8_t *>(data)), waiters->locationListMap_.end());
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, SharedNotifyHonorsCountAndSkipsInactiveWaiters)
{
    auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, true);
    std::array<WaiterListNode, 3> nodes;
    auto *waiters = Singleton<WaiterList>::GetInstance();
    auto *mutex = Singleton<Mutex>::GetInstance();
    {
        MutexGuard guard(mutex);
        for (size_t i = 0; i < nodes.size(); i++) {
            nodes[i].arrayHandle_ = array.GetAddress();
            nodes[i].index_ = 0;
            nodes[i].waiting_ = i != 1;
            waiters->AddNode(&nodes[i]);
        }
    }
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
        {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(0)}), JSTaggedValue(0));
    EXPECT_TRUE(nodes[0].waiting_);
    EXPECT_TRUE(nodes[2].waiting_);
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
        {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(1)}), JSTaggedValue(1));
    EXPECT_FALSE(nodes[0].waiting_);
    EXPECT_TRUE(nodes[2].waiting_);
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
        {array.GetTaggedValue(), JSTaggedValue(0)}), JSTaggedValue(1));
    EXPECT_FALSE(nodes[2].waiting_);
    {
        MutexGuard guard(mutex);
        for (auto &node : nodes) {
            waiters->DeleteNode(&node);
        }
        EXPECT_TRUE(waiters->sharedWaiters_.empty());
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, SharedNotifySkipsOtherLocationsAndDetachedBuffers)
{
    auto array = CreateSmallWaitableArray(thread, true);
    auto otherArray = CreateSmallWaitableArray(thread, true);
    auto detachedArray = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, true);
    AtomicsTestHelper::DetachBuffer(thread, detachedArray);
    std::array<WaiterListNode, 4> nodes;
    const std::array<uintptr_t, 4> handles = {
        array.GetAddress(), otherArray.GetAddress(), detachedArray.GetAddress(), array.GetAddress()};
    auto *waiters = Singleton<WaiterList>::GetInstance();
    {
        MutexGuard guard(Singleton<Mutex>::GetInstance());
        for (size_t i = 0; i < nodes.size(); i++) {
            nodes[i].arrayHandle_ = handles[i];
            nodes[i].index_ = i == 0 ? sizeof(int32_t) : 0;
            nodes[i].waiting_ = true;
            waiters->AddNode(&nodes[i]);
        }
    }
    EXPECT_EQ(CallAtomicBuiltin(thread, BuiltinsAtomics::Notify,
        {array.GetTaggedValue(), JSTaggedValue(0)}), JSTaggedValue(1));
    EXPECT_TRUE(nodes[0].waiting_);
    EXPECT_TRUE(nodes[1].waiting_);
    EXPECT_TRUE(nodes[2].waiting_);
    EXPECT_FALSE(nodes[3].waiting_);
    {
        MutexGuard guard(Singleton<Mutex>::GetInstance());
        for (auto &node : nodes) {
            waiters->DeleteNode(&node);
        }
        EXPECT_TRUE(waiters->sharedWaiters_.empty());
    }
}

namespace {
struct WaitNotifyContext {
    uintptr_t arrayHandle {0};
    bool collectGarbage {false};
    bool promoteBuffer {false};
    bool backingMoved {false};
    JSTaggedValue wrongOffsetNotified {JSTaggedValue::Undefined()};
    WaiterListNode *waitNode {nullptr};
    std::promise<void> ready;
    JSTaggedValue notified {JSTaggedValue::Undefined()};
    std::atomic<bool> waitFinished {false};
};

bool WaitForRegisteredNode(JSThread *thread, const WaitNotifyContext &context)
{
    ThreadNativeScope nativeScope(thread);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!context.waitFinished.load() && std::chrono::steady_clock::now() < deadline) {
        {
            MutexGuard guard(Singleton<Mutex>::GetInstance());
            auto &waiters = Singleton<WaiterList>::GetInstance()->sharedWaiters_;
            if (std::find(waiters.begin(), waiters.end(), context.waitNode) != waiters.end()) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

JSHandle<JSTaggedValue> CreatePromotedWaitView(JSThread *thread, const JSHandle<JSTaggedValue> &array,
                                               uint32_t byteOffset)
{
    JSHandle<JSTaggedValue> buffer(thread,
        JSSharedTypedArray::GetSharedOffHeapBuffer(thread, JSHandle<JSSharedTypedArray>::Cast(array)));
    auto constructor = thread->GetEcmaVM()->GetGlobalEnv()->GetSharedInt32ArrayFunction();
    auto *info = TestHelper::CreateEcmaRuntimeCallInfo(thread, constructor.GetTaggedValue(), 8);
    info->SetFunction(constructor.GetTaggedValue());
    info->SetThis(JSTaggedValue::Undefined());
    info->SetCallArg(0, buffer.GetTaggedValue());
    info->SetCallArg(1, JSTaggedValue(byteOffset));
    auto *prev = TestHelper::SetupFrame(thread, info);
    JSTaggedValue result = BuiltinsSharedTypedArray::Int32ArrayConstructor(info);
    TestHelper::TearDownFrame(thread, prev);
    return JSHandle<JSTaggedValue>(thread, result);
}

void RunWaitNotifier(WaitNotifyContext &context)
{
    EcmaVM *workerVm = nullptr;
    JSThread *workerThread = nullptr;
    EcmaHandleScope *workerScope = nullptr;
    TestHelper::CreateEcmaVMWithScope(workerVm, workerThread, workerScope);
    JSHandle<JSTaggedValue> workerArray(workerThread,
        JSTaggedValue(Barriers::GetTaggedValue(workerThread, context.arrayHandle)));
    context.ready.set_value();
    if (WaitForRegisteredNode(workerThread, context)) {
        if (context.collectGarbage) {
            auto oldBuffer = AtomicsTestHelper::GetBuffer(workerThread, workerArray).GetTaggedValue().GetRawData();
            SharedHeap::GetInstance()->CollectGarbage<TriggerGCType::SHARED_FULL_GC, GCReason::OTHER>(workerThread);
            auto newBuffer = AtomicsTestHelper::GetBuffer(workerThread, workerArray).GetTaggedValue().GetRawData();
            context.backingMoved = oldBuffer != newBuffer;
        }
        if (context.promoteBuffer) {
            auto otherElement = CreatePromotedWaitView(workerThread, workerArray, sizeof(int32_t));
            context.wrongOffsetNotified = CallAtomicBuiltin(workerThread, BuiltinsAtomics::Notify,
                {otherElement.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(1)});
            workerArray = CreatePromotedWaitView(workerThread, workerArray, 0);
        }
        context.notified = CallAtomicBuiltin(workerThread, BuiltinsAtomics::Notify,
            {workerArray.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(1)});
    }
    // Release an indefinite wait on failure so the assertions can report the regression.
    if (context.notified != JSTaggedValue(1)) {
        MutexGuard guard(Singleton<Mutex>::GetInstance());
        context.waitNode->waiting_ = false;
        context.waitNode->cond_.Signal();
    }
    TestHelper::DestroyEcmaVMWithScope(workerVm, workerScope);
}
}  // namespace

namespace {
void VerifySharedWait(JSThread *thread, bool fromOf, JSTaggedValue timeout, bool collectGarbage, bool promoteBuffer)
{
    auto array = CreateSmallWaitableArray(thread, fromOf);
    ASSERT_FALSE(thread->HasPendingException());
    ASSERT_TRUE(array->IsSharedTypedArray());
    WaitNotifyContext context;
    context.arrayHandle = array.GetAddress();
    context.collectGarbage = collectGarbage;
    context.promoteBuffer = promoteBuffer;
    context.waitNode = thread->GetEcmaVM()->GetWaiterListNode();
    auto readyFuture = context.ready.get_future();
    std::thread notifier(RunWaitNotifier, std::ref(context));
    {
        ThreadNativeScope nativeScope(thread);
        readyFuture.wait();
    }
    JSTaggedValue result = CallAtomicBuiltin(thread, BuiltinsAtomics::Wait,
        {array.GetTaggedValue(), JSTaggedValue(0), JSTaggedValue(0), timeout});
    context.waitFinished.store(true);
    {
        ThreadNativeScope nativeScope(thread);
        notifier.join();
    }
    EXPECT_EQ(result, thread->GlobalConstants()->GetOkString());
    ASSERT_FALSE(thread->HasPendingException());
    EXPECT_EQ(context.notified, JSTaggedValue(1));
    EXPECT_EQ(context.backingMoved, collectGarbage);
    if (promoteBuffer) {
        EXPECT_EQ(context.wrongOffsetNotified, JSTaggedValue(0));
    }
    EXPECT_FALSE(context.waitNode->waiting_);
    EXPECT_EQ(context.waitNode->arrayHandle_, 0U);
    EXPECT_EQ(context.waitNode->date_, nullptr);
    EXPECT_EQ(context.waitNode->waitPointer_, nullptr);
    {
        MutexGuard guard(Singleton<Mutex>::GetInstance());
        EXPECT_TRUE(Singleton<WaiterList>::GetInstance()->sharedWaiters_.empty());
    }
    EXPECT_EQ(AtomicsTestHelper::GetBuffer(thread, array)->IsByteArray(), !promoteBuffer);
}
}  // namespace

HWTEST_F_L0(BuiltinsAtomicsTest, SharedWaitIsNotifiedAndClearsQueue)
{
    for (bool fromOf : {false, true}) {
        for (JSTaggedValue timeout : {JSTaggedValue(1000), JSTaggedValue::Undefined()}) {
            VerifySharedWait(thread, fromOf, timeout, false, false);
        }
    }
}

HWTEST_F_L0(BuiltinsAtomicsTest, SharedWaitSurvivesGCAndBufferPromotion)
{
    for (bool promoteBuffer : {false, true}) {
        VerifySharedWait(thread, true, JSTaggedValue::Undefined(), true, promoteBuffer);
    }
}
}

namespace panda::test {
namespace {
using AtomicArgument = base::BuiltinsBase::ArgsPosition;
constexpr int32_t PROMOTED_BUFFER_VALUE = 11;
constexpr int32_t ATOMIC_OPERAND_VALUE = 7;
constexpr int32_t ATOMIC_SUM_VALUE = PROMOTED_BUFFER_VALUE + ATOMIC_OPERAND_VALUE;

JSTaggedValue PromoteBufferOnValueOf(EcmaRuntimeCallInfo *argv)
{
    auto *thread = argv->GetThread();
    EcmaHandleScope scope(thread);
    auto object = base::BuiltinsBase::GetThis(argv);
    auto array = AtomicsTestHelper::GetProperty(thread, object, "array");
    auto result = AtomicsTestHelper::GetProperty(thread, object, "result");
    JSSharedTypedArray::GetSharedOffHeapBuffer(thread, JSHandle<JSSharedTypedArray>::Cast(array));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // Change the promoted storage so a stale ByteArray read also produces an observable failure.
    JSHandle<JSTaggedValue> index(thread, JSTaggedValue(0));
    JSHandle<JSTaggedValue> seed(thread, JSTaggedValue(PROMOTED_BUFFER_VALUE));
    collections::CollectionsAtomicHelper::AtomicStore(thread, array, index, seed);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    return result.GetTaggedValue();
}

JSHandle<JSTaggedValue> CreatePromotingValue(JSThread *thread, const JSHandle<JSTaggedValue> &array,
    int32_t result)
{
    auto object = AtomicsTestHelper::CreateValueOfObject(thread);
    auto env = thread->GetEcmaVM()->GetGlobalEnv();
    JSHandle<JSTaggedValue> valueOf(thread->GetEcmaVM()->GetFactory()->NewJSFunction(
        env, reinterpret_cast<void *>(PromoteBufferOnValueOf)));
    JSObject::SetProperty(thread, object, thread->GlobalConstants()->GetHandledValueOfString(), valueOf);
    AtomicsTestHelper::SetProperty(thread, object, "array", array);
    AtomicsTestHelper::SetProperty(thread, object, "result", JSHandle<JSTaggedValue>(thread, JSTaggedValue(result)));
    return object;
}

void VerifyAtomicBufferPromotion(JSThread *thread, AtomicBuiltin operation, uint32_t callbackArgument,
    int32_t expectedResult, int32_t expectedStored)
{
    auto array = CreateSmallWaitableArray(thread, true);
    ASSERT_FALSE(thread->HasPendingException());
    ASSERT_TRUE(AtomicsTestHelper::GetBuffer(thread, array)->IsByteArray());
    auto callback = CreatePromotingValue(thread, array,
        callbackArgument == AtomicArgument::SECOND ? 0 : ATOMIC_OPERAND_VALUE);
    std::vector<JSTaggedValue> arguments = {
        array.GetTaggedValue(), JSTaggedValue(0),
        JSTaggedValue(ATOMIC_OPERAND_VALUE), JSTaggedValue(ATOMIC_OPERAND_VALUE)};
    if (operation == collections::CollectionsAtomics::CompareExchange) {
        arguments[AtomicArgument::THIRD] = JSTaggedValue(PROMOTED_BUFFER_VALUE);
    }
    arguments[callbackArgument] = callback.GetTaggedValue();
    JSTaggedValue result = CallAtomicBuiltin(thread, operation, arguments);
    ASSERT_FALSE(thread->HasPendingException());
    ASSERT_TRUE(result.IsNumber());
    EXPECT_EQ(result.GetNumber(), expectedResult);
    EXPECT_TRUE(AtomicsTestHelper::GetBuffer(thread, array)->IsSendableArrayBuffer());
    JSTaggedValue stored = CallAtomicBuiltin(thread, collections::CollectionsAtomics::Load,
        {array.GetTaggedValue(), JSTaggedValue(0)});
    ASSERT_FALSE(thread->HasPendingException());
    ASSERT_TRUE(stored.IsNumber());
    EXPECT_EQ(stored.GetNumber(), expectedStored);
}
}  // namespace

HWTEST_F_L0(BuiltinsAtomicsTest, CollectionsAtomicsResolvePromotedBufferAfterArgumentConversion)
{
    using Collections = collections::CollectionsAtomics;
    VerifyAtomicBufferPromotion(thread, Collections::Load, AtomicArgument::SECOND,
        PROMOTED_BUFFER_VALUE, PROMOTED_BUFFER_VALUE);
    VerifyAtomicBufferPromotion(thread, Collections::Store, AtomicArgument::SECOND,
        ATOMIC_OPERAND_VALUE, ATOMIC_OPERAND_VALUE);
    VerifyAtomicBufferPromotion(thread, Collections::Store, AtomicArgument::THIRD,
        ATOMIC_OPERAND_VALUE, ATOMIC_OPERAND_VALUE);
    VerifyAtomicBufferPromotion(thread, Collections::Add, AtomicArgument::SECOND,
        PROMOTED_BUFFER_VALUE, ATOMIC_SUM_VALUE);
    VerifyAtomicBufferPromotion(thread, Collections::Add, AtomicArgument::THIRD,
        PROMOTED_BUFFER_VALUE, ATOMIC_SUM_VALUE);
    VerifyAtomicBufferPromotion(thread, Collections::CompareExchange, AtomicArgument::FOURTH,
        PROMOTED_BUFFER_VALUE, ATOMIC_OPERAND_VALUE);
}
}  // namespace panda::test
