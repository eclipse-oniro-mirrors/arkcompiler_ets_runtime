/*
 * Copyright (c) 2023-2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ecmascript/base/json_helper.h"
#include "ecmascript/base/utf_helper.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace panda::ecmascript::base {
constexpr unsigned char CODE_SPACE = 0x20;
constexpr char ZERO_FIRST = static_cast<char>(0xc0); // \u0000 => c0 80
constexpr char ALONE_SURROGATE_3B_FIRST = static_cast<char>(0xed);
constexpr char ALONE_SURROGATE_3B_SECOND_START = static_cast<char>(0xa0);
constexpr char ALONE_SURROGATE_3B_SECOND_END = static_cast<char>(0xbf);
constexpr char ALONE_SURROGATE_3B_THIRD_START = static_cast<char>(0x80);
constexpr char ALONE_SURROGATE_3B_THIRD_END = static_cast<char>(0xbf);

bool JsonHelper::IsFastValueToQuotedString(const CString& str)
{
    for (const auto ch : str) {
        switch (ch) {
            case '\"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
            case ZERO_FIRST:
            case ALONE_SURROGATE_3B_FIRST:
                return false;
            default:
                if (ch > 0 && ch < CODE_SPACE) {
                    return false;
                }
                break;
        }
    }
    return true;
}

void JsonHelper::AppendValueToQuotedString(const CString& str, CString& output)
{
    output += "\"";
    bool isFast = IsFastValueToQuotedString(str); // fast mode
    if (isFast) {
        output += str;
        output += "\"";
        return;
    }
    for (uint32_t i = 0; i < str.size(); ++i) {
        auto ch = str[i];
        switch (ch) {
            case '\"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            case ZERO_FIRST:
                output += "\\u0000";
                ++i;
                break;
            case ALONE_SURROGATE_3B_FIRST:
                if (i + 2 < str.size() && // 2: Check 2 more characters
                    str[i + 1] >= ALONE_SURROGATE_3B_SECOND_START && // 1: The first character after ch
                    str[i + 1] <= ALONE_SURROGATE_3B_SECOND_END && // 1: The first character after ch
                    str[i + 2] >= ALONE_SURROGATE_3B_THIRD_START && // 2: The second character after ch
                    str[i + 2] <= ALONE_SURROGATE_3B_THIRD_END) {   // 2: The second character after ch
                    auto unicodeRes = utf_helper::ConvertUtf8ToUnicodeChar(
                        reinterpret_cast<const uint8_t*>(str.c_str() + i), 3); // 3: Parse 3 characters
                    AppendUnicodeEscape(static_cast<int>(unicodeRes.first), output);
                    i += 2; // 2 : Skip 2 characters
                    break;
                }
                [[fallthrough]];
            default:
                if (ch > 0 && ch < CODE_SPACE) {
                    AppendUnicodeEscape(static_cast<int>(ch), output);
                } else {
                    output += ch;
                }
        }
    }
    output += "\"";
}

void JsonHelper::AppendValueToQuotedString(const Span<const uint8_t>& sp, CString& output)
{
    output += "\"";
    for (uint32_t i = 0; i < sp.size(); ++i) {
        const char ch = static_cast<char>(sp[i]);
        switch (ch) {
            case '\"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            case ZERO_FIRST:
                output += "\\u0000";
                ++i;
                break;
            case ALONE_SURROGATE_3B_FIRST:
                if (i + 2 < sp.size() && // 2: Check 2 more characters
                    static_cast<char>(sp[i + 1]) >= ALONE_SURROGATE_3B_SECOND_START && // 1: The 1st character after ch
                    static_cast<char>(sp[i + 1]) <= ALONE_SURROGATE_3B_SECOND_END && // 1: The 1st character after ch
                    static_cast<char>(sp[i + 2]) >= ALONE_SURROGATE_3B_THIRD_START && // 2: The 2nd character after ch
                    static_cast<char>(sp[i + 2]) <= ALONE_SURROGATE_3B_THIRD_END) {   // 2: The 2nd character after ch
                    auto unicodeRes = utf_helper::ConvertUtf8ToUnicodeChar(sp.data() + i, 3); // 3: Parse 3 characters
                    AppendUnicodeEscape(static_cast<int>(unicodeRes.first), output);
                    i += 2; // 2 : Skip 2 characters
                    break;
                }
                [[fallthrough]];
            default:
                if (ch > 0 && ch < CODE_SPACE) {
                    AppendUnicodeEscape(static_cast<int>(ch), output);
                } else {
                    output += ch;
                }
        }
    }
    output += "\"";
}
} // namespace panda::ecmascript::base