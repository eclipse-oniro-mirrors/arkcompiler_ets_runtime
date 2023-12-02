/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_PGO_PROFILER_H
#define ECMASCRIPT_PGO_PROFILER_H

#include <chrono>
#include <memory>

#include "ecmascript/common.h"
#include "ecmascript/elements.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/jspandafile/method_literal.h"
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/mem/region.h"
#include "ecmascript/mem/visitor.h"
#include "ecmascript/pgo_profiler/types/pgo_type_generator.h"
#include "ecmascript/platform/mutex.h"
#include "ecmascript/taskpool/task.h"
#include "ecmascript/pgo_profiler/pgo_utils.h"
#include "ecmascript/pgo_profiler/types/pgo_profile_type.h"

namespace panda::ecmascript {
class ProfileTypeInfo;
class JSFunction;

namespace pgo {
class PGORecordDetailInfos;

enum class SampleMode : uint8_t {
    HOTNESS_MODE,
    CALL_MODE,
};

class PGOProfiler {
public:
    NO_COPY_SEMANTIC(PGOProfiler);
    NO_MOVE_SEMANTIC(PGOProfiler);

    PGOProfiler(EcmaVM *vm, bool isEnable);

    virtual ~PGOProfiler();

    static ProfileType CreateRecordProfileType(ApEntityId abcId, ApEntityId classId);
    void ProfileCreateObject(JSTaggedType object, ApEntityId abcId, int32_t traceId);
    void ProfileDefineClass(JSTaggedType ctor);
    void ProfileDefineGetterSetter(
        JSHClass *receverHClass, JSHClass *holderHClass, const JSHandle<JSTaggedValue> &func, int32_t pcOffset);
    void ProfileClassRootHClass(JSTaggedType ctor, JSTaggedType rootHcValue,
                                ProfileType::Kind kind = ProfileType::Kind::ClassId);
    void UpdateRootProfileType(JSHClass *oldHClass, JSHClass *newHClass);

    void SetSaveTimestamp(std::chrono::system_clock::time_point timestamp)
    {
        saveTimestamp_ = timestamp;
    }

    void PGOPreDump(JSTaggedType func);
    void PGODump(JSTaggedType func);

    void WaitPGODumpPause();
    void WaitPGODumpResume();
    void WaitPGODumpFinish();

    void HandlePGOPreDump();
    void HandlePGODump(bool force);

    void ProcessReferences(const WeakRootVisitor &visitor);
    void Iterate(const RootVisitor &visitor);

    void UpdateTrackArrayLength(JSTaggedValue trackInfoVal, uint32_t newSize);
    void UpdateTrackSpaceFlag(TaggedObject *object, RegionSpaceFlag spaceFlag);
    void UpdateTrackElementsKind(JSTaggedValue trackInfoVal, ElementsKind newKind);
    void UpdateTrackInfo(JSTaggedValue trackInfoVal);

    JSTaggedValue TryFindKeyInPrototypeChain(TaggedObject *currObj, JSHClass *currHC, JSTaggedValue key);

private:
    static constexpr uint32_t MERGED_EVERY_COUNT = 50;
    static constexpr uint32_t MS_PRE_SECOND = 1000;
    enum class BCType : uint8_t {
        STORE,
        LOAD,
    };

    enum class State : uint8_t {
        STOP,
        PAUSE,
        START,
        FORCE_SAVE,
        FORCE_SAVE_PAUSE,
    };

    void ProfileBytecode(ApEntityId abcId, const CString &recordName, JSTaggedValue value);
    bool PausePGODump();

    void DumpICByName(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset, uint32_t slotId,
                      ProfileTypeInfo *profileTypeInfo, BCType type);
    void DumpICByValue(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                       uint32_t slotId, ProfileTypeInfo *profileTypeInfo, BCType type);

    void DumpICByNameWithPoly(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                              JSTaggedValue cacheValue, BCType type);
    void DumpICByValueWithPoly(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                               JSTaggedValue cacheValue, BCType type);

    void DumpICByNameWithHandler(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                                 JSHClass *hclass, JSTaggedValue secondValue, BCType type);
    void DumpICByValueWithHandler(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                                  JSHClass *hclass, JSTaggedValue secondValue, BCType type);
    void DumpByForce();

    void DumpOpType(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset, uint32_t slotId,
                    ProfileTypeInfo *profileTypeInfo);
    void DumpDefineClass(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                         uint32_t slotId, ProfileTypeInfo *profileTypeInfo);
    void DumpCreateObject(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                          uint32_t slotId, ProfileTypeInfo *profileTypeInfo, int32_t traceId);
    void DumpCall(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset, uint32_t slotId,
                  ProfileTypeInfo *profileTypeInfo);
    void DumpNewObjRange(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                         uint32_t slotId, ProfileTypeInfo *profileTypeInfo);
    void DumpGetIterator(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                         uint32_t slotId, ProfileTypeInfo *profileTypeInfo);
    void DumpInstanceof(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                         uint32_t slotId, ProfileTypeInfo *profileTypeInfo);

