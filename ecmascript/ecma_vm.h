/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_ECMA_VM_H
#define ECMASCRIPT_ECMA_VM_H

#include <tuple>

#include "ecmascript/base/config.h"
#include "ecmascript/ecma_string_table.h"
#include "ecmascript/global_handle_collection.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_method.h"
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/mem/c_string.h"
#include "ecmascript/mem/chunk_containers.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/object_xray.h"
#include "ecmascript/mem/space.h"
#include "ecmascript/platform/task.h"
#include "ecmascript/snapshot/mem/snapshot_serialize.h"
#include "ecmascript/tooling/interface/js_debugger_manager.h"
#include "ecmascript/tooling/backend/js_pt_extractor.h"
#include "include/panda_vm.h"
#include "libpandabase/macros.h"

namespace panda {
namespace panda_file {
class File;
}  // namespace panda_file

namespace ecmascript {
class GlobalEnv;
class ObjectFactory;
class RegExpParserCache;
class EcmaRuntimeStat;
class Heap;
class HeapTracker;
class JSNativePointer;
class Program;
class RegExpExecResultCache;
class JSPromise;
enum class PromiseRejectionEvent : uint8_t;
class JSPandaFileManager;
class JSPandaFile;
namespace job {
class MicroJobQueue;
}  // namespace job

namespace tooling {
class JsDebuggerManager;
}  // namespace tooling

template<typename T>
class JSHandle;
class JSArrayBuffer;
class JSFunction;
class Program;
class ModuleManager;
class EcmaModule;
class TSLoader;
struct BytecodeTranslationInfo;
using HostPromiseRejectionTracker = void (*)(const EcmaVM* vm,
                                             const JSHandle<JSPromise> promise,
                                             const JSHandle<JSTaggedValue> reason,
                                             PromiseRejectionEvent operation,
                                             void* data);
using PromiseRejectCallback = void (*)(void* info);

class EcmaVM : public PandaVM {
public:
    static EcmaVM *Cast(PandaVM *object)
    {
        return reinterpret_cast<EcmaVM *>(object);
    }

    static EcmaVM *Create(const JSRuntimeOptions &options);

    static bool Destroy(PandaVM *vm);

    explicit EcmaVM(JSRuntimeOptions options);

    static Expected<EcmaVM *, CString> Create([[maybe_unused]] Runtime *runtime);

    EcmaVM();

    ~EcmaVM() override;

    bool ExecuteFromPf(const std::string &filename, std::string_view entryPoint,
                       const std::vector<std::string> &args, bool isModule = false);

    bool ExecuteFromBuffer(const void *buffer, size_t size, std::string_view entryPoint,
                           const std::vector<std::string> &args, const std::string &filename = "");

    bool PUBLIC_API CollectInfoOfPandaFile(const std::string &filename, std::string_view entryPoint,
                                           std::vector<BytecodeTranslationInfo> *infoList,
                                           const panda_file::File *&pf);

    bool IsInitialized() const
    {
        return vmInitialized_;
    }

    ObjectFactory *GetFactory() const
    {
        return factory_;
    }

    bool Initialize() override;

    bool InitializeFinish() override;
    void UninitializeThreads() override {}
    void PreStartup() override {}
    void PreZygoteFork() override {}
    void PostZygoteFork() override {}
    void InitializeGC() override {}
    void StartGC() override {}
    void StopGC() override {}

    void VisitVmRoots([[maybe_unused]] const GCRootVisitor &visitor) override {}
    void UpdateVmRefs() override {}

    PandaVMType GetPandaVMType() const override
    {
        return PandaVMType::ECMA_VM;
    }

    LanguageContext GetLanguageContext() const override
    {
        return Runtime::GetCurrent()->GetLanguageContext(panda_file::SourceLang::ECMASCRIPT);
    }

    panda::mem::HeapManager *GetHeapManager() const override
    {
        return nullptr;
    }

    panda::mem::GC *GetGC() const override
    {
        return nullptr;
    }

    panda::mem::GCTrigger *GetGCTrigger() const override
    {
        return nullptr;
    }

    StringTable *GetStringTable() const override
    {
        return nullptr;
    }

    panda::mem::GCStats *GetGCStats() const override
    {
        return nullptr;
    }

    panda::mem::MemStatsType *GetMemStats() const override
    {
        return nullptr;
    }

    GCStats *GetEcmaGCStats() const
    {
        return gcStats_;
    }

    panda::mem::GlobalObjectStorage *GetGlobalObjectStorage() const override
    {
        return nullptr;
    }

    MonitorPool *GetMonitorPool() const override
    {
        return nullptr;
    }

    ThreadManager *GetThreadManager() const override
    {
        return nullptr;
    }

