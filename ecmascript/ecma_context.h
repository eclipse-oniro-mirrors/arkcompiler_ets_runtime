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
#ifndef ECMASCRIPT_ECMA_CONTEXT_H
#define ECMASCRIPT_ECMA_CONTEXT_H

#include <cstdint>
#include <optional>
#include "ecmascript/base/config.h"
#include "ecmascript/common.h"
#include "ecmascript/dfx/vmstat/opt_code_profiler.h"
#include "ecmascript/frames.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/mem/visitor.h"
#include "ecmascript/patch/patch_loader.h"
#include "ecmascript/regexp/regexp_parser_cache.h"
#include "ecmascript/waiter_list.h"
#include "global_handle_collection.h"
#include "libpandafile/file.h"

namespace panda {
class JSNApi;
namespace panda_file {
class File;
}  // namespace panda_file

namespace ecmascript {
class GlobalEnv;
class ObjectFactory;
class EcmaRuntimeStat;
class RegExpParserCache;
class JSPandaFileManager;
class JSPandaFile;
class ConstantPool;
class JSPromise;
class RegExpExecResultCache;
class EcmaHandleScope;
class GlobalIndexMap;
enum class PromiseRejectionEvent : uint8_t;

template<typename T>
class JSHandle;
class JSThread;
class JSFunction;
class JSPromise;
class JSTaggedValue;
class EcmaVM;
class ModuleManager;
class TSManager;
class AOTFileManager;
class QuickFixManager;
class OptCodeProfiler;
struct CJSInfo;

namespace job {
class MicroJobQueue;
}  // namespace job
namespace tooling {
class JsDebuggerManager;
}  // namespace tooling
namespace kungfu {
class PGOTypeManager;
} // namespace kungfu

enum class IcuFormatterType {
    SIMPLE_DATE_FORMAT_DEFAULT,
    SIMPLE_DATE_FORMAT_DATE,
    SIMPLE_DATE_FORMAT_TIME,
    NUMBER_FORMATTER,
    COLLATOR,
    ICU_FORMATTER_TYPE_COUNT
};

using HostPromiseRejectionTracker = void (*)(const EcmaVM* vm,
                                             const JSHandle<JSPromise> promise,
                                             const JSHandle<JSTaggedValue> reason,
                                             PromiseRejectionEvent operation,
                                             void* data);
using PromiseRejectCallback = void (*)(void* info);
using IcuDeleteEntry = void(*)(void *pointer, void *data);
class EcmaContext {
public:
    static EcmaContext *CreateAndInitialize(JSThread *thread);
    static void CheckAndDestroy(JSThread *thread, EcmaContext *context);

    static EcmaContext *Create(JSThread *thread);
    static bool Destroy(EcmaContext *context);

    EcmaContext(JSThread *thread);
    ~EcmaContext();

    EcmaVM *GetEcmaVM() const
    {
        return vm_;
    }

    bool Initialize();

    bool IsExecutingPendingJob() const
    {
        return isProcessingPendingJob_.load();
    }

    bool HasPendingJob();

    bool ExecutePromisePendingJob();

    static EcmaContext *ConstCast(const EcmaContext *context)
    {
        return const_cast<EcmaContext *>(context);
    }

    bool IsInitialized() const
    {
        return initialized_;
    }

    ModuleManager *GetModuleManager() const
    {
        return moduleManager_;
    }

    TSManager *GetTSManager() const
    {
        return tsManager_;
    }

    kungfu::PGOTypeManager *GetPTManager() const
    {
        return ptManager_;
    }

    void PUBLIC_API SetTSManager(TSManager *set);

    ARK_INLINE JSThread *GetJSThread() const
    {
        return thread_;
    }
    PromiseRejectCallback GetPromiseRejectCallback() const
    {
        return promiseRejectCallback_;
    }

    void SetPromiseRejectCallback(PromiseRejectCallback cb)
    {
        promiseRejectCallback_ = cb;
    }

    void SetData(void* data)
    {
        data_ = data;
    }

