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

#include "ecmascript/ecma_vm.h"

#include "ecmascript/base/string_helper.h"
#include "ecmascript/builtins/builtins.h"
#include "ecmascript/builtins/builtins_ark_tools.h"
#include "ecmascript/checkpoint/thread_state_transition.h"
#ifdef ARK_SUPPORT_INTL
#include "ecmascript/builtins/builtins_collator.h"
#include "ecmascript/builtins/builtins_date_time_format.h"
#include "ecmascript/builtins/builtins_number_format.h"
#endif
#include "ecmascript/builtins/builtins_global.h"
#include "ecmascript/builtins/builtins_object.h"
#include "ecmascript/builtins/builtins_promise.h"
#include "ecmascript/builtins/builtins_promise_handler.h"
#include "ecmascript/builtins/builtins_proxy.h"
#include "ecmascript/builtins/builtins_regexp.h"
#include "ecmascript/compiler/builtins/builtins_call_signature.h"
#include "ecmascript/compiler/call_signature.h"
#include "ecmascript/compiler/common_stubs.h"
#include "ecmascript/compiler/interpreter_stub.h"
#include "ecmascript/compiler/rt_call_signature.h"
#include "ecmascript/jit/jit.h"
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER)
#include "ecmascript/dfx/cpu_profiler/cpu_profiler.h"
#endif
#if !WIN_OR_MAC_OR_IOS_PLATFORM
#include "ecmascript/dfx/hprof/heap_profiler.h"
#include "ecmascript/dfx/hprof/heap_profiler_interface.h"
#endif
#include "ecmascript/compiler/aot_file/an_file_data_manager.h"
#include "ecmascript/compiler/aot_file/aot_file_manager.h"
#include "ecmascript/debugger/js_debugger_manager.h"
#include "ecmascript/dfx/stackinfo/js_stackinfo.h"
#include "ecmascript/dfx/tracing/tracing.h"
#include "ecmascript/dfx/vmstat/function_call_timer.h"
#include "ecmascript/dfx/vmstat/opt_code_profiler.h"
#include "ecmascript/dfx/vmstat/runtime_stat.h"
#include "ecmascript/ecma_context.h"
#include "ecmascript/ecma_string_table.h"
#include "ecmascript/global_env.h"
#include "ecmascript/global_env_constants-inl.h"
#include "ecmascript/global_env_constants.h"
#include "ecmascript/interpreter/interpreter-inl.h"
#include "ecmascript/jobs/micro_job_queue.h"
#include "ecmascript/js_arraybuffer.h"
#include "ecmascript/js_for_in_iterator.h"
#include "ecmascript/js_native_pointer.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/jspandafile/constpool_value.h"
#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/jspandafile/panda_file_translator.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/mem/concurrent_marker.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/mem/heap.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/mem/space.h"
#include "ecmascript/mem/visitor.h"
#include "ecmascript/module/js_module_manager.h"
#include "ecmascript/module/module_data_extractor.h"
#include "ecmascript/module/module_path_helper.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/patch/quick_fix_manager.h"
#include "ecmascript/pgo_profiler/pgo_profiler_manager.h"
#include "ecmascript/regexp/regexp_parser_cache.h"
#include "ecmascript/runtime.h"
#include "ecmascript/runtime_call_id.h"
#include "ecmascript/snapshot/mem/snapshot.h"
#include "ecmascript/snapshot/mem/snapshot_env.h"
#include "ecmascript/stubs/runtime_stubs.h"
#include "ecmascript/tagged_array-inl.h"
#include "ecmascript/tagged_dictionary.h"
#include "ecmascript/tagged_queue.h"
#include "ecmascript/tagged_queue.h"
#include "ecmascript/taskpool/task.h"
#include "ecmascript/taskpool/taskpool.h"
#include "ecmascript/ts_types/ts_manager.h"

#include "ecmascript/ohos/enable_aot_list_helper.h"