    ManagedThread *GetAssociatedThread() const override
    {
        return thread_;
    }

    JSThread *GetAssociatedJSThread() const
    {
        return thread_;
    }

    CompilerInterface *GetCompiler() const override
    {
        return nullptr;
    }

    Rendezvous *GetRendezvous() const override
    {
        return nullptr;
    }

    ObjectHeader *GetOOMErrorObject() override
    {
        // preallocated OOM is not implemented for JS
        UNREACHABLE();
    }

    panda::mem::ReferenceProcessor *GetReferenceProcessor() const override
    {
        return nullptr;
    }

    const RuntimeOptions &GetOptions() const override
    {
        return Runtime::GetOptions();
    }

    static const JSRuntimeOptions &GetJSOptions()
    {
        return options_;
    }

    JSHandle<GlobalEnv> GetGlobalEnv() const;

    JSHandle<job::MicroJobQueue> GetMicroJobQueue() const;

    bool ExecutePromisePendingJob() const;

    RegExpParserCache *GetRegExpParserCache() const
    {
        ASSERT(regExpParserCache_ != nullptr);
        return regExpParserCache_;
    }

    JSMethod *GetMethodForNativeFunction(const void *func);

    EcmaStringTable *GetEcmaStringTable() const
    {
        ASSERT(stringTable_ != nullptr);
        return stringTable_;
    }

    JSThread *GetJSThread() const
    {
#if defined(ECMASCRIPT_ENABLE_THREAD_CHECK) && ECMASCRIPT_ENABLE_THREAD_CHECK
        // Exclude GC thread
        if (options_.IsEnableThreadCheck()) {
            if (!Platform::GetCurrentPlatform()->IsInThreadPool(std::this_thread::get_id()) &&
                thread_->GetThreadId() != JSThread::GetCurrentThreadId()) {
                    LOG(FATAL, RUNTIME) << "Fatal: ecma_vm cannot run in multi-thread!";
            }
        }
#endif
        return thread_;
    }

    JSThread *GetJSThreadNoCheck() const
    {
        return thread_;
    }

    bool ICEnable() const
    {
        return icEnable_;
    }

    void PushToArrayDataList(JSNativePointer *array);
    void RemoveArrayDataList(JSNativePointer *array);

    JSHandle<ecmascript::JSTaggedValue> GetAndClearEcmaUncaughtException() const;
    JSHandle<ecmascript::JSTaggedValue> GetEcmaUncaughtException() const;
    void EnableUserUncaughtErrorHandler();

    EcmaRuntimeStat *GetRuntimeStat() const
    {
        return runtimeStat_;
    }

    void SetRuntimeStatEnable(bool flag);

    bool IsRuntimeStatEnabled() const
    {
        return runtimeStatEnabled_;
    }

    bool IsOptionalLogEnabled() const
    {
        return optionalLogEnabled_;
    }

    void Iterate(const RootVisitor &v);

    const Heap *GetHeap() const
    {
        return heap_;
    }

    void CollectGarbage(TriggerGCType gcType) const;

    void StartHeapTracking(HeapTracker *tracker);

    void StopHeapTracking();

    NativeAreaAllocator *GetNativeAreaAllocator() const
    {
        return nativeAreaAllocator_.get();
    }

    HeapRegionAllocator *GetHeapRegionAllocator() const
    {
        return heapRegionAllocator_.get();
    }

    Chunk *GetChunk() const
    {
        return const_cast<Chunk *>(&chunk_);
    }
    void ProcessNativeDelete(const WeakRootVisitor &v0);
    void ProcessReferences(const WeakRootVisitor &v0);

    JSHandle<JSTaggedValue> GetModuleByName(JSHandle<JSTaggedValue> moduleName);

    void ExecuteModule(const std::string &moduleFile, std::string_view entryPoint,
                       const std::vector<std::string> &args);

    ModuleManager *GetModuleManager() const
    {
        return moduleManager_;
    }

    TSLoader *GetTSLoader() const
    {
        return tsLoader_;
    }
    void SetupRegExpResultCache();

    JSHandle<JSTaggedValue> GetRegExpCache() const
    {
        return JSHandle<JSTaggedValue>(reinterpret_cast<uintptr_t>(&regexpCache_));
    }

    void SetRegExpCache(JSTaggedValue newCache)
    {
        regexpCache_ = newCache;
    }

    tooling::JsDebuggerManager *GetJsDebuggerManager() const
    {
        return debuggerManager_;
    }

    void SetEnableForceGC(bool enable)
    {
        options_.SetEnableForceGC(enable);
    }

    void SetData(void* data)
    {
        data_ = data;
    }

    void SetPromiseRejectCallback(PromiseRejectCallback cb)
    {
        promiseRejectCallback_ = cb;
    }

