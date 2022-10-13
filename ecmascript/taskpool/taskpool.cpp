/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include "ecmascript/taskpool/taskpool.h"

#if defined(PANDA_TARGET_WINDOWS)
#include <sysinfoapi.h>
#elif defined(PANDA_TARGET_MACOS)
#include "sys/sysctl.h"
#else
#include "sys/sysinfo.h"
#endif

namespace panda::ecmascript {
Taskpool *Taskpool::GetCurrentTaskpool()
{
    static Taskpool taskpool;
    return &taskpool;
}

void Taskpool::Initialize(int threadNum)
{
    os::memory::LockHolder lock(mutex_);
    if (isInitialized_++ <= 0) {
        runner_ = std::make_unique<Runner>(TheMostSuitableThreadNum(threadNum));
    }
}

void Taskpool::Destroy()
{
    os::memory::LockHolder lock(mutex_);
    if (isInitialized_ <= 0) {
        return;
    }
    isInitialized_--;
    if (isInitialized_ == 0) {
        runner_->TerminateThread();
    } else {
        runner_->TerminateTask();
    }
}

uint32_t Taskpool::TheMostSuitableThreadNum(uint32_t threadNum) const
{
    if (threadNum > 0) {
        return std::min<uint32_t>(threadNum, MAX_TASKPOOL_THREAD_NUM);
    }
#ifdef PANDA_TARGET_WINDOWS
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uint32_t numOfCpuCore = info.dwNumberOfProcessors;
#elif PANDA_TARGET_MACOS
    uint32_t numOfCpuCore = MAC_MAX_THREADS_NUM;
#else
    uint32_t numOfCpuCore = static_cast<uint32_t>(get_nprocs() / 2);
#endif
    uint32_t numOfThreads = std::min<uint32_t>(numOfCpuCore, MAX_TASKPOOL_THREAD_NUM);
    return std::max<uint32_t>(numOfThreads, MIN_TASKPOOL_THREAD_NUM);
}
}  // namespace panda::ecmascript