namespace panda::ecmascript {
using RandomGenerator = base::RandomGenerator;
using PGOProfilerManager = pgo::PGOProfilerManager;
AOTFileManager *JsStackInfo::loader = nullptr;

EcmaVM *EcmaVM::Create(const JSRuntimeOptions &options)
{
    Runtime::CreateIfFirstVm(options);
    auto heapType = options.IsWorker() ? EcmaParamConfiguration::HeapType::WORKER_HEAP :
        EcmaParamConfiguration::HeapType::DEFAULT_HEAP;
    auto config = EcmaParamConfiguration(heapType,
                                         MemMapAllocator::GetInstance()->GetCapacity());
    JSRuntimeOptions newOptions = options;
    // only define SUPPORT_ENABLE_ASM_INTERP can enable asm-interpreter
#if !defined(SUPPORT_ENABLE_ASM_INTERP)
    newOptions.SetEnableAsmInterpreter(false);
#endif
    auto vm = new EcmaVM(newOptions, config);
    auto jsThread = JSThread::Create(vm);
    vm->thread_ = jsThread;
    Runtime::GetInstance()->InitializeIfFirstVm(vm);
    if (JsStackInfo::loader == nullptr) {
        JsStackInfo::loader = vm->GetAOTFileManager();
    }
#if defined(__aarch64__) && !defined(PANDA_TARGET_MACOS) && !defined(PANDA_TARGET_IOS)
    if (SetThreadInfoCallback != nullptr) {
        SetThreadInfoCallback(CrashCallback);
    }
#endif
    return vm;
}

// static
bool EcmaVM::Destroy(EcmaVM *vm)
{
    if (UNLIKELY(vm == nullptr)) {
        return false;
    }
    delete vm;
    Runtime::DestroyIfLastVm();
    return true;
}

void EcmaVM::PreFork()
{
    heap_->CompactHeapBeforeFork();
    heap_->AdjustSpaceSizeForAppSpawn();
    heap_->GetReadOnlySpace()->SetReadOnly();
    SharedHeap::GetInstance()->DisableParallelGC();
    heap_->DisableParallelGC();
}

void EcmaVM::PostFork()
{
    RandomGenerator::InitRandom();
    heap_->SetHeapMode(HeapMode::SHARE);
    GetAssociatedJSThread()->PostFork();
    Taskpool::GetCurrentTaskpool()->Initialize();
    SharedHeap::GetInstance()->EnableParallelGC(GetJSOptions());
    heap_->EnableParallelGC();
    std::string bundleName = PGOProfilerManager::GetInstance()->GetBundleName();
    if (ohos::EnableAotListHelper::GetInstance()->IsDisableBlackList(bundleName)) {
        options_.SetEnablePGOProfiler(false);
        PGOProfilerManager::GetInstance()->SetDisableAot(true);
    } else if (ohos::EnableAotListHelper::GetInstance()->IsEnableList(bundleName)) {
        options_.SetEnablePGOProfiler(true);
    }
    ResetPGOProfiler();

    bool isEnableJit = options_.IsEnableJIT() && options_.GetEnableAsmInterpreter();
    options_.SetEnableAPPJIT(true);
    Jit::GetInstance()->SetEnableOrDisable(options_, isEnableJit);
#ifdef ENABLE_POSTFORK_FORCEEXPAND
    heap_->NotifyPostFork();
    heap_->NotifyFinishColdStartSoon();
#endif
}

EcmaVM::EcmaVM(JSRuntimeOptions options, EcmaParamConfiguration config)
    : nativeAreaAllocator_(std::make_unique<NativeAreaAllocator>()),
      heapRegionAllocator_(std::make_unique<HeapRegionAllocator>()),
      chunk_(nativeAreaAllocator_.get()),
      ecmaParamConfiguration_(std::move(config))
{
    options_ = std::move(options);
    icEnabled_ = options_.EnableIC();
    optionalLogEnabled_ = options_.EnableOptionalLog();
    options_.ParseAsmInterOption();
    SetEnableOsr(options_.IsEnableOSR() && options_.IsEnableJIT() && options_.GetEnableAsmInterpreter());
}

void EcmaVM::InitializePGOProfiler()
{
    bool isEnablePGOProfiler = IsEnablePGOProfiler();
    if (pgoProfiler_ == nullptr) {
        pgoProfiler_ = PGOProfilerManager::GetInstance()->Build(this, isEnablePGOProfiler);
    }
    thread_->SetPGOProfilerEnable(isEnablePGOProfiler);
}

void EcmaVM::ResetPGOProfiler()
{
    if (pgoProfiler_ != nullptr) {
        bool isEnablePGOProfiler = IsEnablePGOProfiler();
        PGOProfilerManager::GetInstance()->Reset(pgoProfiler_, isEnablePGOProfiler);
        thread_->SetPGOProfilerEnable(isEnablePGOProfiler);
        thread_->CheckOrSwitchPGOStubs();
    }
}

bool EcmaVM::IsEnablePGOProfiler() const
{
    if (options_.IsWorker()) {
        return PGOProfilerManager::GetInstance()->IsEnable();
    }
    return options_.GetEnableAsmInterpreter() && options_.IsEnablePGOProfiler();
}

bool EcmaVM::IsEnableElementsKind() const
{
    if (options_.IsWorker()) {
        return false;
    }
    return options_.GetEnableAsmInterpreter() && options_.IsEnableElementsKind();
}

bool EcmaVM::IsEnableJit() const
{
    return GetJit()->IsEnable();
}

void EcmaVM::EnableJit() const
{
    GetJSThread()->SwitchJitProfileStubs();
}

Jit *EcmaVM::GetJit() const
{
    return Jit::GetInstance();
}

bool EcmaVM::Initialize()
{
    ECMA_BYTRACE_NAME(HITRACE_TAG_ARK, "EcmaVM::Initialize");
    stringTable_ = Runtime::GetInstance()->GetEcmaStringTable();
    InitializePGOProfiler();
    Taskpool::GetCurrentTaskpool()->Initialize();
#ifndef PANDA_TARGET_WINDOWS
    RuntimeStubs::Initialize(thread_);
#endif
    heap_ = new Heap(this);
    heap_->Initialize();
    gcStats_ = chunk_.New<GCStats>(heap_, options_.GetLongPauseTime());
    gcKeyStats_ = chunk_.New<GCKeyStats>(heap_, gcStats_);
    factory_ = chunk_.New<ObjectFactory>(thread_, heap_, SharedHeap::GetInstance());
    if (UNLIKELY(factory_ == nullptr)) {
        LOG_FULL(FATAL) << "alloc factory_ failed";
        UNREACHABLE();
    }
    debuggerManager_ = chunk_.New<tooling::JsDebuggerManager>(this);
    aotFileManager_ = new AOTFileManager(this);
    auto context = new EcmaContext(thread_);
    thread_->PushContext(context);
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    thread_->SetReadyForGCIterating();
    snapshotEnv_ = new SnapshotEnv(this);
    context->Initialize();
    snapshotEnv_->AddGlobalConstToMap();
    thread_->SetGlueGlobalEnv(reinterpret_cast<GlobalEnv *>(context->GetGlobalEnv().GetTaggedType()));
    thread_->SetGlobalObject(GetGlobalEnv()->GetGlobalObject());
    thread_->SetCurrentEcmaContext(context);
    GenerateInternalNativeMethods();
    quickFixManager_ = new QuickFixManager();
    if (options_.GetEnableAsmInterpreter()) {
        thread_->GetCurrentEcmaContext()->LoadStubFile();
    }

    callTimer_ = new FunctionCallTimer();
    strategy_ = new ThroughputJSObjectResizingStrategy();
    if (IsEnableJit()) {
        EnableJit();
    }
    initialized_ = true;
    return true;
}

EcmaVM::~EcmaVM()
{
    ASSERT(thread_->IsInRunningStateOrProfiling());
    initialized_ = false;
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER)
    if (thread_->isProfiling_) {
        if (profiler_->GetOutToFile()) {
            DFXJSNApi::StopCpuProfilerForFile(this);
        } else {
            DFXJSNApi::StopCpuProfilerForInfo(this);
        }
    }
#endif
#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    DeleteHeapProfile();
#endif
    heap_->WaitAllTasksFinished();
    Taskpool::GetCurrentTaskpool()->Destroy(thread_->GetThreadId());

