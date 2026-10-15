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

#include "ecmascript/aot_literal_info.h"
#include "ecmascript/tagged_array-inl.h"

namespace panda::ecmascript {

void AOTLiteralInfo::InitializeWithSpecialValue(JSTaggedValue initValue, uint32_t capacity, uint32_t extraLength)
{
    TaggedArray::InitializeWithSpecialValue(initValue, capacity + RESERVED_LENGTH, extraLength);
    SetIhcPrimitive(JSTaggedValue::Undefined());
    SetChcPrimitive(JSTaggedValue::Undefined());
    SetLiteralType(JSTaggedValue(INVALID_LITERAL_TYPE));
}

void AOTLiteralInfo::SetObjectToCache(JSThread *thread, uint32_t index, JSTaggedValue value)
{
    Set(thread, index, value);
}

} // namespace panda::ecmascript