    void PromiseRejectionTracker(const JSHandle<JSPromise> &promise,
                                 const JSHandle<JSTaggedValue> &reason, PromiseRejectionEvent operation)
    {
        if (hostPromiseRejectionTracker_ != nullptr) {
            hostPromiseRejectionTracker_(vm_, promise, reason, operation, data_);
        }
    }

    void SetHostPromiseRejectionTracker(HostPromiseRejectionTracker cb)
    {
        hostPromiseRejectionTracker_ = cb;
    }
    void SetupRegExpResultCache();
    void SetupNumberToStringResultCache();
    void SetupStringSplitResultCache();
    void SetupStringToListResultCache();
    JSHandle<JSTaggedValue> GetRegExpCache() const
    {
        return JSHandle<JSTaggedValue>(reinterpret_cast<uintptr_t>(&regexpCache_));
    }

    RegExpParserCache *GetRegExpParserCache() const
    {
        ASSERT(regExpParserCache_ != nullptr);
        return regExpParserCache_;
    }

    void SetRegExpCache(JSTaggedValue newCache)
    {
        regexpCache_ = newCache;
    }
    uintptr_t GetExpCacheAddress()
    {
        return reinterpret_cast<uintptr_t>(&regexpCache_);
    }

    void SetupRegExpGlobalResult();

    JSHandle<JSTaggedValue> GetRegExpGlobalResult() const
    {
        return JSHandle<JSTaggedValue>(reinterpret_cast<uintptr_t>(&regexpGlobal_));
    }

    void SetRegExpGlobalResult(JSTaggedValue newResult)
    {
        regexpGlobal_ = newResult;
    }

    WaiterListNode *GetWaiterListNode()
    {
        return &waiterListNode_;
    }

    void SetAllowAtomicWait(bool wait)
    {
        AllowAtomicWait_ = wait;
    }

    bool GetAllowAtomicWait() const
    {
        return AllowAtomicWait_;
    }
    JSHandle<JSTaggedValue> GetNumberToStringResultCache() const
    {
        return JSHandle<JSTaggedValue>(reinterpret_cast<uintptr_t>(&numberToStringResultCache_));
    }

    void SetNumberToStringResultCache(JSTaggedValue newCache)
    {
        numberToStringResultCache_ = newCache;
    }

    JSHandle<JSTaggedValue> GetStringSplitResultCache() const
    {
        return JSHandle<JSTaggedValue>(reinterpret_cast<uintptr_t>(&stringSplitResultCache_));
    }

    JSHandle<JSTaggedValue> GetStringToListResultCache() const
    {
        return JSHandle<JSTaggedValue>(reinterpret_cast<uintptr_t>(&stringToListResultCache_));
    }

    void SetStringSplitResultCache(JSTaggedValue newCache)
    {
        stringSplitResultCache_ = newCache;
    }
    JSHandle<ecmascript::JSTaggedValue> GetAndClearEcmaUncaughtException() const;
    JSHandle<ecmascript::JSTaggedValue> GetEcmaUncaughtException() const;
    void EnableUserUncaughtErrorHandler();

    void AddConstpool(const JSPandaFile *jsPandaFile, JSTaggedValue constpool, int32_t index = 0);

    void UpdateConstpool(const std::string& fileName, JSTaggedValue constpool, int32_t index = 0);

    bool HasCachedConstpool(const JSPandaFile *jsPandaFile) const;

    JSTaggedValue PUBLIC_API FindConstpool(const JSPandaFile *jsPandaFile, int32_t index);
    // For new version instruction.
    JSTaggedValue PUBLIC_API FindConstpool(const JSPandaFile *jsPandaFile, panda_file::File::EntityId id);
    JSTaggedValue PUBLIC_API FindUnsharedConstpool(JSTaggedValue sharedConstpool);
    JSHandle<ConstantPool> CreateConstpoolPair(JSPandaFile *jsPandaFile, EntityId methodId);
    JSTaggedValue FindConstpoolWithAOT(const JSPandaFile *jsPandaFile, int32_t index);
    void EraseUnsharedConstpool(JSTaggedValue sharedConstpool);
    std::optional<std::reference_wrapper<CMap<int32_t, JSTaggedValue>>> FindConstpools(
        const JSPandaFile *jsPandaFile);