    if (pgoProfiler_ != nullptr) {
        PGOProfilerManager::GetInstance()->Destroy(pgoProfiler_);
        pgoProfiler_ = nullptr;
    }

#if ECMASCRIPT_ENABLE_FUNCTION_CALL_TIMER
    DumpCallTimeInfo();
#endif

#if defined(ECMASCRIPT_SUPPORT_TRACING)
    if (tracing_) {
        DFXJSNApi::StopTracing(this);
    }
#endif

    // clear c_address: c++ pointer delete
    ClearBufferData();
    if (!isBundlePack_) {
        std::shared_ptr<JSPandaFile> jsPandaFile = JSPandaFileManager::GetInstance()->FindJSPandaFile(assetPath_);
        if (jsPandaFile != nullptr) {
            jsPandaFile->DeleteParsedConstpoolVM(this);
        }
    }

    if (gcStats_ != nullptr) {
        if (options_.EnableGCStatsPrint()) {
            gcStats_->PrintStatisticResult();
        }
        chunk_.Delete(gcStats_);
        gcStats_ = nullptr;
    }

    if (gcKeyStats_ != nullptr) {
        chunk_.Delete(gcKeyStats_);
        gcKeyStats_ = nullptr;
    }

    if (JsStackInfo::loader == aotFileManager_) {
        JsStackInfo::loader = nullptr;
    }

