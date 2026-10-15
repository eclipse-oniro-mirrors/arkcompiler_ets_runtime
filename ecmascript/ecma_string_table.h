/*
 * Copyright (c) 2021-2024 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_STRING_TABLE_H
#define ECMASCRIPT_STRING_TABLE_H

#include <array>
#include "common_components/taskpool/task.h"
#include "ecmascript/daemon/daemon_thread.h"
#include "ecmascript/ecma_string.h"
#include "ecmascript/js_tagged_value_wrapper.h"
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/mem/space.h"
#include "ecmascript/mem/visitor.h"
#include "ecmascript/platform/mutex.h"
#include "ecmascript/tagged_array.h"
#include "ecmascript/string/base_string.h"
#include "ecmascript/string/chained_hash_map.h"

namespace panda::ecmascript {
class EcmaString;
class EcmaVM;
class JSPandaFile;
class JSThread;

class EcmaStringTable;
class EcmaStringTableCleaner {
public:
    using IteratorPtr = std::shared_ptr<std::atomic<uint32_t>>;
    EcmaStringTableCleaner(EcmaStringTable *stringTable) : stringTable_(stringTable) {}
    ~EcmaStringTableCleaner()
    {
        stringTable_ = nullptr;
    }
    void PostSweepWeakRefTask(const WeakRootVisitor &visitor);
    void JoinAndWaitSweepWeakRefTask(const WeakRootVisitor &visitor);
    void WaitSweepWeakRefTaskFinished();

    void PostConcurrentSweepWeakRefTask(const WeakRootVisitor &visitor);
    void WaitConcurrentSweepWeakRefTaskAndSuspend(JSThread *thread);
    void WaitConcurrentSweepWeakRefTaskAndSuspendByDaemonThread(DaemonThread *dThread);
    void FinishConcurrentSweepInSTW(DaemonThread *dThread);
    void WaitConcurrentSweepWeakRefTaskFinished();

    bool IsEnableConcurrentSweep() const
    {
        return enableConcurrentSweep_;
    }

    void SetEnableConcurrentSweep(bool enableConcurrentSweep)
    {
        enableConcurrentSweep_ = enableConcurrentSweep;
    }
private:
    NO_COPY_SEMANTIC(EcmaStringTableCleaner);
    NO_MOVE_SEMANTIC(EcmaStringTableCleaner);
    static void ProcessSweepWeakRef(IteratorPtr &iter, EcmaStringTableCleaner *cleaner, const WeakRootVisitor &visitor);
    static void ProcessConcurrentSweepWeakRef(IteratorPtr &iter, EcmaStringTableCleaner *cleaner,
                                              const WeakRootVisitor &visitor);
    void StartSweepWeakRefTask();
    void WaitSweepWeakRefTask();
    void WaitConcurrentSweepWeakRefTask();
    void SignalSweepWeakRefTaskFinish();
    void SignalSweepWeakRefTaskPending();
    void SuspendAllAndFinishSweeping(JSThread *thread);
    void SuspendAllAndFinishSweepingByDaemonThread(DaemonThread *dThread);
    void ProcessCheckAndFreeHeadEntries();
    void FinishSweepingInternal();

    static inline uint32_t GetNextIndexId(IteratorPtr &iter)
    {
        return iter->fetch_add(1U, std::memory_order_relaxed);
    }

    static inline bool ReduceCountAndCheckFinish(EcmaStringTableCleaner *cleaner)
    {
        return (cleaner->PendingTaskCount_.fetch_sub(1U, std::memory_order_acq_rel) == 1U);
    }

    class SweepWeakRefTask : public common::Task {
    public:
        SweepWeakRefTask(IteratorPtr iter, EcmaStringTableCleaner *cleaner, const WeakRootVisitor &visitor)
            : common::Task(0), iter_(iter), cleaner_(cleaner), visitor_(visitor)
        {
        }
        ~SweepWeakRefTask() = default;

        bool Run(uint32_t threadIndex) override;

        NO_COPY_SEMANTIC(SweepWeakRefTask);
        NO_MOVE_SEMANTIC(SweepWeakRefTask);

    private:
        IteratorPtr iter_;
        EcmaStringTableCleaner *cleaner_;
        const WeakRootVisitor &visitor_;
    };

    class ConcurrentSweepWeakRefTask : public common::Task {
    public:
        ConcurrentSweepWeakRefTask(IteratorPtr iter, EcmaStringTableCleaner *cleaner, const WeakRootVisitor &visitor)
            : common::Task(0), iter_(iter), cleaner_(cleaner), visitor_(visitor)
        {
        }
        ~ConcurrentSweepWeakRefTask() = default;

        bool Run(uint32_t threadIndex) override;

        NO_COPY_SEMANTIC(ConcurrentSweepWeakRefTask);
        NO_MOVE_SEMANTIC(ConcurrentSweepWeakRefTask);

    private:
        IteratorPtr iter_;
        EcmaStringTableCleaner *cleaner_;
        WeakRootVisitor visitor_;
    };

    enum class SweepState {
        SWEEPING,
        PENDING,
        FINISHED
    };

    IteratorPtr iter_;
    EcmaStringTable *stringTable_;
    std::atomic<uint32_t> PendingTaskCount_ {0U};
    bool enableConcurrentSweep_ {false};
    std::array<std::vector<ChainedHashMapEntry *>, ChainedHashMapConfig::SWEEP_PARTITION_COUNT> waitFreeEntries_ {};
    Mutex sweepWeakRefMutex_;
    SweepState sweepWeakRefFinished_ { SweepState::FINISHED };
    ConditionVariable sweepWeakRefCV_;
};

class EcmaStringTableImpl final {
public:
    EcmaStringTableImpl() {}

    EcmaString *GetOrInternFlattenString(EcmaVM *vm, EcmaString *string);
    EcmaString *GetOrInternFlattenStringNoGC(EcmaVM *vm, EcmaString *string);
    EcmaString *GetOrInternStringFromCompressedSubString(EcmaVM *vm, const JSHandle<EcmaString> &string,
                                                         uint32_t offset, uint32_t utf8Len);
    EcmaString *GetOrInternString(EcmaVM *vm, EcmaString *string);

    template <typename LoaderCallback, typename EqualsCallback>
    EcmaString *GetOrInternString(EcmaVM *vm, uint32_t hashcode, LoaderCallback loaderCallback,
                                  EqualsCallback equalsCallback);
    EcmaString *GetOrInternString(EcmaVM *vm, const JSHandle<EcmaString> &firstString,
                                  const JSHandle<EcmaString> &secondString);
    EcmaString *GetOrInternString(EcmaVM *vm, const uint8_t *utf8Data, uint32_t utf8Len, bool canBeCompress,
                                  MemSpaceType type = MemSpaceType::SHARED_OLD_SPACE);
    EcmaString *GetOrInternString(EcmaVM *vm, const uint8_t *utf8Data, uint32_t utf16Len, MemSpaceType type);
    EcmaString *GetOrInternString(EcmaVM *vm, const uint16_t *utf16Data, uint32_t utf16Len, bool canBeCompress);
    // This is ONLY for JIT Thread, since JIT could not create JSHandle so need to allocate String with holding
    // lock_ --- need to support JSHandle
    EcmaString *GetOrInternStringWithoutJSHandleForJit(EcmaVM *vm, const uint8_t *utf8Data, uint32_t utf16Len,
                                                       MemSpaceType type);
    EcmaString *GetOrInternStringWithoutJSHandleForJit(EcmaVM *vm, const uint8_t *utf8Data, uint32_t utf8Len,
                                                       bool canBeCompress, MemSpaceType type);
    EcmaString *TryGetInternString(JSThread *thread, const JSHandle<EcmaString> &string);

    void SweepWeakRef(const WeakRootVisitor &visitor, uint32_t partitionID);

    void ConcurrentSweepWeakRef(const WeakRootVisitor &visitor, uint32_t partitionID,
                                std::vector<ChainedHashMapEntry*>& waitDeleteEntries);

    bool CheckStringTableValidity(JSThread *thread);

    NO_COPY_SEMANTIC(EcmaStringTableImpl);
    NO_MOVE_SEMANTIC(EcmaStringTableImpl);

    /**
     *
     * These are some "incorrect" functions, which need to fix the call chain to be removed.
     *
     */
    // This should only call in Debugger Signal, and need to fix and remove
    EcmaString *GetOrInternStringThreadUnsafe(EcmaVM *vm, const JSHandle<EcmaString> firstString,
                                              const JSHandle<EcmaString> secondString);
    // This should only call in Debugger Signal, and need to fix and remove
    EcmaString* GetOrInternStringThreadUnsafe(EcmaVM* vm, const uint8_t* utf8Data, uint32_t utf8Len,
                                              bool canBeCompress);

    ChainedHashMap *GetChainedHashMap() const
    {
        return const_cast<ChainedHashMap *>(&chainedHashMap_);
    }