    JSHandle<ConstantPool> PUBLIC_API FindOrCreateConstPool(const JSPandaFile *jsPandaFile,
                                                            panda_file::File::EntityId id);
    void CreateAllConstpool(const JSPandaFile *jsPandaFile);

    void HandleUncaughtException(JSTaggedValue exception);
    void HandleUncaughtException();
    void ProcessNativeDeleteInSharedGC(const WeakRootVisitor &visitor);
    void ProcessReferences(const WeakRootVisitor &visitor);
    JSHandle<GlobalEnv> GetGlobalEnv() const;
    bool GlobalEnvIsHole()
    {
        return globalEnv_.IsHole();
    }

    JSHandle<job::MicroJobQueue> GetMicroJobQueue() const;

    static void PrintJSErrorInfo(JSThread *thread, const JSHandle<JSTaggedValue> &exceptionInfo);
    void Iterate(const RootVisitor &v, const RootRangeVisitor &rv);
    static void MountContext(JSThread *thread);
    static void UnmountContext(JSThread *thread);
    void SetMicroJobQueue(job::MicroJobQueue *queue);
    void SetGlobalEnv(GlobalEnv *global);
    void PrintOptStat();

    OptCodeProfiler *GetOptCodeProfiler() const
    {
        return optCodeProfiler_;
    }

    TypedOpProfiler *GetTypdOpProfiler() const
    {
        return typedOpProfiler_;
    }

    // For icu objects cache
    void SetIcuFormatterToCache(IcuFormatterType type, const std::string &locale, void *icuObj,
                                IcuDeleteEntry deleteEntry = nullptr)
    {
        EcmaContext::IcuFormatter icuFormatter = IcuFormatter(locale, icuObj, deleteEntry);
        icuObjCache_[static_cast<int>(type)] = icuFormatter;
    }

    ARK_INLINE void *GetIcuFormatterFromCache(IcuFormatterType type, std::string &locale)
    {
        auto &icuFormatter = icuObjCache_[static_cast<int>(type)];
        if (icuFormatter.locale == locale) {
            return icuFormatter.icuObj;
        }
        return nullptr;
    }

    void ClearIcuCache()
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(IcuFormatterType::ICU_FORMATTER_TYPE_COUNT); i++) {
            auto &icuFormatter = icuObjCache_[i];
            IcuDeleteEntry deleteEntry = icuFormatter.deleteEntry;
            if (deleteEntry != nullptr) {
                deleteEntry(icuFormatter.icuObj, vm_);
            }
            icuFormatter = EcmaContext::IcuFormatter{};
        }
    }

    EcmaRuntimeStat *GetRuntimeStat() const
    {
        return runtimeStat_;
    }

    void SetRuntimeStatEnable(bool flag);
    void InitializeEcmaScriptRunStat();
    void DumpAOTInfo() const DUMP_API_ATTR;

    JSTaggedValue ExecuteAot(size_t actualNumArgs, JSTaggedType *args, const JSTaggedType *prevFp,
                             bool needPushUndefined);
    void LoadStubFile();

    JSTaggedType *GetHandleScopeStorageNext() const
    {
        return handleScopeStorageNext_;
    }

    void SetHandleScopeStorageNext(JSTaggedType *value)
    {
        handleScopeStorageNext_ = value;
    }

    JSTaggedType *GetHandleScopeStorageEnd() const
    {
        return handleScopeStorageEnd_;
    }

    void SetHandleScopeStorageEnd(JSTaggedType *value)
    {
        handleScopeStorageEnd_ = value;
    }

    int GetCurrentHandleStorageIndex() const
    {
        return currentHandleStorageIndex_;
    }