    if (heap_ != nullptr) {
        heap_->Destroy();
        delete heap_;
        heap_ = nullptr;
    }

    if (debuggerManager_ != nullptr) {
        chunk_.Delete(debuggerManager_);
        debuggerManager_ = nullptr;
    }

    if (aotFileManager_ != nullptr) {
        delete aotFileManager_;
        aotFileManager_ = nullptr;
    }

    if (factory_ != nullptr) {
        chunk_.Delete(factory_);
        factory_ = nullptr;
    }

    if (stringTable_ != nullptr) {
        stringTable_ = nullptr;
    }

    if (quickFixManager_ != nullptr) {
        delete quickFixManager_;
        quickFixManager_ = nullptr;
    }

    if (snapshotEnv_ != nullptr) {
        snapshotEnv_->ClearEnvMap();
        delete snapshotEnv_;
        snapshotEnv_ = nullptr;
    }

    if (callTimer_ != nullptr) {
        delete callTimer_;
        callTimer_ = nullptr;
    }

    if (strategy_ != nullptr) {
        delete strategy_;
        strategy_ = nullptr;
    }

    if (thread_ != nullptr) {
        delete thread_;
        thread_ = nullptr;
    }
}

JSHandle<GlobalEnv> EcmaVM::GetGlobalEnv() const
{
    return thread_->GetCurrentEcmaContext()->GetGlobalEnv();
}

JSTaggedValue EcmaVM::FastCallAot(size_t actualNumArgs, JSTaggedType *args, const JSTaggedType *prevFp)
{
    INTERPRETER_TRACE(thread_, ExecuteAot);
    ASSERT(thread_->IsInManagedState());
    auto entry = thread_->GetRTInterface(kungfu::RuntimeStubCSigns::ID_OptimizedFastCallEntry);
    // do not modify this log to INFO, this will call many times
    LOG_ECMA(DEBUG) << "start to execute aot entry: " << (void*)entry;
    auto res = reinterpret_cast<FastCallAotEntryType>(entry)(thread_->GetGlueAddr(),
                                                             actualNumArgs,
                                                             args,
                                                             reinterpret_cast<uintptr_t>(prevFp));
    return res;
}

void EcmaVM::CheckStartCpuProfiler()
{
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER)
    if (options_.EnableCpuProfilerColdStartMainThread() && options_.GetArkBundleName().compare(bundleName_) == 0 &&
        !options_.IsWorker() && profiler_ == nullptr) {
        std::string fileName = options_.GetArkBundleName() + ".cpuprofile";
        if (!builtins::BuiltinsArkTools::CreateFile(fileName)) {
            LOG_ECMA(ERROR) << "createFile failed " << fileName;
            return;
        } else {
            DFXJSNApi::StartCpuProfilerForFile(this, fileName, CpuProfiler::INTERVAL_OF_INNER_START);
            return;
        }
    }

    if (options_.EnableCpuProfilerColdStartWorkerThread() && options_.GetArkBundleName().compare(bundleName_) == 0 &&
        options_.IsWorker() && profiler_ == nullptr) {
        std::string fileName = options_.GetArkBundleName() + "_"
                               + std::to_string(thread_->GetThreadId()) + ".cpuprofile";
        if (!builtins::BuiltinsArkTools::CreateFile(fileName)) {
            LOG_ECMA(ERROR) << "createFile failed " << fileName;
            return;
        } else {
            DFXJSNApi::StartCpuProfilerForFile(this, fileName, CpuProfiler::INTERVAL_OF_INNER_START);
            return;
        }
    }
#endif
}

JSHandle<JSTaggedValue> EcmaVM::GetAndClearEcmaUncaughtException() const
{
    JSHandle<JSTaggedValue> exceptionHandle = GetEcmaUncaughtException();
    thread_->ClearException();  // clear for ohos app
    return exceptionHandle;
}

JSHandle<JSTaggedValue> EcmaVM::GetEcmaUncaughtException() const
{
    if (!thread_->HasPendingException()) {
        return JSHandle<JSTaggedValue>();
    }
    JSHandle<JSTaggedValue> exceptionHandle(thread_, thread_->GetException());
    return exceptionHandle;
}

void EcmaVM::PrintJSErrorInfo(const JSHandle<JSTaggedValue> &exceptionInfo) const
{
    EcmaContext::PrintJSErrorInfo(thread_, exceptionInfo);
}

