/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#include "ecmascript/mem/gc_key_stats.h"

#include <iostream>
#include <cstring>

#ifdef ENABLE_HISYSEVENT
#include "hisysevent.h"
#endif
#if !defined(PANDA_TARGET_WINDOWS) && !defined(PANDA_TARGET_MACOS) && !defined(PANDA_TARGET_IOS)
#include <sys/resource.h>
#endif

#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/pgo_profiler/pgo_profiler_manager.h"

namespace panda::ecmascript {
using PGOProfilerManager = pgo::PGOProfilerManager;
using Clock = std::chrono::high_resolution_clock;

bool GCKeyStats::CheckIfMainThread() const
{
#if !defined(PANDA_TARGET_WINDOWS) && !defined(PANDA_TARGET_MACOS) && !defined(PANDA_TARGET_IOS)
    return getpid() == syscall(SYS_gettid);
#else
    return true;
#endif
}

bool GCKeyStats::CheckIfKeyPauseTime() const
{
    return gcStats_->GetScopeDuration(GCStats::Scope::ScopeId::TotalGC) >= KEY_PAUSE_TIME;
}

void GCKeyStats::AddGCStatsToKey()
{
    LOG_GC(INFO) << "GCKeyStats AddGCStatsToKey!";

    recordCount_++;

    AddRecordDataStats(RecordKeyData::GC_TOTAL_MEM_USED,
        SizeToIntKB(gcStats_->GetRecordData(RecordData::END_OBJ_SIZE)));
    AddRecordDataStats(RecordKeyData::GC_TOTAL_MEM_COMMITTED,
        SizeToIntKB(gcStats_->GetRecordData(RecordData::END_COMMIT_SIZE)));
    AddRecordDataStats(RecordKeyData::GC_ACTIVE_MEM_USED,
        SizeToIntKB(gcStats_->GetRecordData(RecordData::YOUNG_ALIVE_SIZE)));
    AddRecordDataStats(RecordKeyData::GC_ACTIVE_MEM_COMMITTED,
        SizeToIntKB(gcStats_->GetRecordData(RecordData::YOUNG_COMMIT_SIZE)));
    AddRecordDataStats(RecordKeyData::GC_OLD_MEM_USED,
        SizeToIntKB(gcStats_->GetRecordData(RecordData::OLD_ALIVE_SIZE)));
    AddRecordDataStats(RecordKeyData::GC_OLD_MEM_COMMITTED,
        SizeToIntKB(gcStats_->GetRecordData(RecordData::OLD_COMMIT_SIZE)));

    AddRecordDataStats(RecordKeyData::GC_HUGE_MEM_USED,
        SizeToIntKB(heap_->GetHugeObjectSpace()->GetHeapObjectSize()));
    AddRecordDataStats(RecordKeyData::GC_HUGE_MEM_COMMITTED,
        SizeToIntKB(heap_->GetHugeObjectSpace()->GetCommittedSize()));

    AddRecordKeyDuration(RecordKeyDuration::GC_TOTAL_TIME,
        gcStats_->GetScopeDuration(GCStats::Scope::ScopeId::TotalGC));
    AddRecordKeyDuration(RecordKeyDuration::GC_MARK_TIME,
        gcStats_->GetScopeDuration(GCStats::Scope::ScopeId::Mark));
    AddRecordKeyDuration(RecordKeyDuration::GC_EVACUATE_TIME,
        gcStats_->GetScopeDuration(GCStats::Scope::ScopeId::Evacuate));

    if (CheckLastSendTimeIfSend() && CheckCountIfSend()) {
        SendSysEvent();
        PrintKeyStatisticResult();
        InitializeRecordList();
    }
}

void GCKeyStats::SendSysEvent() const
{
#ifdef ENABLE_HISYSEVENT
    int32_t ret = HiSysEventWrite(OHOS::HiviewDFX::HiSysEvent::Domain::ARKTS_RUNTIME,
        "ARK_STATS_GC",
        OHOS::HiviewDFX::HiSysEvent::EventType::STATISTIC,
        "BUNDLE_NAME", PGOProfilerManager::GetInstance()->GetBundleName(),
        "PID", getpid(),
        "TID", syscall(SYS_gettid),
        "GC_TOTAL_COUNT", gcCount_,
        "GC_TOTAL_TIME", static_cast<int>(GetRecordKeyDuration(RecordKeyDuration::GC_TOTAL_TIME)),
        "GC_MARK_TIME", static_cast<int>(GetRecordKeyDuration(RecordKeyDuration::GC_MARK_TIME)),
        "GC_EVACUATE_TIME", static_cast<int>(GetRecordKeyDuration(RecordKeyDuration::GC_EVACUATE_TIME)),
        "GC_LONG_TIME", recordCount_,
        "GC_TOTAL_MEM_USED", GetRecordDataStats(RecordKeyData::GC_TOTAL_MEM_USED),
        "GC_TOTAL_MEM_COMMITTED", GetRecordDataStats(RecordKeyData::GC_TOTAL_MEM_COMMITTED),
        "GC_ACTIVE_MEM_USED", GetRecordDataStats(RecordKeyData::GC_ACTIVE_MEM_USED),
        "GC_ACTIVE_MEM_COMMITTED", GetRecordDataStats(RecordKeyData::GC_ACTIVE_MEM_COMMITTED),
        "GC_OLD_MEM_USED", GetRecordDataStats(RecordKeyData::GC_OLD_MEM_USED),
        "GC_OLD_MEM_COMMITTED", GetRecordDataStats(RecordKeyData::GC_OLD_MEM_COMMITTED),
        "GC_HUGE_MEM_USED", GetRecordDataStats(RecordKeyData::GC_HUGE_MEM_USED),
        "GC_HUGE_MEM_COMMITTED", GetRecordDataStats(RecordKeyData::GC_HUGE_MEM_COMMITTED));
    if (ret != 0) {
        LOG_GC(INFO) << "GCKeyStats HiSysEventWrite Failed! ret = " << ret;
    }
#endif
}

void GCKeyStats::PrintKeyStatisticResult() const
{
    LOG_GC(INFO) << "/******************* GCKeyStats HiSysEvent statistic: *******************/";
}

void GCKeyStats::InitializeRecordList()
{
    gcCount_ = 0;
    recordCount_ = 0;
    std::fill(recordDurationStats_, recordDurationStats_ + (uint8_t)RecordKeyDuration::NUM_OF_KEY_DURATION, 0.0f);
    std::fill(recordDataStats_, recordDataStats_ + (uint8_t)RecordKeyData::NUM_OF_KEY_DATA, 0);
    lastSendTimestamp_ = Clock::now();
}

} // namespace panda::ecmascript