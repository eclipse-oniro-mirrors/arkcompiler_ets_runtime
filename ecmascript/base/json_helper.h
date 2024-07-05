/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_BASE_JSON_HELPER_H
#define ECMASCRIPT_BASE_JSON_HELPER_H

#include "ecmascript/js_handle.h"
#include "ecmascript/mem/c_string.h"
#include "ecmascript/property_attributes.h"

namespace panda::ecmascript::base {

class JsonHelper {
public:
    enum class TransformType : uint32_t {
        SENDABLE = 1,
        NORMAL = 2,
        BIGINT = 3
    };

    enum class ParseOptions : uint8_t {
        DEFAULT,
        PARSEASBIGINT,
        ALWAYSPARSEASBIGINT
    };

    static inline bool IsTypeSupportBigInt(TransformType type)
    {
        return type == TransformType::SENDABLE || type == TransformType::BIGINT;
    }

    static CString ValueToQuotedString(CString str);

    static bool IsFastValueToQuotedString(const char *value);

    static inline bool CompareKey(const std::pair<JSHandle<JSTaggedValue>, PropertyAttributes> &a,
                                  const std::pair<JSHandle<JSTaggedValue>, PropertyAttributes> &b)
    {
        return a.second.GetDictionaryOrder() < b.second.GetDictionaryOrder();
    }

    static inline bool CompareNumber(const JSHandle<JSTaggedValue> &a, const JSHandle<JSTaggedValue> &b)
    {
        return a->GetNumber() < b->GetNumber();
    }
};

}  // namespace panda::ecmascript::base

#endif  // ECMASCRIPT_BASE_UTF_JSON_H