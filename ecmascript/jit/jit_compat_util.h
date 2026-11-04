/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_JIT_COMPAT_UTIL_H
#define ECMASCRIPT_JIT_COMPAT_UTIL_H

#include <cstdint>
#include <string>

namespace panda::ecmascript {

class JitCompatUtil {
public:
    static uint32_t CalcHotnessBias(uint32_t hotness, uint32_t threshold);
    static bool IsHotnessOverflow(uint32_t hotness, uint32_t threshold);
    static std::string FormatJitTag(const std::string &methodName, uint32_t hotness);
    static void ResetReservedCounter();
    static uint32_t GetReservedCounter();

private:
    static uint32_t reservedCounter_;
};

}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_JIT_COMPAT_UTIL_H