void EcmaVM::ProcessNativeDelete(const WeakRootVisitor &visitor)
{
    if (!heap_->IsYoungGC()) {
        auto iter = nativePointerList_.begin();
        while (iter != nativePointerList_.end()) {
            JSNativePointer *object = *iter;
            auto fwd = visitor(reinterpret_cast<TaggedObject *>(object));
            if (fwd == nullptr) {
                nativeAreaAllocator_->DecreaseNativeSizeStats(object->GetBindingSize(), object->GetNativeFlag());
                object->Destroy();
                iter = nativePointerList_.erase(iter);
            } else {
                ++iter;
            }
        }

        auto newIter = concurrentNativePointerList_.begin();
        while (newIter != concurrentNativePointerList_.end()) {
            JSNativePointer *object = *newIter;
            auto fwd = visitor(reinterpret_cast<TaggedObject *>(object));
            if (fwd == nullptr) {
                nativeAreaAllocator_->DecreaseNativeSizeStats(object->GetBindingSize(), object->GetNativeFlag());
                nativePointerCallbacks_.emplace_back(std::make_pair(object->GetDeleter(),
                                                                    std::make_pair(object->GetExternalPointer(),
                                                                                   object->GetData())));
                newIter = concurrentNativePointerList_.erase(newIter);
            } else {
                ++newIter;
            }
        }
    }
}

void EcmaVM::ProcessSharedNativeDelete(const WeakRootVisitor &visitor)
{
    auto sharedIter = sharedNativePointerList_.begin();
    while (sharedIter != sharedNativePointerList_.end()) {
        JSNativePointer *object = *sharedIter;
        auto fwd = visitor(reinterpret_cast<TaggedObject *>(object));
        if (fwd == nullptr) {
            sharedNativePointerCallbacks_.emplace_back(
                std::make_pair(object->GetDeleter(), std::make_pair(object->GetExternalPointer(), object->GetData())));
            sharedIter = sharedNativePointerList_.erase(sharedIter);
        } else {
            ++sharedIter;
        }
    }
}

void EcmaVM::ProcessReferences(const WeakRootVisitor &visitor)
{
    if (thread_->GetCurrentEcmaContext()->GetRegExpParserCache() != nullptr) {
        thread_->GetCurrentEcmaContext()->GetRegExpParserCache()->Clear();
    }
    if (!heap_->IsYoungGC()) {
        heap_->ResetNativeBindingSize();
        // array buffer
        auto iter = nativePointerList_.begin();
        ECMA_BYTRACE_NAME(HITRACE_TAG_ARK, "ProcessNativeDeleteNum:" + std::to_string(nativePointerList_.size()));
        while (iter != nativePointerList_.end()) {
            JSNativePointer *object = *iter;
            auto fwd = visitor(reinterpret_cast<TaggedObject *>(object));
            if (fwd == nullptr) {
                nativeAreaAllocator_->DecreaseNativeSizeStats(object->GetBindingSize(), object->GetNativeFlag());
                object->Destroy();
                iter = nativePointerList_.erase(iter);
                continue;
            }
            heap_->IncreaseNativeBindingSize(JSNativePointer::Cast(fwd));
            if (fwd != reinterpret_cast<TaggedObject *>(object)) {
                *iter = JSNativePointer::Cast(fwd);
            }
            ++iter;
        }

        auto newIter = concurrentNativePointerList_.begin();
        while (newIter != concurrentNativePointerList_.end()) {
            JSNativePointer *object = *newIter;
            auto fwd = visitor(reinterpret_cast<TaggedObject *>(object));
            if (fwd == nullptr) {
                nativeAreaAllocator_->DecreaseNativeSizeStats(object->GetBindingSize(), object->GetNativeFlag());
                nativePointerCallbacks_.emplace_back(std::make_pair(object->GetDeleter(),
                                                                    std::make_pair(object->GetExternalPointer(),
                                                                                   object->GetData())));
                newIter = concurrentNativePointerList_.erase(newIter);
                continue;
            }
            heap_->IncreaseNativeBindingSize(JSNativePointer::Cast(fwd));
            if (fwd != reinterpret_cast<TaggedObject *>(object)) {
                *newIter = JSNativePointer::Cast(fwd);
            }
            ++newIter;
        }
    }
    GetPGOProfiler()->ProcessReferences(visitor);
}

void EcmaVM::PushToNativePointerList(JSNativePointer *pointer, Concurrent isConcurrent)
{
    ASSERT(!JSTaggedValue(pointer).IsInSharedHeap());
    if (isConcurrent == Concurrent::YES) {
        concurrentNativePointerList_.emplace_back(pointer);
    } else {
        nativePointerList_.emplace_back(pointer);
    }
}

