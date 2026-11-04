/*
 * Copyright (c) 2021-2024 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_AOT_LITERAL_INFO_H
#define ECMASCRIPT_AOT_LITERAL_INFO_H

#include "ecmascript/tagged_array.h"

namespace panda::ecmascript {
class JSThread;

/*                  AOTLiteralInfo
 *      +-----------------------------------+----
 *      |              ...                  |  ^
 *      |              ...                  |  |
 *      | cache(store function entry index) |  AOT Function Entry Index, if its value is -1,
 *      |              ...                  |  means the function has not been compiled with AOT.
 *      |              ...                  |  v
 *      +-----------------------------------+----
 *      |         Literal Type              |  JSTaggedValue(int32_t)
 *      +-----------------------------------+----
 *      |      AOT Instance Hclass (IHC)    |  JSTaggedValue(HClass)
 *      +-----------------------------------+----
 *      |   AOT Constructor Hclass (CHC)    |  JSTaggedValue(HClass)
 *      +-----------------------------------+----
 */
class AOTLiteralInfo : public TaggedArray {
public:
    static constexpr size_t NO_FUNC_ENTRY_VALUE = -1;
    static constexpr size_t AOT_CHC_INDEX = 1;
    static constexpr size_t AOT_IHC_INDEX = 2;
    static constexpr size_t LITERAL_TYPE_INDEX = 3;
    static constexpr size_t RESERVED_LENGTH = LITERAL_TYPE_INDEX;

    static constexpr int32_t METHOD_LITERAL_TYPE = 1;
    static constexpr int32_t INVALID_LITERAL_TYPE = 0;

    static AOTLiteralInfo* Cast(TaggedObject* object)
    {
        ASSERT(JSTaggedValue(object).IsAOTLiteralInfo());
        return static_cast<AOTLiteralInfo*>(object);
    }

    static size_t ComputeSize(uint32_t cacheSize)
    {
        return TaggedArray::ComputeSize(JSTaggedValue::TaggedTypeSize(), cacheSize + RESERVED_LENGTH);
    }

    void InitializeWithSpecialValue(JSTaggedValue initValue, uint32_t capacity,
                                    uint32_t extraLength = 0);

    inline uint32_t GetCacheLength() const
    {
        return GetLength() - RESERVED_LENGTH;
    }

    inline void SetIhcPrimitive(JSTaggedValue value)
    {
        Barriers::SetPrimitive(GetData(), GetIhcOffset(), value.GetRawData());
    }

    inline void SetChcPrimitive(JSTaggedValue value)
    {
        Barriers::SetPrimitive(GetData(), GetChcOffset(), value.GetRawData());
    }

    inline void SetIhc(JSThread *thread, JSTaggedValue value)
    {
        if (value.IsHeapObject()) {
            Barriers::SetObject(thread, this, TaggedArray::DATA_OFFSET + GetIhcOffset(), value.GetRawData());
        } else {
            Barriers::SetPrimitive<JSTaggedType>(GetData(), GetIhcOffset(), value.GetRawData());
        }
    }

    inline JSTaggedValue GetIhc(JSThread *thread) const
    {
        return JSTaggedValue(Barriers::GetTaggedValue(thread, this, TaggedArray::DATA_OFFSET + GetIhcOffset()));
    }

    inline void SetChc(JSThread *thread, JSTaggedValue value)
    {
        if (value.IsHeapObject()) {
            Barriers::SetObject(thread, this, TaggedArray::DATA_OFFSET + GetChcOffset(), value.GetRawData());
        } else {
            Barriers::SetPrimitive<JSTaggedType>(GetData(), GetChcOffset(), value.GetRawData());
        }
    }

    inline JSTaggedValue GetChc(JSThread *thread) const
    {
        return JSTaggedValue(Barriers::GetTaggedValue(thread, this, TaggedArray::DATA_OFFSET + GetChcOffset()));
    }

    inline void SetLiteralType(JSTaggedValue value)
    {
        Barriers::SetPrimitive(GetData(), GetLiteralTypeOffset(), value.GetRawData());
    }

    inline int GetLiteralType() const
    {
        return JSTaggedValue(Barriers::GetPrimitive<JSTaggedValue>(GetData(), GetLiteralTypeOffset())).GetInt();
    }

    void SetObjectToCache(JSThread *thread, uint32_t index, JSTaggedValue value);

    inline JSTaggedValue GetObjectFromCache(JSThread *thread, uint32_t index) const
    {
        return Get(thread, index);
    }

private:
    inline size_t GetIhcOffset() const
    {
        return JSTaggedValue::TaggedTypeSize() * (GetLength() - AOT_IHC_INDEX);
    }

    inline size_t GetChcOffset() const
    {
        return JSTaggedValue::TaggedTypeSize() * (GetLength() - AOT_CHC_INDEX);
    }

    inline size_t GetLiteralTypeOffset() const
    {
        return JSTaggedValue::TaggedTypeSize() * (GetLength() - LITERAL_TYPE_INDEX);
    }
};
} // namespace panda::ecmascript
#endif // ECMASCRIPT_AOT_LITERAL_INFO_H
