/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "common_components/heap/space/old_space.h"
#if defined(COMMON_SANITIZER_SUPPORT)
#include "common_components/base/asan_interface.h"
#endif

namespace common {
void OldSpace::DumpRegionStats() const
{
    size_t oldRegions =
        tlOldRegionList_.GetRegionCount() + recentFullOldRegionList_.GetRegionCount() + oldRegionList_.GetRegionCount();
    size_t oldUnits =
        tlOldRegionList_.GetUnitCount() + recentFullOldRegionList_.GetUnitCount() + oldRegionList_.GetUnitCount();
    size_t oldSize = oldUnits * RegionDesc::UNIT_SIZE;
    size_t allocFromSize = tlOldRegionList_.GetAllocatedSize() + recentFullOldRegionList_.GetAllocatedSize() +
                           oldRegionList_.GetAllocatedSize();

    VLOG(DEBUG, "\told-regions %zu: %zu units (%zu B, alloc %zu)",
        oldRegions,  oldUnits, oldSize, allocFromSize);
}
} // namespace common