void EcmaVM::PushToSharedNativePointerList(JSNativePointer *pointer)
{
    ASSERT(JSTaggedValue(pointer).IsInSharedHeap());
    sharedNativePointerList_.emplace_back(pointer);
}

void EcmaVM::RemoveFromNativePointerList(JSNativePointer *pointer)
{
    auto iter = std::find(nativePointerList_.begin(), nativePointerList_.end(), pointer);
    if (iter != nativePointerList_.end()) {
        JSNativePointer *object = *iter;
        nativeAreaAllocator_->DecreaseNativeSizeStats(object->GetBindingSize(), object->GetNativeFlag());
        object->Destroy();
        nativePointerList_.erase(iter);
    }
    auto newIter = std::find(concurrentNativePointerList_.begin(), concurrentNativePointerList_.end(), pointer);
    if (newIter != concurrentNativePointerList_.end()) {
        JSNativePointer *object = *newIter;
        nativeAreaAllocator_->DecreaseNativeSizeStats(object->GetBindingSize(), object->GetNativeFlag());
        object->Destroy();
        concurrentNativePointerList_.erase(newIter);
    }
}

void EcmaVM::PushToDeregisterModuleList(CString module)
{
    deregisterModuleList_.emplace_back(module);
}

void EcmaVM::RemoveFromDeregisterModuleList(CString module)
{
    auto iter = std::find(deregisterModuleList_.begin(), deregisterModuleList_.end(), module);
    if (iter != deregisterModuleList_.end()) {
        deregisterModuleList_.erase(iter);
    }
}

bool EcmaVM::ContainInDeregisterModuleList(CString module)
{
    return (std::find(deregisterModuleList_.begin(), deregisterModuleList_.end(), module)
        != deregisterModuleList_.end());
}

void EcmaVM::ClearBufferData()
{
    for (auto iter : nativePointerList_) {
        iter->Destroy();
    }
    for (auto iter : concurrentNativePointerList_) {
        iter->Destroy();
    }
    nativePointerList_.clear();
    thread_->GetCurrentEcmaContext()->ClearBufferData();
    internalNativeMethods_.clear();
    workerList_.clear();
    deregisterModuleList_.clear();
}

void EcmaVM::CollectGarbage(TriggerGCType gcType, GCReason reason) const
{
    heap_->CollectGarbage(gcType, reason);
}

void EcmaVM::Iterate(const RootVisitor &v, const RootRangeVisitor &rv)
{
    rv(Root::ROOT_VM, ObjectSlot(ToUintPtr(&internalNativeMethods_.front())),
        ObjectSlot(ToUintPtr(&internalNativeMethods_.back()) + JSTaggedValue::TaggedTypeSize()));
    if (!WIN_OR_MAC_OR_IOS_PLATFORM && snapshotEnv_!= nullptr) {
        snapshotEnv_->Iterate(v);
    }
    if (pgoProfiler_ != nullptr) {
        pgoProfiler_->Iterate(v);
    }
}

#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
void EcmaVM::DeleteHeapProfile()
{
    if (heapProfile_ == nullptr) {
        return;
    }
    delete heapProfile_;
    heapProfile_ = nullptr;
}

HeapProfilerInterface *EcmaVM::GetHeapProfile()
{
    if (heapProfile_ != nullptr) {
        return heapProfile_;
    }
    return nullptr;
}

HeapProfilerInterface *EcmaVM::GetOrNewHeapProfile()
{
    if (heapProfile_ != nullptr) {
        return heapProfile_;
    }
    heapProfile_ = new HeapProfiler(this);
    ASSERT(heapProfile_ != nullptr);
    return heapProfile_;
}

void EcmaVM::StartHeapTracking()
{
    heap_->StartHeapTracking();
}

void EcmaVM::StopHeapTracking()
{
    heap_->StopHeapTracking();
}
#endif

