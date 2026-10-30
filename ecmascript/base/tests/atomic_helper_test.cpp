/*
 * Copyright (c) 2022-2024 Huawei Device Co., Ltd.
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
#include <limits>
#include <thread>
#include <vector>

#include "ecmascript/base/atomic_helper.h"
#include "ecmascript/base/typed_array_helper-inl.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_typed_array.h"
#include "ecmascript/shared_objects/js_sendable_arraybuffer.h"
#include "ecmascript/shared_objects/js_shared_typed_array.h"
#include "ecmascript/tests/atomics_test_helper.h"
#include "ecmascript/tests/test_helper.h"
#include "ecmascript/utils/collections_atomic_helper.h"

using namespace panda::ecmascript;
using namespace panda::ecmascript::base;

namespace panda::test {
namespace {
constexpr uint32_t SENDABLE_BUFFER_SIZE = 16;
constexpr uint32_t SENDABLE_ARRAY_LENGTH = 4;
constexpr uint32_t EXPECTED_SECOND_ELEMENT_OFFSET = 4;
constexpr uint32_t BIGINT_BUFFER_SIZE = 16;
constexpr uint32_t BIGINT_ARRAY_LENGTH = 2;
constexpr int32_t FIRST_BIGINT_INDEX = 0;
constexpr int32_t SECOND_BIGINT_INDEX = 1;

JSTaggedValue TestValidateIntegerTypedArray(JSThread *thread, JSHandle<JSTaggedValue> array,
    bool waitable = false)
{
    return array->IsSharedTypedArray() ?
        collections::CollectionsAtomicHelper::ValidateIntegerTypedArray(thread, array, waitable) :
        AtomicHelper::ValidateIntegerTypedArray(thread, array, waitable);
}

uint32_t TestValidateAtomicAccess(JSThread *thread, JSHandle<JSTaggedValue> array,
                                  JSHandle<JSTaggedValue> index)
{
    return array->IsSharedTypedArray() ?
        collections::CollectionsAtomicHelper::ValidateAtomicAccess(thread, array, index) :
        AtomicHelper::ValidateAtomicAccess(thread, array, index);
}

JSTaggedValue TestAtomicLoad(JSThread *thread, JSHandle<JSTaggedValue> array, JSHandle<JSTaggedValue> index)
{
    return array->IsSharedTypedArray() ? collections::CollectionsAtomicHelper::AtomicLoad(thread, array, index) :
        AtomicHelper::AtomicLoad(thread, array, index);
}

JSTaggedValue TestAtomicStore(JSThread *thread, JSHandle<JSTaggedValue> array,
    JSHandle<JSTaggedValue> index, JSHandle<JSTaggedValue> &value)
{
    return array->IsSharedTypedArray() ?
        collections::CollectionsAtomicHelper::AtomicStore(thread, array, index, value) :
        AtomicHelper::AtomicStore(thread, array, index, value);
}

JSHandle<JSTypedArray> CreateBigIntTypedArray(JSThread *thread, DataViewType type)
{
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSTaggedValue> constructor = type == DataViewType::BIGINT64 ?
        env->GetBigInt64ArrayFunction() : env->GetBigUint64ArrayFunction();
    JSHandle<JSTypedArray> array = JSHandle<JSTypedArray>::Cast(
        factory->NewJSObjectByConstructor(JSHandle<JSFunction>::Cast(constructor), constructor));
    JSHandle<JSArrayBuffer> buffer = factory->NewJSArrayBuffer(BIGINT_BUFFER_SIZE);
    array->SetViewedArrayBufferOrByteArray(thread, buffer.GetTaggedValue());
    array->SetContentType(type);
    array->SetByteLength(BIGINT_BUFFER_SIZE);
    array->SetByteOffset(0);
    array->SetArrayLength(BIGINT_ARRAY_LENGTH);
    return array;
}
}

class AtomicHelperTest : public BaseTestWithScope<false> {
};

HWTEST_F_L0(AtomicHelperTest, ValidateIntegerTypedArray)
{
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();

    bool waitable = false;
    uint32_t bufferSize = 10;
    JSHandle<JSTaggedValue> func = env->GetInt8ArrayFunction();
    JSHandle<JSTypedArray> array =
        JSHandle<JSTypedArray>::Cast(factory->NewJSObjectByConstructor(JSHandle<JSFunction>::Cast(func), func));
    JSHandle<JSArrayBuffer> buffer = factory->NewJSArrayBuffer(bufferSize);
    JSHandle<JSTaggedValue> bufferVal = JSHandle<JSTaggedValue>::Cast(buffer);
    array->SetViewedArrayBufferOrByteArray(thread, bufferVal);
    JSHandle<JSTaggedValue> arrayVal = JSHandle<JSTaggedValue>::Cast(array);
    JSHandle<JSTaggedValue> resultBuffer(thread, TestValidateIntegerTypedArray(thread, arrayVal, waitable));
    EXPECT_EQ(resultBuffer.GetTaggedValue(), buffer.GetTaggedValue());
}

HWTEST_F_L0(AtomicHelperTest, ValidateSharedIntegerTypedArray)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    const std::array<DataViewType, 6> types = {DataViewType::INT8, DataViewType::UINT8, DataViewType::INT16,
        DataViewType::UINT16, DataViewType::INT32, DataViewType::UINT32};

    for (DataViewType type : types) {
        JSHandle<JSFunction> constructor = TypedArrayHelper::GetSharedConstructorFromType(thread, type);
        JSHandle<JSSharedTypedArray> array = JSHandle<JSSharedTypedArray>::Cast(
            factory->NewJSObjectByConstructor(constructor, JSHandle<JSTaggedValue>::Cast(constructor)));
        JSHandle<JSSendableArrayBuffer> buffer = factory->NewJSSendableArrayBuffer(SENDABLE_BUFFER_SIZE);
        array->SetViewedArrayBufferOrByteArray(thread, buffer.GetTaggedValue());
        array->SetContentType(type);
        array->SetByteLength(SENDABLE_ARRAY_LENGTH * TypedArrayHelper::GetSizeFromType(type));
        array->SetByteOffset(0);
        array->SetArrayLength(SENDABLE_ARRAY_LENGTH);
        JSHandle<JSTaggedValue> arrayValue = JSHandle<JSTaggedValue>::Cast(array);
        JSTaggedValue result = TestValidateIntegerTypedArray(thread, arrayValue);
        EXPECT_EQ(result, buffer.GetTaggedValue());
        EXPECT_FALSE(thread->HasPendingException());
    }
}

HWTEST_F_L0(AtomicHelperTest, RejectInvalidSharedTypedArrayTypes)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    const std::array<DataViewType, 5> types = {DataViewType::UINT8_CLAMPED, DataViewType::FLOAT32,
        DataViewType::FLOAT64, DataViewType::BIGINT64, DataViewType::BIGUINT64};

    for (DataViewType type : types) {
        JSHandle<JSFunction> constructor = TypedArrayHelper::GetSharedConstructorFromType(thread, type);
        JSHandle<JSSharedTypedArray> array = JSHandle<JSSharedTypedArray>::Cast(
            factory->NewJSObjectByConstructor(constructor, JSHandle<JSTaggedValue>::Cast(constructor)));
        uint32_t byteLength = SENDABLE_ARRAY_LENGTH * TypedArrayHelper::GetSizeFromType(type);
        JSHandle<JSSendableArrayBuffer> buffer = factory->NewJSSendableArrayBuffer(byteLength);
        array->SetViewedArrayBufferOrByteArray(thread, buffer.GetTaggedValue());
        array->SetContentType(type);
        array->SetByteLength(byteLength);
        array->SetByteOffset(0);
        array->SetArrayLength(SENDABLE_ARRAY_LENGTH);
        JSTaggedValue result = TestValidateIntegerTypedArray(
            thread, JSHandle<JSTaggedValue>::Cast(array));
        EXPECT_TRUE(result.IsException());
        EXPECT_TRUE(thread->HasPendingException());
        thread->ClearException();
    }

    JSHandle<JSTaggedValue> invalid(thread, JSTaggedValue(1));
    EXPECT_TRUE(TestValidateIntegerTypedArray(thread, invalid).IsException());
    EXPECT_TRUE(thread->HasPendingException());
    thread->ClearException();
}

HWTEST_F_L0(AtomicHelperTest, ValidateSharedWaitableType)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    const std::array<DataViewType, 2> types = {DataViewType::INT32, DataViewType::UINT32};

    for (DataViewType type : types) {
        JSHandle<JSFunction> constructor = TypedArrayHelper::GetSharedConstructorFromType(thread, type);
        JSHandle<JSSharedTypedArray> array = JSHandle<JSSharedTypedArray>::Cast(
            factory->NewJSObjectByConstructor(constructor, JSHandle<JSTaggedValue>::Cast(constructor)));
        JSHandle<JSSendableArrayBuffer> buffer = factory->NewJSSendableArrayBuffer(SENDABLE_BUFFER_SIZE);
        array->SetViewedArrayBufferOrByteArray(thread, buffer.GetTaggedValue());
        array->SetContentType(type);
        array->SetByteLength(SENDABLE_ARRAY_LENGTH * TypedArrayHelper::GetSizeFromType(type));
        array->SetByteOffset(0);
        array->SetArrayLength(SENDABLE_ARRAY_LENGTH);
        JSTaggedValue result = TestValidateIntegerTypedArray(
            thread, JSHandle<JSTaggedValue>::Cast(array), true);
        if (type == DataViewType::INT32) {
            EXPECT_EQ(result, buffer.GetTaggedValue());
            EXPECT_FALSE(thread->HasPendingException());
        } else {
            EXPECT_TRUE(result.IsException());
            EXPECT_TRUE(thread->HasPendingException());
            thread->ClearException();
        }
    }
}

HWTEST_F_L0(AtomicHelperTest, ValidateBigIntTypedArray)
{
    JSHandle<JSTypedArray> signedArray = CreateBigIntTypedArray(thread, DataViewType::BIGINT64);
    JSHandle<JSTypedArray> unsignedArray = CreateBigIntTypedArray(thread, DataViewType::BIGUINT64);
    JSHandle<JSTaggedValue> signedValue = JSHandle<JSTaggedValue>::Cast(signedArray);
    JSHandle<JSTaggedValue> unsignedValue = JSHandle<JSTaggedValue>::Cast(unsignedArray);

    EXPECT_EQ(TestValidateIntegerTypedArray(thread, signedValue),
        signedArray->GetViewedArrayBufferOrByteArray(thread));
    EXPECT_EQ(TestValidateIntegerTypedArray(thread, unsignedValue),
        unsignedArray->GetViewedArrayBufferOrByteArray(thread));
    EXPECT_EQ(TestValidateIntegerTypedArray(thread, signedValue, true),
        signedArray->GetViewedArrayBufferOrByteArray(thread));
    EXPECT_FALSE(thread->HasPendingException());

    EXPECT_TRUE(TestValidateIntegerTypedArray(thread, unsignedValue, true).IsException());
    EXPECT_TRUE(thread->HasPendingException());
    thread->ClearException();
}

HWTEST_F_L0(AtomicHelperTest, AtomicBigIntStoreLoad)
{
    JSHandle<JSTypedArray> signedArray = CreateBigIntTypedArray(thread, DataViewType::BIGINT64);
    JSHandle<JSTypedArray> unsignedArray = CreateBigIntTypedArray(thread, DataViewType::BIGUINT64);
    JSHandle<JSTaggedValue> signedArrayValue = JSHandle<JSTaggedValue>::Cast(signedArray);
    JSHandle<JSTaggedValue> unsignedArrayValue = JSHandle<JSTaggedValue>::Cast(unsignedArray);
    JSHandle<JSTaggedValue> firstIndex(thread, JSTaggedValue(FIRST_BIGINT_INDEX));
    JSHandle<JSTaggedValue> secondIndex(thread, JSTaggedValue(SECOND_BIGINT_INDEX));
    JSHandle<BigInt> signedBigInt = BigInt::Int64ToBigInt(thread, std::numeric_limits<int64_t>::min());
    JSHandle<BigInt> unsignedBigInt = BigInt::Uint64ToBigInt(thread, std::numeric_limits<uint64_t>::max());
    JSHandle<JSTaggedValue> signedValue(signedBigInt);
    JSHandle<JSTaggedValue> unsignedValue(unsignedBigInt);

    EXPECT_TRUE(TestAtomicStore(thread, signedArrayValue, secondIndex, signedValue).IsBigInt());
    EXPECT_TRUE(TestAtomicStore(thread, unsignedArrayValue, firstIndex, unsignedValue).IsBigInt());
    JSHandle<JSTaggedValue> signedResult(
        thread, TestAtomicLoad(thread, signedArrayValue, secondIndex));
    JSHandle<JSTaggedValue> unsignedResult(
        thread, TestAtomicLoad(thread, unsignedArrayValue, firstIndex));

    bool lossless = false;
    int64_t signedNumber = 0;
    BigInt::BigIntToInt64(thread, signedResult, &signedNumber, &lossless);
    EXPECT_TRUE(lossless);
    EXPECT_EQ(signedNumber, std::numeric_limits<int64_t>::min());
    uint64_t unsignedNumber = 0;
    BigInt::BigIntToUint64(thread, unsignedResult, &unsignedNumber, &lossless);
    EXPECT_TRUE(lossless);
    EXPECT_EQ(unsignedNumber, std::numeric_limits<uint64_t>::max());
    EXPECT_FALSE(thread->HasPendingException());
}

HWTEST_F_L0(AtomicHelperTest, ValidateAtomicAccess)
{
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();

    uint32_t bufferSize = 256;
    uint32_t byteOffset = 7;
    uint32_t arrayLength = 3;
    JSHandle<JSTaggedValue> func = env->GetInt8ArrayFunction();
    JSHandle<JSTypedArray> array =
        JSHandle<JSTypedArray>::Cast(factory->NewJSObjectByConstructor(JSHandle<JSFunction>::Cast(func), func));
    JSHandle<JSArrayBuffer> buffer = factory->NewJSArrayBuffer(bufferSize);
    JSHandle<JSTaggedValue> bufferVal = JSHandle<JSTaggedValue>::Cast(buffer);
    array->SetViewedArrayBufferOrByteArray(thread, bufferVal);
    array->SetContentType(DataViewType::INT8); // test int8 array
    array->SetByteOffset(byteOffset);
    array->SetArrayLength(arrayLength);
    JSHandle<JSTaggedValue> arrayVal = JSHandle<JSTaggedValue>::Cast(array);
    uint32_t index0 =
        TestValidateAtomicAccess(thread, arrayVal, JSHandle<JSTaggedValue>(thread, JSTaggedValue(0)));
    uint32_t index1 =
        TestValidateAtomicAccess(thread, arrayVal, JSHandle<JSTaggedValue>(thread, JSTaggedValue(1)));
    uint32_t index2 =
        TestValidateAtomicAccess(thread, arrayVal, JSHandle<JSTaggedValue>(thread, JSTaggedValue(2)));
    EXPECT_EQ(index0, 0 * sizeof(int8_t) + byteOffset);
    EXPECT_EQ(index1, 1 * sizeof(int8_t) + byteOffset);
    EXPECT_EQ(index2, 2 * sizeof(int8_t) + byteOffset);
}

HWTEST_F_L0(AtomicHelperTest, ValidateSharedAtomicAccessBounds)
{
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSTaggedValue> constructor = env->GetSharedInt16ArrayFunction();
    JSHandle<JSSharedTypedArray> array = JSHandle<JSSharedTypedArray>::Cast(
        factory->NewJSObjectByConstructor(JSHandle<JSFunction>::Cast(constructor), constructor));
    JSHandle<JSSendableArrayBuffer> buffer = factory->NewJSSendableArrayBuffer(SENDABLE_BUFFER_SIZE);
    array->SetViewedArrayBufferOrByteArray(thread, buffer.GetTaggedValue());
    array->SetContentType(DataViewType::INT16);
    array->SetByteLength(2 * sizeof(int16_t));
    array->SetByteOffset(2);
    array->SetArrayLength(2);
    JSHandle<JSTaggedValue> arrayValue = JSHandle<JSTaggedValue>::Cast(array);

    EXPECT_EQ(TestValidateAtomicAccess(thread, arrayValue,
        JSHandle<JSTaggedValue>(thread, JSTaggedValue(1))), EXPECTED_SECOND_ELEMENT_OFFSET);
    EXPECT_FALSE(thread->HasPendingException());
    TestValidateAtomicAccess(
        thread, arrayValue, JSHandle<JSTaggedValue>(thread, JSTaggedValue(2)));
    EXPECT_TRUE(thread->HasPendingException());
    thread->ClearException();
    TestValidateAtomicAccess(
        thread, arrayValue, JSHandle<JSTaggedValue>(thread, JSTaggedValue(-1)));
    EXPECT_TRUE(thread->HasPendingException());
    thread->ClearException();
}

HWTEST_F_L0(AtomicHelperTest, Atomic_Store_Load)
{
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();

    uint32_t bufferSize = 256;
    uint32_t byteOffset = 2 * sizeof(uint32_t);
    uint32_t arrayLength = 3;
    JSHandle<JSTaggedValue> func = env->GetUint32ArrayFunction();
    JSHandle<JSTypedArray> array =
        JSHandle<JSTypedArray>::Cast(factory->NewJSObjectByConstructor(JSHandle<JSFunction>::Cast(func), func));
    JSHandle<JSArrayBuffer> buffer = factory->NewJSArrayBuffer(bufferSize);
    JSHandle<JSTaggedValue> bufferVal = JSHandle<JSTaggedValue>::Cast(buffer);
    array->SetViewedArrayBufferOrByteArray(thread, bufferVal);
    array->SetContentType(DataViewType::UINT32); // test uint32_t array
    array->SetByteLength(arrayLength * sizeof(uint32_t));
    array->SetByteOffset(byteOffset);
    array->SetArrayLength(arrayLength);
    JSHandle<JSTaggedValue> arrayVal = JSHandle<JSTaggedValue>::Cast(array);
    JSHandle<JSTaggedValue> index0(thread, JSTaggedValue(0));
    JSHandle<JSTaggedValue> index1(thread, JSTaggedValue(1));
    JSHandle<JSTaggedValue> index2(thread, JSTaggedValue(2));
    JSHandle<JSTaggedValue> value0(thread, JSTaggedValue(-1)); // to uint32_t : 4294967295
    JSHandle<JSTaggedValue> value1(thread, JSTaggedValue(1));
#ifdef PANDA_TARGET_MACOS
    JSHandle<JSTaggedValue> value2(
        thread, static_cast<JSTaggedValue>(static_cast<uint32_t>(4294967295 + 1))); // to uint32_t : 0
#else
    JSHandle<JSTaggedValue> value2(thread, JSTaggedValue(4294967295 + 1));
#endif
    JSHandle<JSTaggedValue> bufferTag0(thread, TestAtomicStore(thread, arrayVal, index0, value0));
    JSHandle<JSTaggedValue> bufferTag1(thread, TestAtomicStore(thread, arrayVal, index1, value1));
    JSHandle<JSTaggedValue> bufferTag2(thread, TestAtomicStore(thread, arrayVal, index2, value2));
    EXPECT_EQ(value0.GetTaggedValue().GetNumber(), -1);
    EXPECT_EQ(value1.GetTaggedValue().GetNumber(), 1);
    EXPECT_EQ(value2.GetTaggedValue().GetNumber(), 4294967296);

    JSHandle<JSTaggedValue> result0(thread, TestAtomicLoad(thread, arrayVal, index0));
    JSHandle<JSTaggedValue> result1(thread, TestAtomicLoad(thread, arrayVal, index1));
    JSHandle<JSTaggedValue> result2(thread, TestAtomicLoad(thread, arrayVal, index2));
    EXPECT_EQ(result0.GetTaggedValue().GetNumber(), 4294967295); // 4294967295 : -1 to uint32_t
    EXPECT_EQ(result1.GetTaggedValue().GetNumber(), 1);
    EXPECT_EQ(result2.GetTaggedValue().GetNumber(), 0); // 0 : 4294967296 to uint32_t
}
HWTEST_F_L0(AtomicHelperTest, IntegerStoreLoadConversions)
{
    const std::array<DataViewType, 6> types = {DataViewType::INT8, DataViewType::UINT8, DataViewType::INT16,
        DataViewType::UINT16, DataViewType::INT32, DataViewType::UINT32};
    const std::array<double, 6> expected = {-1, 255, -1, 65535, -1, 4294967295};
    JSHandle<JSTaggedValue> index(thread, JSTaggedValue(1));
    for (bool sendable : {false, true}) {
        for (size_t i = 0; i < types.size(); i++) {
            auto array = AtomicsTestHelper::CreateArray(thread, types[i], sendable);
            JSHandle<JSTaggedValue> value(thread, JSTaggedValue(-1));
            EXPECT_EQ(TestAtomicStore(thread, array, index, value).GetNumber(), -1);
            EXPECT_EQ(TestAtomicLoad(thread, array, index).GetNumber(), expected[i]);
            value = JSHandle<JSTaggedValue>(thread, JSTaggedValue(3.75));
            EXPECT_EQ(TestAtomicStore(thread, array, index, value).GetNumber(), 3);
            EXPECT_EQ(TestAtomicLoad(thread, array, index).GetNumber(), 3);
            EXPECT_FALSE(thread->HasPendingException());
        }
    }
}

HWTEST_F_L0(AtomicHelperTest, ValidateIntegerAndWaitableTypeCombinations)
{
    const std::array<DataViewType, 5> types = {DataViewType::INT32, DataViewType::UINT32,
        DataViewType::BIGINT64, DataViewType::BIGUINT64, DataViewType::FLOAT32};
    for (bool sendable : {true}) {
        for (bool waitable : {false, true}) {
            for (DataViewType type : types) {
                auto array = AtomicsTestHelper::CreateArray(thread, type, sendable);
                bool valid = waitable ? (type == DataViewType::INT32 ||
                    (!sendable && type == DataViewType::BIGINT64)) :
                    (type == DataViewType::INT32 || type == DataViewType::UINT32 ||
                    (!sendable && (type == DataViewType::BIGINT64 || type == DataViewType::BIGUINT64)));
                collections::CollectionsAtomicHelper::AtomicTypedArrayWitness witness {};
                EXPECT_EQ(collections::CollectionsAtomicHelper::ValidateIntegerTypedArray(
                    thread, array, witness, waitable), valid);
                if (valid) {
                    EXPECT_EQ(witness.type, type);
                    EXPECT_EQ(witness.length, 2U);
                    EXPECT_EQ(witness.byteOffset, 0U);
                    EXPECT_EQ(witness.isSendable, sendable);
                    EXPECT_EQ(witness.buffer,
                        JSHandle<JSTypedArray>::Cast(array)->GetViewedArrayBufferOrByteArray(thread));
                    EXPECT_FALSE(thread->HasPendingException());
                } else {
                    EXPECT_EXCEPTION();
                }
            }
        }
    }
}

HWTEST_F_L0(AtomicHelperTest, StoreLoadRejectInvalidArraysAndIndices)
{
    JSHandle<JSTaggedValue> zero(thread, JSTaggedValue(0));
    JSHandle<JSTaggedValue> value(thread, JSTaggedValue(7));
    JSHandle<JSTaggedValue> invalid(thread, JSTaggedValue::Null());
    EXPECT_TRUE(TestAtomicLoad(thread, invalid, zero).IsException());
    EXPECT_EXCEPTION();
    EXPECT_TRUE(TestAtomicStore(thread, invalid, zero, value).IsException());
    EXPECT_EXCEPTION();
    for (bool sendable : {false, true}) {
        auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, sendable);
        const std::array<JSTaggedValue, 3> indices = {JSTaggedValue(-1), JSTaggedValue(2),
            thread->GlobalConstants()->GetIteratorSymbol()};
        for (JSTaggedValue invalidIndex : indices) {
            JSHandle<JSTaggedValue> index(thread, invalidIndex);
            EXPECT_TRUE(TestAtomicLoad(thread, array, index).IsException());
            EXPECT_EXCEPTION();
            EXPECT_TRUE(TestAtomicStore(thread, array, index, value).IsException());
            EXPECT_EXCEPTION();
        }
        EXPECT_EQ(TestAtomicLoad(thread, array, zero).GetNumber(), 0);
    }
}

HWTEST_F_L0(AtomicHelperTest, StoreRejectsNumberAndBigIntConversionErrors)
{
    JSHandle<JSTaggedValue> index(thread, JSTaggedValue(0));
    JSHandle<JSTaggedValue> symbol(thread, thread->GlobalConstants()->GetIteratorSymbol());
    JSHandle<JSTaggedValue> number(thread, JSTaggedValue(1));
    for (bool sendable : {false, true}) {
        auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, sendable);
        EXPECT_TRUE(TestAtomicStore(thread, array, index, symbol).IsException());
        EXPECT_EXCEPTION();
        EXPECT_EQ(TestAtomicLoad(thread, array, index).GetNumber(), 0);
    }
    for (DataViewType type : {DataViewType::BIGINT64, DataViewType::BIGUINT64}) {
        auto array = AtomicsTestHelper::CreateArray(thread, type, false);
        EXPECT_TRUE(TestAtomicStore(thread, array, index, number).IsException());
        EXPECT_EXCEPTION();
    }
}

HWTEST_F_L0(AtomicHelperTest, StoreLoadRejectInitiallyDetachedBuffers)
{
    JSHandle<JSTaggedValue> index(thread, JSTaggedValue(0));
    JSHandle<JSTaggedValue> value(thread, JSTaggedValue(1));
    for (bool sendable : {false, true}) {
        auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, sendable);
        AtomicsTestHelper::DetachBuffer(thread, array);
        EXPECT_TRUE(TestAtomicLoad(thread, array, index).IsException());
        EXPECT_EXCEPTION();
        EXPECT_TRUE(TestAtomicStore(thread, array, index, value).IsException());
        EXPECT_EXCEPTION();
    }
}

HWTEST_F_L0(AtomicHelperTest, LoadRejectsDetachmentDuringIndexConversion)
{
    JSHandle<JSTaggedValue> zero(thread, JSTaggedValue(0));
    for (bool sendable : {false, true}) {
        auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, sendable);
        auto index = AtomicsTestHelper::CreateDetachingValue(thread, array, zero);
        EXPECT_TRUE(TestAtomicLoad(thread, array, index).IsException());
        EXPECT_EXCEPTION();
        JSTaggedValue buffer = JSHandle<JSTypedArray>::Cast(array)->GetViewedArrayBufferOrByteArray(thread);
        EXPECT_TRUE(JSArrayBuffer::Cast(buffer.GetTaggedObject())->GetArrayBufferData(thread).IsNull());
    }
}

HWTEST_F_L0(AtomicHelperTest, StoreRejectsDetachmentDuringIndexAndValueConversion)
{
    JSHandle<JSTaggedValue> zero(thread, JSTaggedValue(0));
    for (bool sendable : {false, true}) {
        for (bool detachIndex : {false, true}) {
            auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, sendable);
            auto detaching = AtomicsTestHelper::CreateDetachingValue(thread, array, zero);
            JSHandle<JSTaggedValue> index = detachIndex ? detaching : zero;
            JSHandle<JSTaggedValue> value = detachIndex ? zero : detaching;
            EXPECT_TRUE(TestAtomicStore(thread, array, index, value).IsException());
            EXPECT_EXCEPTION();
        }
    }
}
HWTEST_F_L0(AtomicHelperTest, ValidateIntegerTypedArrayWrapperTypeMatrix)
{
    const std::array<DataViewType, 11> types = {DataViewType::INT8, DataViewType::UINT8,
        DataViewType::INT16, DataViewType::UINT16, DataViewType::INT32, DataViewType::UINT32,
        DataViewType::BIGINT64, DataViewType::BIGUINT64, DataViewType::UINT8_CLAMPED,
        DataViewType::FLOAT32, DataViewType::FLOAT64};
    for (bool sendable : {false, true}) {
        for (bool waitable : {false, true}) {
            for (DataViewType type : types) {
                auto array = AtomicsTestHelper::CreateArray(thread, type, sendable);
                const bool integer = type == DataViewType::INT8 || type == DataViewType::UINT8 ||
                    type == DataViewType::INT16 || type == DataViewType::UINT16 ||
                    type == DataViewType::INT32 || type == DataViewType::UINT32;
                const bool bigInt = type == DataViewType::BIGINT64 || type == DataViewType::BIGUINT64;
                const bool valid = waitable ?
                    (type == DataViewType::INT32 || (!sendable && type == DataViewType::BIGINT64)) :
                    (integer || (!sendable && bigInt));
                JSTaggedValue result = TestValidateIntegerTypedArray(thread, array, waitable);
                if (valid) {
                    EXPECT_EQ(result, AtomicsTestHelper::GetBuffer(thread, array).GetTaggedValue());
                    EXPECT_FALSE(thread->HasPendingException());
                } else {
                    EXPECT_TRUE(result.IsException());
                    EXPECT_EXCEPTION();
                }
            }
        }
    }
}

HWTEST_F_L0(AtomicHelperTest, ValidateIntegerTypedArrayWrapperRejectsNonArraysAndDetachedBuffers)
{
    const std::array<JSTaggedValue, 3> invalidValues = {
        JSTaggedValue::Undefined(), JSTaggedValue::Null(), JSTaggedValue(0)};
    for (bool waitable : {false, true}) {
        for (JSTaggedValue invalidValue : invalidValues) {
            JSHandle<JSTaggedValue> invalid(thread, invalidValue);
            EXPECT_TRUE(TestValidateIntegerTypedArray(thread, invalid, waitable).IsException());
            EXPECT_EXCEPTION();
        }
        for (bool sendable : {false, true}) {
            auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, sendable);
            AtomicsTestHelper::DetachBuffer(thread, array);
            EXPECT_TRUE(TestValidateIntegerTypedArray(thread, array, waitable).IsException());
            EXPECT_EXCEPTION();
        }
    }
}

HWTEST_F_L0(AtomicHelperTest, CollectionsValidationBusinessErrors)
{
    using Helper = collections::CollectionsAtomicHelper;
    using Errors = containers::ErrorFlag;
    JSHandle<JSTaggedValue> invalid(thread, JSTaggedValue::Null());
    EXPECT_TRUE(Helper::ValidateIntegerTypedArray(thread, invalid).IsException());
    AtomicsTestHelper::ExpectBusinessError(thread, Errors::TYPE_ERROR);
    auto ordinary = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, false);
    EXPECT_TRUE(Helper::ValidateIntegerTypedArray(thread, ordinary).IsException());
    AtomicsTestHelper::ExpectBusinessError(thread, Errors::TYPE_ERROR);
    for (DataViewType type : {DataViewType::FLOAT32, DataViewType::FLOAT64, DataViewType::UINT8_CLAMPED}) {
        auto array = AtomicsTestHelper::CreateArray(thread, type, true);
        EXPECT_TRUE(Helper::ValidateIntegerTypedArray(thread, array).IsException());
        AtomicsTestHelper::ExpectBusinessError(thread, Errors::TYPE_ERROR);
    }
    auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, true);
    JSHandle<JSTaggedValue> outside(thread, JSTaggedValue(2));
    EXPECT_EQ(Helper::ValidateAtomicAccess(thread, array, outside), 0U);
    AtomicsTestHelper::ExpectBusinessError(thread, Errors::RANGE_ERROR);
    AtomicsTestHelper::DetachBuffer(thread, array);
    EXPECT_TRUE(Helper::ValidateIntegerTypedArray(thread, array).IsException());
    AtomicsTestHelper::ExpectBusinessError(thread, Errors::TYPE_ERROR);
}

HWTEST_F_L0(AtomicHelperTest, CollectionsStoreLoadBusinessErrors)
{
    using Helper = collections::CollectionsAtomicHelper;
    JSHandle<JSTaggedValue> zero(thread, JSTaggedValue(0));
    for (bool store : {false, true}) {
        for (bool detachDuringConversion : {false, true}) {
            auto array = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, true);
            JSHandle<JSTaggedValue> index = zero;
            if (detachDuringConversion) {
                index = AtomicsTestHelper::CreateDetachingValue(thread, array, zero);
            } else {
                AtomicsTestHelper::DetachBuffer(thread, array);
            }
            EXPECT_TRUE((store ? Helper::AtomicStore(thread, array, index, zero) :
                Helper::AtomicLoad(thread, array, index)).IsException());
            AtomicsTestHelper::ExpectBusinessError(thread, containers::ErrorFlag::TYPE_ERROR);
        }
    }
    auto ordinary = AtomicsTestHelper::CreateArray(thread, DataViewType::INT32, false);
    JSHandle<JSTaggedValue> outside(thread, JSTaggedValue(2));
    EXPECT_TRUE(AtomicHelper::AtomicLoad(thread, ordinary, outside).IsException());
    ASSERT_TRUE(thread->HasPendingException());
    JSHandle<JSTaggedValue> error(thread, thread->GetException());
    thread->ClearException();
    EXPECT_TRUE(AtomicsTestHelper::GetProperty(thread, error, "code")->IsUndefined());
    auto name = AtomicsTestHelper::GetProperty(thread, error, "name");
    ASSERT_TRUE(name->IsString());
    EXPECT_EQ(EcmaStringAccessor(JSHandle<EcmaString>::Cast(name)).ToStdString(thread), "RangeError");
}

HWTEST_F_L0(AtomicHelperTest, AtomicBigIntStoreTruncatesToLow64Bits)
{
    const std::array<const char *, 8> inputs = {"0", "1", "-1", "4294967296",
        "18446744073709551615", "18446744073709551616", "18446744073709551617", "-18446744073709551617"};
    const std::array<uint64_t, 8> expected = {0, 1, UINT64_MAX, uint64_t {1} << 32,
        UINT64_MAX, 0, 1, UINT64_MAX};
    JSHandle<JSTaggedValue> index(thread, JSTaggedValue(0));
    for (DataViewType type : {DataViewType::BIGINT64, DataViewType::BIGUINT64}) {
        auto array = AtomicsTestHelper::CreateArray(thread, type, false);
        for (size_t i = 0; i < inputs.size(); i++) {
            JSHandle<JSTaggedValue> text(thread->GetEcmaVM()->GetFactory()->NewFromASCII(inputs[i]));
            JSHandle<JSTaggedValue> value(thread, JSTaggedValue::ToBigInt(thread, text));
            ASSERT_FALSE(thread->HasPendingException());
            EXPECT_EQ(TestAtomicStore(thread, array, index, value), value.GetTaggedValue());
            JSHandle<JSTaggedValue> loaded(thread, TestAtomicLoad(thread, array, index));
            ASSERT_TRUE(loaded->IsBigInt());
            if (type == DataViewType::BIGINT64) {
                EXPECT_EQ(JSHandle<BigInt>::Cast(loaded)->ToInt64(), static_cast<int64_t>(expected[i]));
            } else {
                EXPECT_EQ(JSHandle<BigInt>::Cast(loaded)->ToUint64(), expected[i]);
            }
            EXPECT_FALSE(thread->HasPendingException());
        }
    }
}
}  // namespace panda::test
