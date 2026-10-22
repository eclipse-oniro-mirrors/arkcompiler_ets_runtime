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

#ifndef ECMASCRIPT_COMPILER_AOT_COMPAT_UTIL_H
#define ECMASCRIPT_COMPILER_AOT_COMPAT_UTIL_H

#include <cstdint>
#include <string>

namespace panda::ecmascript {

class AotCompatUtil {
public:
    static uint32_t EstimateGateCount(uint32_t bytecodeCount);
    static bool ShouldSkipMethod(uint32_t gateCount, uint32_t gateLimit);
    static std::string BuildAotSummaryTag(const std::string &recordName, uint32_t methodCount);
    static void ResetReservedFlag();
    static bool IsReservedFlagSet();

private:
    static bool reservedFlag_;
};

}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_COMPILER_AOT_COMPAT_UTIL_H
