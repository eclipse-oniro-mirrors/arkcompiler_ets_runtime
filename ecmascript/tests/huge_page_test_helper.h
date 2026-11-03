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

#ifndef ECMASCRIPT_TESTS_HUGE_PAGE_TEST_HELPER_H
#define ECMASCRIPT_TESTS_HUGE_PAGE_TEST_HELPER_H

#include "ecmascript/mem/huge_mem_map_pool.h"

namespace panda::ecmascript {
#if ENABLE_HUGE_PAGE_MEM
inline constexpr size_t DEFAULT_HUGE_PAGE_QUOTA_FOR_TEST = 16;

struct HugePageProcessStateForTest {
    bool canTry;
    bool enabled;
};

// Configures process-global huge-page state without depending on sysfs or kernel VIP support.
PUBLIC_API void ConfigureHugePageStateForTest(
    bool enabled, size_t quotaLimit = DEFAULT_HUGE_PAGE_QUOTA_FOR_TEST);
PUBLIC_API HugePageProcessStateForTest DisableHugePageAndGetStateForTest(
    bool enabled, bool canTry, int err);
#endif
}  // namespace panda::ecmascript

#endif // ECMASCRIPT_TESTS_HUGE_PAGE_TEST_HELPER_H