    void HandleScopeCountAdd()
    {
        handleScopeCount_++;
    }

    void HandleScopeCountDec()
    {
        handleScopeCount_--;
    }

    void SetLastHandleScope(EcmaHandleScope *scope)
    {
        lastHandleScope_ = scope;
    }

    EcmaHandleScope *GetLastHandleScope() const
    {
        return lastHandleScope_;
    }

    size_t IterateHandle(const RootRangeVisitor &rangeVisitor);
    uintptr_t *ExpandHandleStorage();
    void ShrinkHandleStorage(int prevIndex);

    JSTaggedType *GetCurrentFrame() const
    {
        return currentFrame_;
    }

    JSTaggedType *GetLeaveFrame() const
    {
        return leaveFrame_;
    }

    JSTaggedType *GetLastFp() const
    {
        return lastFp_;
    }

    void SetFramePointers(JSTaggedType *currentFrame, JSTaggedType *leaveFrame, JSTaggedType *lastFp)
    {
        currentFrame_ = currentFrame;
        leaveFrame_ = leaveFrame;
        lastFp_ = lastFp;
    }
    void SetFrameBase(JSTaggedType *frameBase)
    {
        frameBase_ = frameBase;
    }
    JSTaggedType *GetFrameBase() const
    {
        return frameBase_;
    }

    void SetStackStart(uint64_t stackStart)
    {
        stackStart_ = stackStart;
    }
    uint64_t GetStackStart() const
    {
        return stackStart_;
    }
    void SetStackLimit(uint64_t stackLimit)
    {
        stackLimit_ = stackLimit;
    }
    uint64_t GetStackLimit() const
    {
        return stackLimit_;
    }

    PropertiesCache *GetPropertiesCache() const
    {
        return propertiesCache_;
    }
    void ClearBufferData();
    const GlobalEnvConstants *GlobalConstants() const
    {
        return &globalConst_;
    }

    void AddPatchModule(const CString &recordName, const JSHandle<JSTaggedValue> moduleRecord)
    {
        cachedPatchModules_.emplace(recordName, moduleRecord);
    }
    JSHandle<JSTaggedValue> FindPatchModule(const CString &recordName) const
    {
        auto iter = cachedPatchModules_.find(recordName);
        if (iter != cachedPatchModules_.end()) {
            return iter->second;
        }
        return JSHandle<JSTaggedValue>(thread_, JSTaggedValue::Hole());
    }
    void ClearPatchModules()
    {
        GlobalHandleCollection gloalHandleCollection(thread_);
        for (auto &item : cachedPatchModules_) {
            gloalHandleCollection.Dispose(item.second);
        }
        cachedPatchModules_.clear();
    }

    StageOfHotReload GetStageOfHotReload() const
    {
        return stageOfHotReload_;
    }
    void SetStageOfHotReload(StageOfHotReload stageOfHotReload)
    {
        stageOfHotReload_ = stageOfHotReload;
    }

    bool JoinStackPushFastPath(JSHandle<JSTaggedValue> receiver);
    bool JoinStackPush(JSHandle<JSTaggedValue> receiver);
    void JoinStackPopFastPath(JSHandle<JSTaggedValue> receiver);
    void JoinStackPop(JSHandle<JSTaggedValue> receiver);

    void SetJsonStringifyCache(size_t index, CVector<std::pair<CString, int>> &value)
    {
        stringifyCache_[index] = value;
    }

    CVector<std::pair<CString, int>> GetJsonStringifyCache(size_t index)
    {
        return stringifyCache_[index];
    }

    bool IsAotEntry()
    {
        return isAotEntry_;
    }

    void InsertFreeUnsharedConstpoolCount(JSTaggedValue sharedConstpool);
    void SetUnsharedConstpool(int32_t unsharedConstpoolIndex, JSTaggedValue constpool)
    {
        ASSERT(0 <= unsharedConstpoolIndex && unsharedConstpoolIndex < UNSHARED_CONSTANTPOOL_COUNT);
        unsharedConstpools_->data()[unsharedConstpoolIndex] = constpool;
    }

