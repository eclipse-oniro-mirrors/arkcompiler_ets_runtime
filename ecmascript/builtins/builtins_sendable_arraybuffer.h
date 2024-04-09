/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_BUILTINS_BUILTINS_SENDABLE_ARRAYBUFFER_H
#define ECMASCRIPT_BUILTINS_BUILTINS_SENDABLE_ARRAYBUFFER_H

#include "ecmascript/base/builtins_base.h"
#include "ecmascript/base/number_helper.h"
#include "ecmascript/js_dataview.h"
#include "ecmascript/js_typed_array.h"

// List of functions in ArrayBuffer, excluding the '@@' properties.
// V(name, func, length, stubIndex)
// where BuiltinsArrayBuffer::func refers to the native implementation of ArrayBuffer[name].
//       kungfu::BuiltinsStubCSigns::stubIndex refers to the builtin stub index, or INVALID if no stub available.
#define BUILTIN_ARRAY_BUFFER_FUNCTIONS(V)                                           \
    /* ArrayBuffer.isView ( arg ) */                                                \
    V("isView", IsView, 1, ArrayBufferIsView)

namespace panda::ecmascript::builtins {
using DataViewType = ecmascript::DataViewType;
using BuiltinFunctionEntry = base::BuiltinFunctionEntry;

class BuiltinsSendableArrayBuffer : public base::BuiltinsBase {
public:
    enum NumberSize : uint8_t {
        UINT16 = 2, INT16 = 2, UINT32 = 4, INT32 = 4, FLOAT32 = 4, FLOAT64 = 8, BIGINT64 = 8, BIGUINT64 = 8
    };

    // 24.1.2.1 ArrayBuffer(length)
    static JSTaggedValue ArrayBufferConstructor(EcmaRuntimeCallInfo *argv);
    // 24.1.3.1 ArrayBuffer.isView(arg)
    static JSTaggedValue IsView(EcmaRuntimeCallInfo *argv);
    // 24.1.3.3 get ArrayBuffer[@@species]
    static JSTaggedValue Species(EcmaRuntimeCallInfo *argv);
    // 24.1.4.1 get ArrayBuffer.prototype.byteLength
    static JSTaggedValue GetByteLength(EcmaRuntimeCallInfo *argv);
    // 24.1.4.3 ArrayBuffer.prototype.slice()
    static JSTaggedValue Slice(EcmaRuntimeCallInfo *argv);
    // 24.1.1.2 IsDetachedBuffer(arrayBuffer)
    static bool IsDetachedBuffer(JSTaggedValue arrayBuffer);
    // 24.1.1.5 GetValueFromBuffer ( arrayBuffer, byteIndex, type, isLittleEndian )
    static JSTaggedValue GetValueFromBuffer(JSThread *thread, JSTaggedValue arrBuf, uint32_t byteIndex,
                                            DataViewType type, bool littleEndian);
    // 24.1.1.6 SetValueInBuffer ( arrayBuffer, byteIndex, type, value, isLittleEndian )
    static JSTaggedValue SetValueInBuffer(JSThread *thread, JSTaggedValue arrBuf, uint32_t byteIndex,
                                          DataViewType type, const JSHandle<JSTaggedValue> &value, bool littleEndian);
    // 24.1.1.4 CloneArrayBuffer( srcBuffer, srcByteOffset [, cloneConstructor] )
    static JSTaggedValue CloneArrayBuffer(JSThread *thread, const JSHandle<JSTaggedValue> &srcBuffer,
                                          uint32_t srcByteOffset, JSHandle<JSTaggedValue> constructor);
    // 24.1.1.1 AllocateArrayBuffer(constructor, byteLength)
    static JSTaggedValue AllocateSendableArrayBuffer(
        JSThread *thread, const JSHandle<JSTaggedValue> &newTarget, uint64_t byteLength);
    // es12 25.1.2.6 IsUnclampedIntegerElementType ( type )
    static bool IsUnclampedIntegerElementType(DataViewType type);
    // es12 25.1.2.7 IsBigIntElementType ( type )
    static bool IsBigIntElementType(DataViewType type);

    // Excluding the '@@' internal properties
    static Span<const base::BuiltinFunctionEntry> GetArrayBufferFunctions()
    {
        return Span<const base::BuiltinFunctionEntry>(ARRAY_BUFFER_FUNCTIONS);
    }

    static JSTaggedValue FastSetValueInBuffer(JSThread* thread, JSTaggedValue arrBuf, uint32_t byteIndex,
                                              DataViewType type, double val, bool littleEndian);
    static JSTaggedValue TryFastSetValueInBuffer(JSThread *thread, JSTaggedValue arrBuf, uint32_t byteBeginOffset,
                                                 uint32_t byteEndOffset, DataViewType type,
                                                 double val, bool littleEndian);
    template<typename T>
    static void FastSetValueInBufferForByte(uint8_t *byteBeginOffset, uint8_t *byteEndOffset,
                                            double val);
    static void FastSetValueInBufferForUint8Clamped(uint8_t *byteBeginOffset, uint8_t *byteEndOffset,
                                                    double val);
    template<typename T>
    static void FastSetValueInBufferForInteger(uint8_t *byteBeginOffset, uint8_t *byteEndOffset,
                                               double val, bool littleEndian);
    template<typename T>
    static void FastSetValueInBufferForFloat(uint8_t *byteBeginOffset, uint8_t *byteEndOffset,
                                             double val, bool littleEndian);
    template<typename T>
    static void FastSetValueInBufferForBigInt(JSThread *thread, uint8_t *byteBeginOffset, uint8_t *byteEndOffset,
                                              double val, bool littleEndian);
    static JSTaggedValue SetValueInBuffer(JSThread *thread, uint32_t byteIndex, uint8_t *block,
                                          DataViewType type, double val, bool littleEndian);
    static JSTaggedValue GetValueFromBuffer(JSThread *thread, uint32_t byteIndex, uint8_t *block,
                                            DataViewType type, bool littleEndian);
    static void *GetDataPointFromBuffer(JSTaggedValue arrBuf, uint32_t byteOffset = 0);

