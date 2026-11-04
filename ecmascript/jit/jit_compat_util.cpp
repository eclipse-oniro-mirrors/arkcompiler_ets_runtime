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

#include "ecmascript/jit/jit_compat_util.h"

namespace panda::ecmascript {

uint32_t JitCompatUtil::reservedCounter_ = 0;

uint32_t JitCompatUtil::CalcHotnessBias(uint32_t hotness, uint32_t threshold)
{
    if (hotness >= threshold) {
        return hotness - threshold;
    }
    return 0;
}

bool JitCompatUtil::IsHotnessOverflow(uint32_t hotness, uint32_t threshold)
{
    return hotness > threshold;
}

std::string JitCompatUtil::FormatJitTag(const std::string &methodName, uint32_t hotness)
{
    return methodName + ":" + std::to_string(hotness);
}

void JitCompatUtil::ResetReservedCounter()
{
    reservedCounter_ = 0;
}

uint32_t JitCompatUtil::GetReservedCounter()
{
    return reservedCounter_;
}

}  // namespace panda::ecmascript
