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

#ifndef ECMASCRIPT_RUNTIME_H
#define ECMASCRIPT_RUNTIME_H

#include "ecmascript/ecma_string_table.h"
#include "ecmascript/global_env_constants.h"
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/platform/mutex.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/module/js_shared_module_manager.h"
#include "ecmascript/mutator_lock.h"

#include "libpandabase/macros.h"

#include <list>
#include <memory>

namespace panda::ecmascript {
class Runtime {
public:
    PUBLIC_API static Runtime *GetInstance();

    static void CreateIfFirstVm(const JSRuntimeOptions &options);
    static void DestroyIfLastVm();
    void InitializeIfFirstVm(EcmaVM *vm);

    void RegisterThread(JSThread* newThread);
    void UnregisterThread(JSThread* thread);

    void SuspendAll(JSThread *current);
    void ResumeAll(JSThread *current);
    void IterateSerializeRoot(const RootVisitor &v);

    JSThread *GetMainThread() const
    {
        return mainThread_;
    }

    MutatorLock *GetMutatorLock()
    {
        return &mutatorLock_;
    }

    const MutatorLock *GetMutatorLock() const
    {
        return &mutatorLock_;
    }

    template<class Callback>
    void GCIterateThreadList(const Callback &cb)
    {
        LockHolder lock(threadsLock_);
        for (auto thread : threads_) {
            if (thread->ReadyForGCIterating()) {
                cb(thread);
            }
        }
    }

    inline const GlobalEnvConstants *GetGlobalEnvConstants()
    {
        return globalConstants_;
    }

    JSTaggedValue GetGlobalEnv() const
    {
        return globalEnv_;
    }

    inline EcmaStringTable *GetEcmaStringTable() const
    {
        return stringTable_.get();
    }

    uint32_t PushSerializationRoot([[maybe_unused]] JSThread *thread, std::vector<TaggedObject *> &rootSet)
    {
        ASSERT(thread->IsInManagedState());
        LockHolder lock(serializeLock_);
        uint32_t index = GetSerializeDataIndex();
        ASSERT(serializeRootMap_.find(index) == serializeRootMap_.end());
        serializeRootMap_.emplace(index, rootSet);
        return index;
    }

    void RemoveSerializationRoot([[maybe_unused]] JSThread *thread, uint32_t index)
    {
        ASSERT(thread->IsInManagedState());
        LockHolder lock(serializeLock_);
        ASSERT(serializeRootMap_.find(index) != serializeRootMap_.end());
        serializeRootMap_.erase(index);
        serializeDataIndexVector_.emplace_back(index);
    }

    static bool SharedGCRequest()
    {
        LockHolder lock(*vmCreationLock_);
        destroyCount_++;
        if (destroyCount_ == WORKER_DESTRUCTION_COUNT || vmCount_ < MIN_GC_TRIGGER_VM_COUNT) {
            destroyCount_ = 0;
            return true;
        } else {
            return false;
        }
    }

    bool HasCachedConstpool(const JSPandaFile *jsPandaFile);
    PUBLIC_API JSTaggedValue FindConstpool(const JSPandaFile *jsPandaFile, int32_t index);
    JSTaggedValue FindConstpoolUnlocked(const JSPandaFile *jsPandaFile, int32_t index);
    JSHandle<ConstantPool> AddOrUpdateConstpool(const JSPandaFile *jsPandaFile,
                                                JSHandle<ConstantPool> constpool,
                                                int32_t index = 0);
    std::optional<std::reference_wrapper<CMap<int32_t, JSTaggedValue>>> FindConstpools(
        const JSPandaFile *jsPandaFile);
    void EraseUnusedConstpool(const JSPandaFile *jsPandaFile, int32_t index, int32_t constpoolIndex);

    void ProcessNativeDeleteInSharedGC(const WeakRootVisitor &visitor);

private:
    static constexpr int32_t WORKER_DESTRUCTION_COUNT = 3;
    static constexpr int32_t MIN_GC_TRIGGER_VM_COUNT = 4;
    Runtime() = default;
    ~Runtime();
    void SuspendAllThreadsImpl(JSThread *current);
    void ResumeAllThreadsImpl(JSThread *current);

    void PreInitialization(const EcmaVM *vm);
    void PostInitialization(const EcmaVM *vm);

    uint32_t GetSerializeDataIndex()
    {
        if (!serializeDataIndexVector_.empty()) {
            uint32_t index = serializeDataIndexVector_.back();
            serializeDataIndexVector_.pop_back();
            return index;
        }
        return ++serializeDataIndex_;
    }

    int32_t GetAndIncreaseSharedConstpoolCount()
    {
        if (freeSharedConstpoolIndex_.size() > 0) {
            auto iter = freeSharedConstpoolIndex_.begin();
            int32_t freeCount = *iter;
            freeSharedConstpoolIndex_.erase(iter);
            return freeCount;
        }
        return sharedConstpoolCount_++;
    }

    Mutex threadsLock_;
    ConditionVariable threadSuspendCondVar_;
    Mutex serializeLock_;
    std::list<JSThread*> threads_;
    uint32_t suspendNewCount_ {0};
    uint32_t serializeDataIndex_ {0};
    MutatorLock mutatorLock_;

    const GlobalEnvConstants *globalConstants_ {nullptr};
    JSTaggedValue globalEnv_ {JSTaggedValue::Hole()};
    JSThread *mainThread_ {nullptr};
    // for shared heap.
    std::unique_ptr<NativeAreaAllocator> nativeAreaAllocator_;
    std::unique_ptr<HeapRegionAllocator> heapRegionAllocator_;
    // for stringTable.
    std::unique_ptr<EcmaStringTable> stringTable_;
    std::unordered_map<uint32_t, std::vector<TaggedObject *>> serializeRootMap_;
    std::vector<uint32_t> serializeDataIndexVector_;

    // Shared constantpool cache
    Mutex constpoolLock_;
    CMap<const JSPandaFile *, CMap<int32_t, JSTaggedValue>> globalSharedConstpools_ {};
    int32_t sharedConstpoolCount_ = 0; // shared constpool count.
    std::set<int32_t> freeSharedConstpoolIndex_ {}; // reuse shared constpool index.

    // Runtime instance and VMs creation.
    static int32_t vmCount_;
    static int32_t destroyCount_;
    static bool firstVmCreated_;
    static Mutex *vmCreationLock_;
    static Runtime *instance_;

    friend class EcmaVM;
    friend class JSThread;

    NO_COPY_SEMANTIC(Runtime);
    NO_MOVE_SEMANTIC(Runtime);
};
}  // namespace panda::ecmascript
#endif // ECMASCRIPT_RUNTIME_H