// NOLINTNEXTLINE(modernize-avoid-c-arrays)
void *EcmaVM::InternalMethodTable[] = {
    reinterpret_cast<void *>(builtins::BuiltinsGlobal::CallJsBoundFunction),
    reinterpret_cast<void *>(builtins::BuiltinsGlobal::CallJsProxy),
    reinterpret_cast<void *>(builtins::BuiltinsObject::CreateDataPropertyOnObjectFunctions),
#ifdef ARK_SUPPORT_INTL
    reinterpret_cast<void *>(builtins::BuiltinsCollator::AnonymousCollator),
    reinterpret_cast<void *>(builtins::BuiltinsDateTimeFormat::AnonymousDateTimeFormat),
    reinterpret_cast<void *>(builtins::BuiltinsNumberFormat::NumberFormatInternalFormatNumber),
#endif
    reinterpret_cast<void *>(builtins::BuiltinsProxy::InvalidateProxyFunction),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::AsyncAwaitFulfilled),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::AsyncAwaitRejected),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::ResolveElementFunction),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::Resolve),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::Reject),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::Executor),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::AnyRejectElementFunction),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::AllSettledResolveElementFunction),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::AllSettledRejectElementFunction),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::ThenFinally),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::CatchFinally),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::valueThunkFunction),
    reinterpret_cast<void *>(builtins::BuiltinsPromiseHandler::throwerFunction),
    reinterpret_cast<void *>(JSAsyncGeneratorObject::ProcessorFulfilledFunc),
    reinterpret_cast<void *>(JSAsyncGeneratorObject::ProcessorRejectedFunc),
    reinterpret_cast<void *>(JSAsyncFromSyncIterator::AsyncFromSyncIterUnwarpFunction),
    reinterpret_cast<void *>(SourceTextModule::AsyncModuleFulfilledFunc),
    reinterpret_cast<void *>(SourceTextModule::AsyncModuleRejectedFunc)
};

void EcmaVM::GenerateInternalNativeMethods()
{
    size_t length = static_cast<size_t>(MethodIndex::METHOD_END);
    constexpr uint32_t numArgs = 2;  // function object and this
    for (size_t i = 0; i < length; i++) {
        auto method = factory_->NewSMethod(nullptr, MemSpaceType::SHARED_NON_MOVABLE);
        method->SetNativePointer(InternalMethodTable[i]);
        method->SetNativeBit(true);
        method->SetNumArgsWithCallField(numArgs);
        method->SetFunctionKind(FunctionKind::NORMAL_FUNCTION);
        internalNativeMethods_.emplace_back(method.GetTaggedValue());
    }
}

JSTaggedValue EcmaVM::GetMethodByIndex(MethodIndex idx)
{
    auto index = static_cast<uint8_t>(idx);
    ASSERT(index < internalNativeMethods_.size());
    return internalNativeMethods_[index];
}

void EcmaVM::TriggerConcurrentCallback(JSTaggedValue result, JSTaggedValue hint)
{
    if (concurrentCallback_ == nullptr) {
        LOG_ECMA(DEBUG) << "Only trigger concurrent callback in taskpool thread";
        return;
    }

    bool success = true;
    if (result.IsJSPromise()) {
        // Async concurrent will return Promise
        auto promise = JSPromise::Cast(result.GetTaggedObject());
        auto status = promise->GetPromiseState();
        if (status == PromiseState::PENDING) {
            result = JSHandle<JSTaggedValue>::Cast(factory_->GetJSError(
                ErrorType::ERROR, "Can't return Promise in pending state")).GetTaggedValue();
        } else {
            result = promise->GetPromiseResult();
        }

        if (status != PromiseState::FULFILLED) {
            success = false;
        }
    }

    JSHandle<JSTaggedValue> functionValue(thread_, hint);
    if (!functionValue->IsJSFunction()) {
        LOG_ECMA(ERROR) << "TriggerConcurrentCallback hint is not function";
        return;
    }
    JSHandle<JSFunction> functionInfo(functionValue);
    if (functionInfo->IsCallNapi()) {
        LOG_ECMA(INFO) << "Function is not taskpool task";
        return;
    }
    JSTaggedValue extraInfoValue = functionInfo->GetFunctionExtraInfo();
    if (!extraInfoValue.IsJSNativePointer()) {
        LOG_ECMA(INFO) << "FunctionExtraInfo is not JSNativePointer";
        return;
    }
    JSHandle<JSNativePointer> extraInfo(thread_, extraInfoValue);
    void *taskInfo = extraInfo->GetData();

    concurrentCallback_(JSNApiHelper::ToLocal<JSValueRef>(JSHandle<JSTaggedValue>(thread_, result)), success,
                        taskInfo, concurrentData_);
}

void EcmaVM::DumpCallTimeInfo()
{
    if (callTimer_ != nullptr) {
        callTimer_->PrintAllStats();
    }
}

void EcmaVM::WorkersetInfo(EcmaVM *workerVm)
{
    LockHolder lock(mutex_);
    auto thread = workerVm->GetJSThread();
    if (thread != nullptr) {
        auto tid = thread->GetThreadId();
        if (tid != 0) {
            workerList_.emplace(tid, workerVm);
        }
    }
}

