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

#ifndef ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_CONCURRENT_MARKER_H
#define ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_CONCURRENT_MARKER_H

#include "ecmascript/mem/global_cc/global_cc.h"
#include "ecmascript/mem/global_cc/global_mark_work_manager.h"

namespace panda::ecmascript {
class JSThread;
class TaggedObject;
class GlobalCCMarkObjectVisitor;

class GlobalCCMarker {
public:
    explicit GlobalCCMarker(GlobalCC *globalCC);
    virtual ~GlobalCCMarker() = default;

    void Mark();

    void ReMark();

    void MarkFromWriteBarrier(const JSThread *thread, TaggedObject *value);

    void ProcessMarkStack(uint32_t threadId);

    virtual void HandleMarkingFinished();

private:
    void InitializeMarking();
    void MarkRoots();
    // Mark() flips per-thread VM-root marking onto each JSThread (see FlipAllThreads);
    // the process-wide shared roots are marked on the daemon thread inside the initial STW.
    void MarkGlobalRoots();
    void MarkLocalRootsOnFlip(JSThread *thread);
    void DoMarking();

    void MarkJitCodeMaps();
    bool HandleWeakAggregate(WeakAggregate weakAggregate, GlobalCCMarkObjectVisitor &visitor);
    void MarkUntilFixPoint();
    static bool IsAlive(TaggedObject *obj);

    class RecursionScope {
    public:
        explicit RecursionScope(GlobalCCMarker *marker) : marker_(marker)
        {
            if (marker_->recursionDepth_++ != 0) {
                LOG_GC(FATAL) << "Recursion in GlobalCCMarker, depth: " << marker_->recursionDepth_;
            }
        }
        ~RecursionScope()
        {
            if (--marker_->recursionDepth_ != 0) {
                LOG_GC(FATAL) << "Recursion in GlobalCCMarker, depth: " << marker_->recursionDepth_;
            }
        }
    private:
        GlobalCCMarker *marker_;
    };

    GlobalCC *globalCC_ {nullptr};
    SharedHeap *sHeap_ {nullptr};
    int32_t recursionDepth_ {0};

    friend class GlobalCCMarkSuspendCallback;
    friend class GlobalCCMarkFlipFunction;
};
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_CONCURRENT_MARKER_H