    static size_t GetNumPrototypeInlinedProperties()
    {
        // 3 : 3 more inline properties in Set.prototype
        //   (1) Set.prototype.slice
        //   (2) Set.prototype [ @@toStringTag ]
        //   (3) get Set.prototype.size
        return 3;
    }

    static Span<const std::pair<std::string_view, bool>> GetPrototypeProperties()
    {
        return Span<const std::pair<std::string_view, bool>>(ARRAYBUFFER_PROTOTYPE_PROPERTIES);
    }

    static Span<const std::pair<std::string_view, bool>> GetFunctionProperties()
    {
        return Span<const std::pair<std::string_view, bool>>(ARRAYBUFFER_FUNCTION_PROPERTIES);
    }

private:
#define BUILTIN_ARRAY_BUFFER_ENTRY(name, func, length, id) \
    BuiltinFunctionEntry::Create(name, BuiltinsSendableArrayBuffer::func, length, kungfu::BuiltinsStubCSigns::id),

    static constexpr std::array ARRAY_BUFFER_FUNCTIONS = {BUILTIN_ARRAY_BUFFER_FUNCTIONS(BUILTIN_ARRAY_BUFFER_ENTRY)};
#undef BUILTIN_ARRAY_BUFFER_ENTRY

#define ARRAYBUFFER_PROPERTIES_PAIR(name, func, length, id) \
    std::pair<std::string_view, bool>(name, false),

    static constexpr std::array ARRAYBUFFER_PROTOTYPE_PROPERTIES = {
        std::pair<std::string_view, bool>("slice", false),
        std::pair<std::string_view, bool>("byteLength", true),
        std::pair<std::string_view, bool>("[Symbol.toStringTag]", false),
    };

    static constexpr std::array ARRAYBUFFER_FUNCTION_PROPERTIES = {
        std::pair<std::string_view, bool>("length", false),
        std::pair<std::string_view, bool>("name", false),
        std::pair<std::string_view, bool>("prototype", false),
        BUILTIN_ARRAY_BUFFER_FUNCTIONS(ARRAYBUFFER_PROPERTIES_PAIR)
        std::pair<std::string_view, bool>("[Symbol.species]", true),
    };
#undef SET_PROPERTIES_PAIR

    template <typename T>
    static T LittleEndianToBigEndian(T liValue);
    template<typename T>
    static T LittleEndianToBigEndian64Bit(T liValue);

    template<typename T>
    static void SetTypeData(uint8_t *block, T value, uint32_t index);

    template<typename T>
    static void FastSetTypeData(uint8_t *byteBeginOffset, uint8_t *byteEndOffset, T value);

    template<typename T, NumberSize size>
    static JSTaggedValue GetValueFromBufferForInteger(uint8_t *block, uint32_t byteIndex, bool littleEndian);

    template<typename T, typename UnionType, NumberSize size>
    static JSTaggedValue GetValueFromBufferForFloat(uint8_t *block, uint32_t byteIndex, bool littleEndian);
    template<typename T1, typename T2>
    static JSTaggedValue CommonConvert(T1 &value, T2 &res, bool littleEndian);
    template<typename T, NumberSize size>
    static JSTaggedValue GetValueFromBufferForBigInt(JSThread *thread, uint8_t *block,
                                                     uint32_t byteIndex, bool littleEndian);

    template<typename T>
    static void SetValueInBufferForByte(double val, uint8_t *block, uint32_t byteIndex);

    static void SetValueInBufferForUint8Clamped(double val, uint8_t *block, uint32_t byteIndex);

    template<typename T>
    static void SetValueInBufferForInteger(double val, uint8_t *block, uint32_t byteIndex, bool littleEndian);

    template<typename T>
    static void SetValueInBufferForFloat(double val, uint8_t *block, uint32_t byteIndex, bool littleEndian);

    template<typename T>
    static void SetValueInBufferForBigInt(JSThread *thread, const JSHandle<JSTaggedValue> &val,
                                          JSHandle<JSTaggedValue> &arrBuf, uint32_t byteIndex, bool littleEndian);

    template<typename T>
    static void SetValueInBufferForBigInt(JSThread *thread, double val,
                                          uint8_t *block, uint32_t byteIndex, bool littleEndian);

    static JSTaggedValue TypedArrayToList(JSThread *thread, JSHandle<JSTypedArray>& items);

    static constexpr uint64_t MAX_NATIVE_SIZE_LIMIT = 4_GB;
    static constexpr char const *NATIVE_SIZE_OUT_OF_LIMIT_MESSAGE = "total array buffer size out of limit(4_GB)";

    friend class BuiltinsArray;
    friend class BuiltinsSharedArray;
};
}  // namespace panda::ecmascript::builtins

#endif  // ECMASCRIPT_BUILTINS_BUILTINS_SENDABLE_ARRAYBUFFER_H