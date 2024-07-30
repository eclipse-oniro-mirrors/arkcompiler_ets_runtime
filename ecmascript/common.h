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

#ifndef ECMASCRIPT_COMMON_H
#define ECMASCRIPT_COMMON_H

#include <limits>

#include "libpandabase/macros.h"

namespace panda {
namespace ecmascript {
enum BarrierMode { SKIP_BARRIER, WRITE_BARRIER, READ_BARRIER };

/*
 * TriggerGCType is categorized according to the scope the GC expects to cover.
 * Different GC algorithms may be applied to different GC types.
 * For example, SemiSpace GC for young generation GC, Mark-Sweep-Compact for full GC, etc.
 */
enum TriggerGCType {
    EDEN_GC,
    // GC is expected to cover young space only;
    YOUNG_GC,
    // GC is expected to cover young space and necessary old spaces;
    OLD_GC,
    // GC is expected to cover all valid heap spaces;
    FULL_GC,
    // GC is expected to compress objects into appspawn space;
    APPSPAWN_FULL_GC,
    SHARED_GC,
    SHARED_FULL_GC,
    GC_TYPE_LAST
};

enum class GCReason : uint8_t {
    ALLOCATION_LIMIT,
    ALLOCATION_FAILED,
    IDLE,
    SWITCH_BACKGROUND,
    EXTERNAL_TRIGGER,
    WORKER_DESTRUCTION,
    OTHER,
};

enum class RequestAotMode : uint8_t {
    RE_COMPILE_ON_IDLE = 0
};

#define SCOPE_LIST(V)                    \
    V(TotalGC)                           \
    V(Initialize)                        \
    V(ConcurrentMark)                    \
    V(WaitConcurrentMarkFinished)        \
    V(ReMark)                            \
    V(Mark)                              \
    V(MarkRoots)                         \
    V(ProcessMarkStack)                  \
    V(ProcessSharedGCRSetWorkList)       \
    V(Sweep)                             \
    V(ClearNativeObject)                 \
    V(Evacuate)                          \
    V(Finish)                            \
    V(UpdateReference)                   \
    V(EvacuateSpace)                     \
    V(SuspendAll)                        \
    V(ResumeAll)                         \
    V(ProceeWorkload)                    \
    V(UpdateRoot)                        \
    V(UpdateWeekRef)                     \
    V(EvacuateRegion)                    \
    V(WaitFinish)

#define RECORD_DATA(V)                   \
    V(START_OBJ_SIZE)                    \
    V(END_OBJ_SIZE)                      \
    V(START_COMMIT_SIZE)                 \
    V(END_COMMIT_SIZE)                   \
    V(START_EDEN_OBJ_SIZE)               \
    V(START_YOUNG_OBJ_SIZE)              \
    V(SEMI_ALIVE_SIZE)                   \
    V(SEMI_COMMIT_SIZE)                  \
    V(SEMI_PROMOTE_SIZE)                 \
    V(EDEN_ALIVE_SIZE)                   \
    V(EDEN_COMMIT_SIZE)                  \
    V(EDEN_PROMOTE_SIZE)                 \
    V(YOUNG_ALIVE_SIZE)                  \
    V(YOUNG_COMMIT_SIZE)                 \
    V(YOUNG_PROMOTE_SIZE)                \
    V(OLD_ALIVE_SIZE)                    \
    V(OLD_COMMIT_SIZE)                   \
    V(COMPRESS_ALIVE_SIZE)               \
    V(COMPRESS_COMMIT_SIZE)              \
    V(START_NATIVE_POINTER_NUM)          \
    V(COLLECT_REGION_SET_SIZE)           \
    SEMI_RECORD_DATA(V)                  \
    PARTIAL_RECORD_DATA(V)               \
    FULL_RECORD_DATA(V)                  \
    SHARED_RECORD_DATA(V)

#define RECORD_DURATION(V)               \
    V(SEMI_MIN_PAUSE)                    \
    V(SEMI_MAX_PAUSE)                    \
    V(SEMI_TOTAL_PAUSE)                  \
    V(EDEN_MIN_PAUSE)                    \
    V(EDEN_MAX_PAUSE)                    \
    V(EDEN_TOTAL_PAUSE)                  \
    V(YOUNG_MIN_PAUSE)                   \
    V(YOUNG_MAX_PAUSE)                   \
    V(YOUNG_TOTAL_PAUSE)                 \
    V(OLD_MIN_PAUSE)                     \
    V(OLD_MAX_PAUSE)                     \
    V(OLD_TOTAL_PAUSE)                   \
    V(COMPRESS_MIN_PAUSE)                \
    V(COMPRESS_MAX_PAUSE)                \
    V(COMPRESS_TOTAL_PAUSE)              \
    V(SHARED_MIN_PAUSE)                  \
    V(SHARED_MAX_PAUSE)                  \
    V(SHARED_TOTAL_PAUSE)

#define SEMI_RECORD_DATA(V)              \
    V(SEMI_COUNT)                        \
    V(SEMI_TOTAL_ALIVE)                  \
    V(SEMI_TOTAL_COMMIT)                 \
    V(SEMI_TOTAL_PROMOTE)

#define PARTIAL_RECORD_DATA(V)           \
    V(EDEN_COUNT)                        \
    V(EDEN_TOTAL_ALIVE)                  \
    V(EDEN_TOTAL_COMMIT)                 \
    V(EDEN_TOTAL_PROMOTE)                \
    V(YOUNG_COUNT)                       \
    V(YOUNG_TOTAL_ALIVE)                 \
    V(YOUNG_TOTAL_COMMIT)                \
    V(YOUNG_TOTAL_PROMOTE)               \
    V(OLD_COUNT)                         \
    V(OLD_TOTAL_ALIVE)                   \
    V(OLD_TOTAL_COMMIT)

#define FULL_RECORD_DATA(V)              \
    V(COMPRESS_COUNT)                    \
    V(COMPRESS_TOTAL_ALIVE)              \
    V(COMPRESS_TOTAL_COMMIT)

#define SHARED_RECORD_DATA(V)            \
    V(SHARED_COUNT)                      \
    V(SHARED_ALIVE_SIZE)                 \
    V(SHARED_TOTAL_ALIVE)                \
    V(SHARED_COMMIT_SIZE)                \
    V(SHARED_TOTAL_COMMIT)

#define TRACE_GC_SPEED(V)                \
    V(UPDATE_REFERENCE_SPEED)            \
    V(OLD_CLEAR_NATIVE_OBJ_SPEED)        \
    V(OLD_EVACUATE_SPACE_SPEED)          \
    V(EDEN_CLEAR_NATIVE_OBJ_SPEED)       \
    V(EDEN_UPDATE_REFERENCE_SPEED)       \
    V(EDEN_EVACUATE_SPACE_SPEED)         \
    V(YOUNG_CLEAR_NATIVE_OBJ_SPEED)      \
    V(YOUNG_UPDATE_REFERENCE_SPEED)      \
    V(YOUNG_EVACUATE_SPACE_SPEED)        \
    V(MARK_SPEED)                        \
    V(SWEEP_SPEED)

constexpr uint32_t NUM_MANDATORY_JSFUNC_ARGS = 3;
constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();

using Address = uintptr_t;

#define PUBLIC_API PANDA_PUBLIC_API

#ifdef NDEBUG
#define DUMP_API_ATTR __attribute__((unused))
#else
#ifndef PANDA_TARGET_WINDOWS
#define DUMP_API_ATTR __attribute__((visibility ("default"), used))
#else
#define DUMP_API_ATTR __attribute__((unused))
#endif
#endif

#ifdef PANDA_TARGET_32
#define STATIC_ASSERT_EQ_ARCH32(a, b) static_assert(a == b)
#else
#define STATIC_ASSERT_EQ_ARCH32(a, b) static_assert(true)
#endif

#ifdef PANDA_TARGET_64
#define STATIC_ASSERT_EQ_ARCH64(a, b) static_assert(a == b)
#else
#define STATIC_ASSERT_EQ_ARCH64(a, b) static_assert(true)
#endif

#if defined(PANDA_TARGET_WINDOWS) || defined(PANDA_TARGET_MACOS) || defined(PANDA_TARGET_IOS)
#define WIN_OR_MAC_OR_IOS_PLATFORM true
#else
#define WIN_OR_MAC_OR_IOS_PLATFORM false
#endif

#define STATIC_ASSERT_EQ_ARCH(expect, valueArch32, valueArch64) \
    STATIC_ASSERT_EQ_ARCH32(expect, valueArch32);               \
    STATIC_ASSERT_EQ_ARCH64(expect, valueArch64)
}  // namespace ecmascript
}  // namespace panda

#endif  // ECMASCRIPT_COMMON_H