    PromiseRejectCallback GetPromiseRejectCallback() const
    {
        return promiseRejectCallback_;
    }

    void SetHostPromiseRejectionTracker(HostPromiseRejectionTracker cb)
    {
        hostPromiseRejectionTracker_ = cb;
    }

    void PromiseRejectionTracker(const JSHandle<JSPromise> &promise,
                                 const JSHandle<JSTaggedValue> &reason, PromiseRejectionEvent operation)
    {
        if (hostPromiseRejectionTracker_ != nullptr) {
            hostPromiseRejectionTracker_(this, promise, reason, operation, data_);
        }
    }

    void SetConstpool(const JSPandaFile *jsPandaFile, JSTaggedValue constpool);

    JSTaggedValue FindConstpool(const JSPandaFile *jsPandaFile);

protected:
    bool CheckEntrypointSignature([[maybe_unused]] Method *entrypoint) override
    {
        return true;
    }

    Expected<int, Runtime::Error> InvokeEntrypointImpl(Method *entrypoint,
                                                       const std::vector<std::string> &args) override;

    void HandleUncaughtException(ObjectHeader *exception) override;

    void PrintJSErrorInfo(const JSHandle<JSTaggedValue> &exceptionInfo);

private:
    bool IsFrameworkPandaFile(std::string_view filename) const;

    void SetGlobalEnv(GlobalEnv *global);

    void SetMicroJobQueue(job::MicroJobQueue *queue);

    bool Execute(const JSPandaFile *jsPandaFile, std::string_view entryPoint, const std::vector<std::string> &args);

    Expected<int, Runtime::Error> InvokeEcmaEntrypoint(const JSPandaFile *jsPandaFile, const CString &methodName,
                                                       const std::vector<std::string> &args);

    void InitializeEcmaScriptRunStat();

    void RedirectMethod(const panda_file::File &pf);

    bool VerifyFilePath(const CString &filePath) const;

    void ClearBufferData();

    void ClearNativeMethodsData();

    NO_MOVE_SEMANTIC(EcmaVM);
    NO_COPY_SEMANTIC(EcmaVM);

    // Useless/deprecated fields in the future:
    bool isTestMode_ {false};

    // VM startup states.
    PUBLIC_API static JSRuntimeOptions options_;
    bool icEnable_ {true};
    bool vmInitialized_ {false};
    GCStats *gcStats_ {nullptr};
    bool snapshotSerializeEnable_ {false};
    bool snapshotDeserializeEnable_ {false};
    bool isUncaughtExceptionRegistered_ {false};

    // VM memory management.
    EcmaStringTable *stringTable_ {nullptr};
    std::unique_ptr<NativeAreaAllocator> nativeAreaAllocator_;
    std::unique_ptr<HeapRegionAllocator> heapRegionAllocator_;
    Chunk chunk_;
    Heap *heap_ {nullptr};
    ObjectFactory *factory_ {nullptr};
    ChunkVector<JSNativePointer *> arrayBufferDataList_;

    // VM execution states.
    JSThread *thread_ {nullptr};
    RegExpParserCache *regExpParserCache_ {nullptr};
    JSTaggedValue globalEnv_ {JSTaggedValue::Hole()};
    JSTaggedValue regexpCache_ {JSTaggedValue::Hole()};
    JSTaggedValue microJobQueue_ {JSTaggedValue::Hole()};
    bool runtimeStatEnabled_ {false};
    EcmaRuntimeStat *runtimeStat_ {nullptr};

    // App framework resources.
    JSTaggedValue frameworkProgram_ {JSTaggedValue::Hole()};
    CString frameworkAbcFileName_;
    const JSPandaFile *frameworkPandaFile_ {nullptr};
    ChunkVector<JSMethod *> frameworkProgramMethods_;
    CMap<const JSPandaFile *, JSTaggedValue> pandaFileWithConstpool_ {};

    // VM resources.
    CString snapshotFileName_;
    ChunkUnorderedMap<const void *, JSMethod *> nativeMethodMaps_;
    ModuleManager *moduleManager_ {nullptr};
    TSLoader *tsLoader_ {nullptr};
    bool optionalLogEnabled_ {false};

    // Debugger
    tooling::JsDebuggerManager *debuggerManager_ {nullptr};

    // Registered Callbacks
    PromiseRejectCallback promiseRejectCallback_ {nullptr};
    HostPromiseRejectionTracker hostPromiseRejectionTracker_ {nullptr};
    void* data_ {nullptr};

    friend class SnapShotSerialize;
    friend class ObjectFactory;
    friend class ValueSerializer;
    friend class panda::JSNApi;
};
}  // namespace ecmascript
}  // namespace panda

#endif