private:
    ChainedHashMap chainedHashMap_;
};


class EcmaStringTable final {
public:
    EcmaStringTable()
    {
        cleaner_ = new EcmaStringTableCleaner(this);
    }

    ~EcmaStringTable()
    {
        if (cleaner_ != nullptr) {
            delete cleaner_;
            cleaner_ = nullptr;
        }
    }

    EcmaString *GetOrInternFlattenString(EcmaVM *vm, EcmaString *string);
    EcmaString *GetOrInternFlattenStringNoGC(EcmaVM *vm, EcmaString *string);
    EcmaString *GetOrInternStringFromCompressedSubString(EcmaVM *vm, const JSHandle<EcmaString> &string,
                                                         uint32_t offset, uint32_t utf8Len);
    EcmaString *GetOrInternString(EcmaVM *vm, EcmaString *string);

    template <typename LoaderCallback, typename EqualsCallback>
    EcmaString *GetOrInternString(EcmaVM *vm, uint32_t hashcode, LoaderCallback loaderCallback,
                                  EqualsCallback equalsCallback);
    EcmaString *GetOrInternString(EcmaVM *vm, const JSHandle<EcmaString> &firstString,
                                  const JSHandle<EcmaString> &secondString);
    EcmaString *GetOrInternString(EcmaVM *vm, const uint8_t *utf8Data, uint32_t utf8Len, bool canBeCompress,
                                  MemSpaceType type = MemSpaceType::SHARED_OLD_SPACE);
    EcmaString *GetOrInternString(EcmaVM *vm, const uint8_t *utf8Data, uint32_t utf16Len, MemSpaceType type);
    EcmaString *GetOrInternString(EcmaVM *vm, const uint16_t *utf16Data, uint32_t utf16Len, bool canBeCompress);
    // This is ONLY for JIT Thread, since JIT could not create JSHandle so need to allocate String with holding
    // lock_ --- need to support JSHandle
    EcmaString *GetOrInternStringWithoutJSHandleForJit(EcmaVM *vm, const uint8_t *utf8Data, uint32_t utf16Len,
                                                       MemSpaceType type);
    EcmaString *GetOrInternStringWithoutJSHandleForJit(EcmaVM *vm, const uint8_t *utf8Data, uint32_t utf8Len,
                                                       bool canBeCompress, MemSpaceType type);
    EcmaString *TryGetInternString(JSThread *thread, const JSHandle<EcmaString> &string);