    int32_t GetAndIncreaseUnsharedConstpoolCount()
    {
        std::lock_guard<std::mutex> guard(unsharedConstpoolCountMutex_);
        if (freeUnsharedConstpoolCount_.size() > 0) {
            auto iter = freeUnsharedConstpoolCount_.begin();
            int32_t freeCount = *iter;
            freeUnsharedConstpoolCount_.erase(iter);
            return freeCount;
        }
        return unsharedConstpoolCount_++;
    }

    void CheckUnsharedConstpoolArrayLimit(int32_t count)
    {
        if (count >= UNSHARED_CONSTANTPOOL_COUNT) {
            LOG_ECMA(FATAL) << "the unshared constpool array need to expanding capacity, count :" << count;
            UNREACHABLE();
        }
    }

    std::tuple<uint64_t, uint8_t *, int, kungfu::CalleeRegAndOffsetVec> CalCallSiteInfo(uintptr_t retAddr) const;
private:
    void CJSExecution(JSHandle<JSFunction> &func, JSHandle<JSTaggedValue> &thisArg,
                      const JSPandaFile *jsPandaFile, std::string_view entryPoint);
    JSTaggedValue InvokeEcmaAotEntrypoint(JSHandle<JSFunction> mainFunc, JSHandle<JSTaggedValue> &thisArg,
                                          const JSPandaFile *jsPandaFile, std::string_view entryPoint,
                                          CJSInfo *cjsInfo = nullptr);
    Expected<JSTaggedValue, bool> InvokeEcmaEntrypoint(const JSPandaFile *jsPandaFile, std::string_view entryPoint,
                                                       bool executeFromJob = false);
    Expected<JSTaggedValue, bool> InvokeEcmaEntrypointForHotReload(
        const JSPandaFile *jsPandaFile, std::string_view entryPoint, bool executeFromJob);
    Expected<JSTaggedValue, bool> CommonInvokeEcmaEntrypoint(const JSPandaFile *jsPandaFile,
        std::string_view entryPoint, JSHandle<JSFunction> &func, bool executeFromJob);
    bool LoadAOTFiles(const std::string &aotFileName);
    void RelocateConstantString(const JSPandaFile *jsPandaFile);
    NO_MOVE_SEMANTIC(EcmaContext);
    NO_COPY_SEMANTIC(EcmaContext);

    PropertiesCache *propertiesCache_ {nullptr};
    JSThread *thread_ {nullptr};
    EcmaVM *vm_ {nullptr};

    bool isUncaughtExceptionRegistered_ {false};
    bool initialized_ {false};
    std::atomic<bool> isProcessingPendingJob_ {false};
    ObjectFactory *factory_ {nullptr};

    // VM execution states.
    RegExpParserCache *regExpParserCache_ {nullptr};
    JSTaggedValue numberToStringResultCache_ {JSTaggedValue::Hole()};
    JSTaggedValue stringSplitResultCache_ {JSTaggedValue::Hole()};
    JSTaggedValue stringToListResultCache_ {JSTaggedValue::Hole()};
    JSTaggedValue globalEnv_ {JSTaggedValue::Hole()};
    JSTaggedValue pointerToIndexDictionary_ {JSTaggedValue::Hole()};
    JSTaggedValue regexpCache_ {JSTaggedValue::Hole()};
    JSTaggedValue regexpGlobal_ {JSTaggedValue::Hole()};
    JSTaggedValue microJobQueue_ {JSTaggedValue::Hole()};
    EcmaRuntimeStat *runtimeStat_ {nullptr};

