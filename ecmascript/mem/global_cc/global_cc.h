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

#ifndef ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_CC_H
#define ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_CC_H

namespace panda::ecmascript {
class SharedHeap;
class GlobalCCMarker;
class GlobalCCWorkManager;

class GlobalCC {
public:
    explicit GlobalCC(SharedHeap *sharedHeap) : sharedHeap_(sharedHeap) {}
    ~GlobalCC() = default;
    SharedHeap *GetSharedHeap() const { return sharedHeap_; }

    GlobalCCMarker *GetMarker() const { return marker_; }
    GlobalCCWorkManager *GetWorkManager() const { return workManager_; }
    void SetMarker(GlobalCCMarker *marker) { marker_ = marker; }
    void SetWorkManager(GlobalCCWorkManager *workManager) { workManager_ = workManager; }

private:
    SharedHeap *sharedHeap_ {nullptr};
    GlobalCCMarker *marker_ {nullptr};
    GlobalCCWorkManager *workManager_ {nullptr};
};
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_CC_H