EcmaVM *EcmaVM::GetWorkerVm(uint32_t tid)
{
    LockHolder lock(mutex_);
    EcmaVM *workerVm = nullptr;
    if (!workerList_.empty()) {
        auto iter = workerList_.find(tid);
        if (iter != workerList_.end()) {
            workerVm = iter->second;
        }
    }
    return workerVm;
}

bool EcmaVM::DeleteWorker(EcmaVM *workerVm)
{
    LockHolder lock(mutex_);
    auto thread = workerVm->GetJSThread();
    if (thread != nullptr) {
        auto tid = thread->GetThreadId();
        if (tid == 0) {
            return false;
        }
        auto iter = workerList_.find(tid);
        if (iter != workerList_.end()) {
            workerList_.erase(iter);
            return true;
        }
        return false;
    }
    return false;
}

bool EcmaVM::SuspendWorkerVm(uint32_t tid)
{
    LockHolder lock(mutex_);
    if (!workerList_.empty()) {
        auto iter = workerList_.find(tid);
        if (iter != workerList_.end()) {
            return DFXJSNApi::SuspendVM(iter->second);
        }
    }
    return false;
}

void EcmaVM::ResumeWorkerVm(uint32_t tid)
{
    LockHolder lock(mutex_);
    if (!workerList_.empty()) {
        auto iter = workerList_.find(tid);
        if (iter != workerList_.end()) {
            DFXJSNApi::ResumeVM(iter->second);
        }
    }
}

/*  This moduleName is a readOnly variable for napi, represent which abc is running in current vm.
*   Get Current recordName: bundleName/moduleName/ets/xxx/xxx
*                           pkg_modules@xxx/xxx/xxx
*   Get Current fileName: /data/storage/el1/bundle/moduleName/ets/modules.abc
*   output: moduleName: moduleName
*   if needRecordName then fileName is: moduleName/ets/modules.abc
*/
std::pair<std::string, std::string> EcmaVM::GetCurrentModuleInfo(bool needRecordName)
{
    std::pair<JSTaggedValue, JSTaggedValue> moduleInfo = EcmaInterpreter::GetCurrentEntryPoint(thread_);
    CString recordName = ConvertToString(moduleInfo.first);
    CString fileName = ConvertToString(moduleInfo.second);
    LOG_FULL(INFO) << "Current recordName is " << recordName <<", current fileName is " << fileName;
    if (needRecordName) {
        if (fileName.length() > ModulePathHelper::BUNDLE_INSTALL_PATH_LEN &&
            fileName.find(ModulePathHelper::BUNDLE_INSTALL_PATH) == 0) {
            fileName = fileName.substr(ModulePathHelper::BUNDLE_INSTALL_PATH_LEN);
        } else {
            LOG_FULL(ERROR) << " GetCurrentModuleName Fail, fileName is " << fileName;
        }
        return std::make_pair(recordName.c_str(), fileName.c_str());
    }
    CString moduleName = ModulePathHelper::GetModuleName(recordName);
    if (moduleName.empty()) {
        LOG_FULL(ERROR) << " GetCurrentModuleName Fail, recordName is " << recordName;
    }
    return std::make_pair(moduleName.c_str(), fileName.c_str());
}

void EcmaVM::SetHmsModuleList(const std::vector<panda::HmsMap> &list)
{
    for (size_t i = 0; i < list.size(); i++) {
        HmsMap hmsMap = list[i];
        hmsModuleList_.emplace(hmsMap.originalPath.c_str(), hmsMap);
    }
}

CString EcmaVM::GetHmsModule(const CString &module) const
{
    auto it = hmsModuleList_.find(module);
    if (it == hmsModuleList_.end()) {
        LOG_ECMA(FATAL) << " Get Hms Module failed";
    }
    HmsMap hmsMap = it->second;
    return hmsMap.targetPath.c_str();
}

bool EcmaVM::IsHmsModule(const CString &moduleStr) const
{
    if (hmsModuleList_.empty()) {
        return false;
    }
    auto it = hmsModuleList_.find(moduleStr);
    if (it == hmsModuleList_.end()) {
        return false;
    }
    return true;
}

// Initialize IcuData Path
void EcmaVM::InitializeIcuData(const JSRuntimeOptions &options)
{
    std::string icuPath = options.GetIcuDataPath();
    if (icuPath == "default") {
#if !WIN_OR_MAC_OR_IOS_PLATFORM && !defined(PANDA_TARGET_LINUX)
        SetHwIcuDirectory();
#endif
    } else {
        std::string absPath;
        if (ecmascript::RealPath(icuPath, absPath)) {
            u_setDataDirectory(absPath.c_str());
        }
    }
}
}  // namespace panda::ecmascript