    CMap<const JSPandaFile *, CMap<int32_t, JSTaggedValue>> cachedSharedConstpools_ {};
    // todo(lijiamin) Consider expanding capacity.
    static constexpr int32_t UNSHARED_CONSTANTPOOL_COUNT = 10240;
    std::array<JSTaggedValue, UNSHARED_CONSTANTPOOL_COUNT> *unsharedConstpools_ = nullptr;
    static int32_t unsharedConstpoolCount_; // unshared constpool index.
    static constexpr int32_t SHARED_CONSTPOOL_KEY_NOT_FOUND = INT32_MAX; // INT32_MAX :invalid value.
    static CUnorderedSet<int32_t> freeUnsharedConstpoolCount_; // reuse unshared Constpool Count.
    static std::mutex unsharedConstpoolCountMutex_;

    // for HotReload of module.
    CMap<CString, JSHandle<JSTaggedValue>> cachedPatchModules_ {};
    StageOfHotReload stageOfHotReload_ = StageOfHotReload::INITIALIZE_STAGE_OF_HOTRELOAD;

    // VM resources.
    ModuleManager *moduleManager_ {nullptr};
    TSManager *tsManager_ {nullptr};
    kungfu::PGOTypeManager *ptManager_ {nullptr};
    AOTFileManager *aotFileManager_ {nullptr};

    // atomics
    bool AllowAtomicWait_ {true};
    WaiterListNode waiterListNode_;

    // Registered Callbacks
    PromiseRejectCallback promiseRejectCallback_ {nullptr};
    HostPromiseRejectionTracker hostPromiseRejectionTracker_ {nullptr};
    void* data_{nullptr};

    // opt code Profiler
    OptCodeProfiler *optCodeProfiler_ {nullptr};

    // opt code loop hoist
    TypedOpProfiler *typedOpProfiler_ {nullptr};

    // For icu objects cache
    struct IcuFormatter {
        std::string locale;
        void *icuObj {nullptr};
        IcuDeleteEntry deleteEntry {nullptr};

        IcuFormatter() = default;
        IcuFormatter(const std::string &locale, void *icuObj, IcuDeleteEntry deleteEntry = nullptr)
            : locale(locale), icuObj(icuObj), deleteEntry(deleteEntry) {}
    };
    IcuFormatter icuObjCache_[static_cast<uint32_t>(IcuFormatterType::ICU_FORMATTER_TYPE_COUNT)];
    // Handlescope
    static const uint32_t NODE_BLOCK_SIZE_LOG2 = 10;
    static const uint32_t NODE_BLOCK_SIZE = 1U << NODE_BLOCK_SIZE_LOG2;
    static constexpr int32_t MIN_HANDLE_STORAGE_SIZE = 2;
    JSTaggedType *handleScopeStorageNext_ {nullptr};
    JSTaggedType *handleScopeStorageEnd_ {nullptr};
    std::vector<std::array<JSTaggedType, NODE_BLOCK_SIZE> *> handleStorageNodes_ {};
    int32_t currentHandleStorageIndex_ {-1};
    int32_t handleScopeCount_ {0};
    EcmaHandleScope *lastHandleScope_ {nullptr};
    // Frame pointer
    JSTaggedType *currentFrame_ {nullptr};
    JSTaggedType *leaveFrame_ {nullptr};
    JSTaggedType *lastFp_ {nullptr};
    JSTaggedType *frameBase_ {nullptr};
    uint64_t stackStart_ {0};
    uint64_t stackLimit_ {0};
    GlobalEnvConstants globalConst_;
    // Join Stack
    static constexpr uint32_t MIN_JOIN_STACK_SIZE = 2;
    CVector<JSTaggedValue> joinStack_ {JSTaggedValue::Hole(), JSTaggedValue::Hole()};
    // json stringify cache
    static constexpr uint32_t STRINGIFY_CACHE_SIZE = 64;
    std::array<CVector<std::pair<CString, int>>, STRINGIFY_CACHE_SIZE> stringifyCache_ {};
    bool isAotEntry_ { false };

    friend class EcmaHandleScope;
    friend class JSPandaFileExecutor;
    friend class ObjectFactory;
    friend class panda::JSNApi;
    friend class AOTFileManager;
    friend class GlobalIndexMap;
};
}  // namespace ecmascript
}  // namespace panda
#endif // ECMASCRIPT_ECMA_CONTEXT_H
