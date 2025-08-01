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
#include "common_components/heap/collector/collector_proxy.h"

namespace common {
void CollectorProxy::Init(const RuntimeParam& param)
{
    wCollector_.Init(param);

    if (currentCollector_ == nullptr) {
        currentCollector_ = &wCollector_;
    }
}

void CollectorProxy::Fini() { wCollector_.Fini(); }

void CollectorProxy::RunGarbageCollection(uint64_t gcIndex, GCReason reason, GCType gcType)
{
    switch (reason) {
        case GC_REASON_HEU:
        case GC_REASON_YOUNG:
        case GC_REASON_BACKUP:
            currentCollector_ = &wCollector_;
            break;
        case GC_REASON_OOM:
        case GC_REASON_FORCE:
            currentCollector_ = &wCollector_;
            break;
        default:
            currentCollector_ = &wCollector_;
            break;
    }
    currentCollector_->MarkGCStart();
    currentCollector_->RunGarbageCollection(gcIndex, reason, gcType);
}
} // namespace common