    void UpdateLayout(JSHClass *hclass);
    void AddTranstionLayout(JSHClass *parent, JSHClass *child);
    void AddTranstionObjectInfo(ProfileType recordType, EntityId methodId, int32_t bcOffset, JSHClass *receiver,
        JSHClass *hold, JSHClass *holdTra);

    void AddObjectInfo(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset,
                       JSHClass *receiver, JSHClass *hold, JSHClass *holdTra);
    void AddObjectInfoWithMega(ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset);
    void AddBuiltinsInfo(
        ApEntityId abcId, const CString &recordName, EntityId methodId, int32_t bcOffset, JSHClass *receiver,
        OnHeapMode onHeap = OnHeapMode::NONE);

    ProfileType GetProfileType(JSTaggedType root, JSTaggedType child);
    ProfileType GetOrInsertProfileType(JSTaggedType root, JSTaggedType child);
    void InsertProfileType(JSTaggedType root, JSTaggedType child, ProfileType traceType);

    JSTaggedValue PopFromProfileQueue();

    class PGOProfilerTask : public Task {
    public:
        explicit PGOProfilerTask(PGOProfiler *profiler, int32_t id)
            : Task(id), profiler_(profiler){};
        virtual ~PGOProfilerTask() override = default;

        bool Run([[maybe_unused]] uint32_t threadIndex) override
        {
            profiler_->HandlePGODump(profiler_->isForce_);
            return true;
        }

        NO_COPY_SEMANTIC(PGOProfilerTask);
        NO_MOVE_SEMANTIC(PGOProfilerTask);
    private:
        PGOProfiler *profiler_;
    };

    class WorkList;
    class WorkNode {
    public:
        WorkNode(JSTaggedType value) : value_(value) {}
        void SetPrev(WorkNode *prev)
        {
            prev_ = prev;
        }

        void SetNext(WorkNode *next)
        {
            next_ = next;
        }

        void SetValue(JSTaggedType value)
        {
            value_ = value;
        }

        void SetWorkList(WorkList *workList)
        {
            workList_ = workList;
        }

        WorkNode *GetPrev() const
        {
            return prev_;
        }

        WorkNode *GetNext() const
        {
            return next_;
        }

        JSTaggedType GetValue() const
        {
            return value_;
        }

        uintptr_t GetValueAddr() const
        {
            return reinterpret_cast<uintptr_t>(&value_);
        }

        WorkList *GetWorkList() const
        {
            return workList_;
        }

    private:
        WorkList *workList_ { nullptr };
        WorkNode *prev_ { nullptr };
        WorkNode *next_ { nullptr };
        JSTaggedType value_ { JSTaggedValue::Undefined().GetRawData() };
    };

    class WorkList {
    public:
        using Callback = std::function<void(WorkNode *node)>;
        bool IsEmpty() const
        {
            return first_ == nullptr;
        }
        void PushBack(WorkNode *node);
        WorkNode *PopFront();
        void Remove(WorkNode *node);
        void Iterate(Callback callback) const;
    private:
        WorkNode *first_ { nullptr };
        WorkNode *last_ { nullptr };
    };

    static ApEntityId GetMethodAbcId(JSFunction *jsFunction);
    ProfileType GetRecordProfileType(JSFunction *jsFunction, const CString &recordName);
    ProfileType GetRecordProfileType(ApEntityId abcId, const CString &recordName);
    ProfileType GetRecordProfileType(const std::shared_ptr<JSPandaFile> &pf, ApEntityId abcId,
                                     const CString &recordName);

    void Reset(bool isEnable);

    EcmaVM *vm_ { nullptr };
    bool isEnable_ { false };
    bool isForce_ {false};
    State state_ { State::STOP };
    uint32_t methodCount_ { 0 };
    std::chrono::system_clock::time_point saveTimestamp_;
    Mutex mutex_;
    ConditionVariable condition_;
    WorkList dumpWorkList_;
    WorkList preDumpWorkList_;
    CMap<JSTaggedType, PGOTypeGenerator *> tracedProfiles_;
    std::unique_ptr<PGORecordDetailInfos> recordInfos_;
    friend class PGOProfilerManager;
};
} // namespace pgo
} // namespace panda::ecmascript
#endif // ECMASCRIPT_PGO_PROFILER_H