    void SweepWeakRef(const WeakRootVisitor &visitor, uint32_t partitionID);
    void ConcurrentSweepWeakRef(const WeakRootVisitor &visitor, uint32_t partitionID,
                                std::vector<ChainedHashMapEntry*>& waitDeleteEntries);

    bool CheckStringTableValidity(JSThread *thread);

    EcmaStringTableCleaner *GetCleaner()
    {
        return cleaner_;
    }

    void WaitSweepWeakRefTaskFinished()
    {
        cleaner_->WaitSweepWeakRefTaskFinished();
    }

    void TransferToNativeAndWaitSweepWeakRefTaskFinished(JSThread *thread);
    void FinishConcurrentSweepInSTW(DaemonThread *dThread)
    {
        cleaner_->FinishConcurrentSweepInSTW(dThread);
    }
    void WaitConcurrentSweepWeakRefTaskFinished()
    {
        cleaner_->WaitConcurrentSweepWeakRefTaskFinished();
    }

    bool IsSweeping() const
    {
        return impl_.GetChainedHashMap()->IsSweeping();
    }

    bool IsInUse() const
    {
        return impl_.GetChainedHashMap()->GetInuseCount() > 0;
    }

    ChainedHashMap *GetChainedHashMap() const
    {
        return impl_.GetChainedHashMap();
    }

private:
    NO_COPY_SEMANTIC(EcmaStringTable);
    NO_MOVE_SEMANTIC(EcmaStringTable);

    /**
     *
     * These are some "incorrect" functions, which need to fix the call chain to be removed.
     *
     */
    // This should only call in Debugger Signal, and need to fix and remove
    EcmaString *GetOrInternStringThreadUnsafe(EcmaVM *vm, const JSHandle<EcmaString> firstString,
                                              const JSHandle<EcmaString> secondString);
    // This should only call in Debugger Signal, and need to fix and remove
    EcmaString* GetOrInternStringThreadUnsafe(EcmaVM* vm, const uint8_t* utf8Data, uint32_t utf8Len,
                                              bool canBeCompress);

    EcmaStringTableImpl impl_;

    EcmaStringTableCleaner *cleaner_ = nullptr;
    friend class SnapshotProcessor;
    friend class BaseDeserializer;
};

class SingleCharTable : public TaggedArray {
public:
    static SingleCharTable *Cast(TaggedObject *object)
    {
        return reinterpret_cast<SingleCharTable*>(object);
    }
    static JSTaggedValue CreateSingleCharTable(JSThread *thread);
    JSTaggedValue GetStringFromSingleCharTable(JSThread *thread, int32_t ch)
    {
        return Get(thread, ch);
    }
private:
    SingleCharTable() = default;
    ~SingleCharTable() = default;
    NO_COPY_SEMANTIC(SingleCharTable);
    NO_MOVE_SEMANTIC(SingleCharTable);
    static constexpr uint32_t MAX_ONEBYTE_CHARCODE = 128; // 0X00-0X7F
};
}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_STRING_TABLE_H
