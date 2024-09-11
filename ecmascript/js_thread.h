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

#ifndef ECMASCRIPT_JS_THREAD_H
#define ECMASCRIPT_JS_THREAD_H

#include <atomic>
#include <sstream>
#include <string>
#include <cstdint>

#include "ecmascript/base/aligned_struct.h"
#include "ecmascript/builtin_entries.h"
#include "ecmascript/daemon/daemon_task.h"
#include "ecmascript/elements.h"
#include "ecmascript/frames.h"
#include "ecmascript/global_env_constants.h"
#include "ecmascript/global_index.h"
#include "ecmascript/js_object_resizing_strategy.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/js_thread_hclass_entries.h"
#include "ecmascript/js_thread_stub_entries.h"
#include "ecmascript/log_wrapper.h"
#include "ecmascript/mem/visitor.h"
#include "ecmascript/mutator_lock.h"
#include "ecmascript/mem/machine_code.h"

#if defined(ENABLE_FFRT_INTERFACES)
#include "ffrt.h"
#include "c/executor_task.h"
#endif

namespace panda::ecmascript {
class EcmaContext;
class EcmaVM;
class EcmaHandleScope;
class GlobalIndex;
class HeapRegionAllocator;
class PropertiesCache;
template<typename T>
class EcmaGlobalStorage;
class Node;
class DebugNode;
class VmThreadControl;
using WeakClearCallback = void (*)(void *);

enum class MarkStatus : uint8_t {
    READY_TO_MARK,
    MARKING,
    MARK_FINISHED,
};

enum class GCKind : uint8_t {
    LOCAL_GC,
    SHARED_GC
};

enum class PGOProfilerStatus : uint8_t {
    PGO_PROFILER_DISABLE,
    PGO_PROFILER_ENABLE,
};

enum class BCStubStatus: uint8_t {
    NORMAL_BC_STUB,
    PROFILE_BC_STUB,
    JIT_PROFILE_BC_STUB,
};

enum class StableArrayChangeKind { PROTO, NOT_PROTO };

enum ThreadType : uint8_t {
    JS_THREAD,
    JIT_THREAD,
    DAEMON_THREAD,
};

enum ThreadFlag : uint16_t {
    NO_FLAGS = 0 << 0,
    SUSPEND_REQUEST = 1 << 0,
    ACTIVE_BARRIER = 1 << 1,
};

static constexpr uint32_t THREAD_STATE_OFFSET = 16;
static constexpr uint32_t THREAD_FLAGS_MASK = (0x1 << THREAD_STATE_OFFSET) - 1;
enum class ThreadState : uint16_t {
    CREATED = 0,
    RUNNING = 1,
    NATIVE = 2,
    WAIT = 3,
    IS_SUSPENDED = 4,
    TERMINATED = 5,
};

union ThreadStateAndFlags {
    explicit ThreadStateAndFlags(uint32_t val = 0): asInt(val) {}
    struct {
        volatile uint16_t flags;
        volatile ThreadState state;
    } asStruct;
    volatile uint32_t asInt;
    uint32_t asNonvolatileInt;
    std::atomic<uint32_t> asAtomicInt;
private:
    NO_COPY_SEMANTIC(ThreadStateAndFlags);
};

static constexpr uint32_t MAIN_THREAD_INDEX = 0;

class JSThread {
public:
    static constexpr int CONCURRENT_MARKING_BITFIELD_NUM = 2;
    static constexpr int CONCURRENT_MARKING_BITFIELD_MASK = 0x3;
    static constexpr int SHARED_CONCURRENT_MARKING_BITFIELD_NUM = 1;
    static constexpr int SHARED_CONCURRENT_MARKING_BITFIELD_MASK = 0x1;
    static constexpr int CHECK_SAFEPOINT_BITFIELD_NUM = 8;
    static constexpr int PGO_PROFILER_BITFIELD_START = 16;
    static constexpr int BOOL_BITFIELD_NUM = 1;
    static constexpr int BCSTUBSTATUS_BITFIELD_NUM = 2;
    static constexpr uint32_t RESERVE_STACK_SIZE = 128;
    using MarkStatusBits = BitField<MarkStatus, 0, CONCURRENT_MARKING_BITFIELD_NUM>;
    using SharedMarkStatusBits = BitField<SharedMarkStatus, 0, SHARED_CONCURRENT_MARKING_BITFIELD_NUM>;
    using CheckSafePointBit = BitField<bool, 0, BOOL_BITFIELD_NUM>;
    using VMNeedSuspensionBit = BitField<bool, CHECK_SAFEPOINT_BITFIELD_NUM, BOOL_BITFIELD_NUM>;
    using VMHasSuspendedBit = VMNeedSuspensionBit::NextFlag;
    using InstallMachineCodeBit = VMHasSuspendedBit::NextFlag;
    using PGOStatusBits = BitField<PGOProfilerStatus, PGO_PROFILER_BITFIELD_START, BOOL_BITFIELD_NUM>;
    using BCStubStatusBits = PGOStatusBits::NextField<BCStubStatus, BCSTUBSTATUS_BITFIELD_NUM>;
    using ThreadId = uint32_t;

    enum FrameDroppedState {
        StateFalse = 0,
        StateTrue,
        StatePending
    };

    explicit JSThread(EcmaVM *vm);
    // only used in jit thread
    explicit JSThread(EcmaVM *vm, ThreadType threadType);
    // only used in daemon thread
    explicit JSThread(ThreadType threadType);

    PUBLIC_API ~JSThread();

    EcmaVM *GetEcmaVM() const
    {
        return vm_;
    }

    static JSThread *Create(EcmaVM *vm);
    static JSThread *GetCurrent();

    int GetNestedLevel() const
    {
        return nestedLevel_;
    }

    void SetNestedLevel(int level)
    {
        nestedLevel_ = level;
    }

    void SetLastFp(JSTaggedType *fp)
    {
        glueData_.lastFp_ = fp;
    }

    const JSTaggedType *GetLastFp() const
    {
        return glueData_.lastFp_;
    }

    const JSTaggedType *GetCurrentSPFrame() const
    {
        return glueData_.currentFrame_;
    }

    void SetCurrentSPFrame(JSTaggedType *sp)
    {
        glueData_.currentFrame_ = sp;
    }

    const JSTaggedType *GetLastLeaveFrame() const
    {
        return glueData_.leaveFrame_;
    }

    void SetLastLeaveFrame(JSTaggedType *sp)
    {
        glueData_.leaveFrame_ = sp;
    }

    const JSTaggedType *GetCurrentFrame() const;

    void SetCurrentFrame(JSTaggedType *sp);

    const JSTaggedType *GetCurrentInterpretedFrame() const;

    bool DoStackOverflowCheck(const JSTaggedType *sp);

    bool DoStackLimitCheck();

    NativeAreaAllocator *GetNativeAreaAllocator() const
    {
        return nativeAreaAllocator_;
    }

    HeapRegionAllocator *GetHeapRegionAllocator() const
    {
        return heapRegionAllocator_;
    }

    void ReSetNewSpaceAllocationAddress(const uintptr_t *top, const uintptr_t* end)
    {
        glueData_.newSpaceAllocationTopAddress_ = top;
        glueData_.newSpaceAllocationEndAddress_ = end;
    }

    void ReSetSOldSpaceAllocationAddress(const uintptr_t *top, const uintptr_t* end)
    {
        glueData_.sOldSpaceAllocationTopAddress_ = top;
        glueData_.sOldSpaceAllocationEndAddress_ = end;
    }

    void ReSetSNonMovableSpaceAllocationAddress(const uintptr_t *top, const uintptr_t* end)
    {
        glueData_.sNonMovableSpaceAllocationTopAddress_ = top;
        glueData_.sNonMovableSpaceAllocationEndAddress_ = end;
    }

    uintptr_t GetUnsharedConstpools() const
    {
        return glueData_.unsharedConstpools_;
    }

    void SetUnsharedConstpools(uintptr_t unsharedConstpools)
    {
        glueData_.unsharedConstpools_ = unsharedConstpools;
    }

    void SetIsStartHeapSampling(bool isStart)
    {
        glueData_.isStartHeapSampling_ = isStart ? JSTaggedValue::True() : JSTaggedValue::False();
    }

    void SetIsTracing(bool isTracing)
    {
        glueData_.isTracing_ = isTracing;
    }

    void Iterate(const RootVisitor &visitor, const RootRangeVisitor &rangeVisitor,
        const RootBaseAndDerivedVisitor &derivedVisitor);

    void IterateJitCodeMap(const JitCodeMapVisitor &updater);

    void IterateHandleWithCheck(const RootVisitor &visitor, const RootRangeVisitor &rangeVisitor);

    uintptr_t* PUBLIC_API ExpandHandleStorage();
    void PUBLIC_API ShrinkHandleStorage(int prevIndex);
    void PUBLIC_API CheckJSTaggedType(JSTaggedType value) const;
    bool PUBLIC_API CpuProfilerCheckJSTaggedType(JSTaggedType value) const;

    void PUBLIC_API SetException(JSTaggedValue exception);

    JSTaggedValue GetException() const
    {
        return glueData_.exception_;
    }

    bool HasPendingException() const
    {
        return !glueData_.exception_.IsHole();
    }

    void ClearException();

    void SetGlobalObject(JSTaggedValue globalObject)
    {
        glueData_.globalObject_ = globalObject;
    }

    const GlobalEnv *GetGlobalEnv() const
    {
        return glueData_.glueGlobalEnv_;
    }

    const GlobalEnvConstants *GlobalConstants() const
    {
        return glueData_.globalConst_;
    }

    void SetGlobalConstants(const GlobalEnvConstants *constants)
    {
        glueData_.globalConst_ = const_cast<GlobalEnvConstants*>(constants);
    }

    const BuiltinEntries GetBuiltinEntries() const
    {
        return glueData_.builtinEntries_;
    }

    BuiltinEntries* GetBuiltinEntriesPointer()
    {
        return &glueData_.builtinEntries_;
    }

    const CMap<ElementsKind, std::pair<ConstantIndex, ConstantIndex>> &GetArrayHClassIndexMap() const
    {
        return arrayHClassIndexMap_;
    }

    const CMap<JSHClass *, GlobalIndex> &GetCtorHclassEntries() const
    {
        return ctorHclassEntries_;
    }

    void NotifyStableArrayElementsGuardians(JSHandle<JSObject> receiver, StableArrayChangeKind changeKind);

    bool IsStableArrayElementsGuardiansInvalid() const
    {
        return !glueData_.stableArrayElementsGuardians_;
    }

    void ResetGuardians();

    void SetInitialBuiltinHClass(
        BuiltinTypeId type, JSHClass *builtinHClass, JSHClass *instanceHClass,
                            JSHClass *prototypeHClass, JSHClass *prototypeOfPrototypeHClass = nullptr,
                            JSHClass *extraHClass = nullptr);

    void SetInitialBuiltinGlobalHClass(JSHClass *builtinHClass, GlobalIndex globalIndex);

    JSHClass *GetBuiltinHClass(BuiltinTypeId type) const;

    JSHClass *GetBuiltinInstanceHClass(BuiltinTypeId type) const;
    JSHClass *GetBuiltinExtraHClass(BuiltinTypeId type) const;
    JSHClass *GetArrayInstanceHClass(ElementsKind kind, bool isPrototype) const;

    PUBLIC_API JSHClass *GetBuiltinPrototypeHClass(BuiltinTypeId type) const;
    PUBLIC_API JSHClass *GetBuiltinPrototypeOfPrototypeHClass(BuiltinTypeId type) const;

    static size_t GetBuiltinHClassOffset(BuiltinTypeId, bool isArch32);

    static size_t GetBuiltinPrototypeHClassOffset(BuiltinTypeId, bool isArch32);

    const BuiltinHClassEntries &GetBuiltinHClassEntries() const
    {
        return glueData_.builtinHClassEntries_;
    }

    JSTaggedValue GetCurrentLexenv() const;
    JSTaggedValue GetCurrentFunction() const;

    void RegisterRTInterface(size_t id, Address addr)
    {
        ASSERT(id < kungfu::RuntimeStubCSigns::NUM_OF_STUBS);
        glueData_.rtStubEntries_.Set(id, addr);
    }

    Address GetRTInterface(size_t id) const
    {
        ASSERT(id < kungfu::RuntimeStubCSigns::NUM_OF_STUBS);
        return glueData_.rtStubEntries_.Get(id);
    }

    Address GetFastStubEntry(uint32_t id) const
    {
        return glueData_.coStubEntries_.Get(id);
    }

    void SetFastStubEntry(size_t id, Address entry)
    {
        glueData_.coStubEntries_.Set(id, entry);
    }

    Address GetBuiltinStubEntry(uint32_t id) const
    {
        return glueData_.builtinStubEntries_.Get(id);
    }

    void SetBuiltinStubEntry(size_t id, Address entry)
    {
        glueData_.builtinStubEntries_.Set(id, entry);
    }

    Address GetBCStubEntry(uint32_t id) const
    {
        return glueData_.bcStubEntries_.Get(id);
    }

    void SetBCStubEntry(size_t id, Address entry)
    {
        glueData_.bcStubEntries_.Set(id, entry);
    }

    Address GetBaselineStubEntry(uint32_t id) const
    {
        return glueData_.baselineStubEntries_.Get(id);
    }

    void SetBaselineStubEntry(size_t id, Address entry)
    {
        glueData_.baselineStubEntries_.Set(id, entry);
    }

    void SetBCDebugStubEntry(size_t id, Address entry)
    {
        glueData_.bcDebuggerStubEntries_.Set(id, entry);
    }

    Address *GetBytecodeHandler()
    {
        return glueData_.bcStubEntries_.GetAddr();
    }

    void PUBLIC_API CheckSwitchDebuggerBCStub();
    void CheckOrSwitchPGOStubs();
    void SwitchJitProfileStubs(bool isEnablePgo);

    ThreadId GetThreadId() const
    {
        return id_.load(std::memory_order_acquire);
    }

    void PostFork();

    static ThreadId GetCurrentThreadId()
    {
#if defined(ENABLE_FFRT_INTERFACES)
        JSThread::ThreadId id = ffrt_this_task_get_id();
        if (id != 0) {
            return id;
        } else {
            return os::thread::GetCurrentThreadId();
        }
#else
        return os::thread::GetCurrentThreadId();
#endif
    }

    void IterateWeakEcmaGlobalStorage(const WeakRootVisitor &visitor, GCKind gcKind = GCKind::LOCAL_GC);

    void UpdateJitCodeMapReference(const WeakRootVisitor &visitor);

    PUBLIC_API PropertiesCache *GetPropertiesCache() const;

    MarkStatus GetMarkStatus() const
    {
        return MarkStatusBits::Decode(glueData_.gcStateBitField_);
    }

    void SetMarkStatus(MarkStatus status)
    {
        MarkStatusBits::Set(status, &glueData_.gcStateBitField_);
    }

    bool IsConcurrentMarkingOrFinished() const
    {
        return !IsReadyToConcurrentMark();
    }

    bool IsReadyToConcurrentMark() const
    {
        auto status = MarkStatusBits::Decode(glueData_.gcStateBitField_);
        return status == MarkStatus::READY_TO_MARK;
    }

    bool IsMarking() const
    {
        auto status = MarkStatusBits::Decode(glueData_.gcStateBitField_);
        return status == MarkStatus::MARKING;
    }

    bool IsMarkFinished() const
    {
        auto status = MarkStatusBits::Decode(glueData_.gcStateBitField_);
        return status == MarkStatus::MARK_FINISHED;
    }

    SharedMarkStatus GetSharedMarkStatus() const
    {
        return SharedMarkStatusBits::Decode(glueData_.sharedGCStateBitField_);
    }

    void SetSharedMarkStatus(SharedMarkStatus status)
    {
        SharedMarkStatusBits::Set(status, &glueData_.sharedGCStateBitField_);
    }

    bool IsSharedConcurrentMarkingOrFinished() const
    {
        auto status = SharedMarkStatusBits::Decode(glueData_.sharedGCStateBitField_);
        return status == SharedMarkStatus::CONCURRENT_MARKING_OR_FINISHED;
    }

    bool IsReadyToSharedConcurrentMark() const
    {
        auto status = SharedMarkStatusBits::Decode(glueData_.sharedGCStateBitField_);
        return status == SharedMarkStatus::READY_TO_CONCURRENT_MARK;
    }

    void SetPGOProfilerEnable(bool enable)
    {
        PGOProfilerStatus status =
            enable ? PGOProfilerStatus::PGO_PROFILER_ENABLE : PGOProfilerStatus::PGO_PROFILER_DISABLE;
        SetInterruptValue<PGOStatusBits>(status);
    }

    bool IsPGOProfilerEnable() const
    {
        auto status = PGOStatusBits::Decode(glueData_.interruptVector_);
        return status == PGOProfilerStatus::PGO_PROFILER_ENABLE;
    }

    void SetBCStubStatus(BCStubStatus status)
    {
        SetInterruptValue<BCStubStatusBits>(status);
    }

    BCStubStatus GetBCStubStatus() const
    {
        return BCStubStatusBits::Decode(glueData_.interruptVector_);
    }

    bool CheckSafepoint();

    void CheckAndPassActiveBarrier();

    bool PassSuspendBarrier();

    void SetGetStackSignal(bool isParseStack)
    {
        getStackSignal_ = isParseStack;
    }

    bool GetStackSignal() const
    {
        return getStackSignal_;
    }

    void SetNeedProfiling(bool needProfiling)
    {
        needProfiling_.store(needProfiling);
    }

    void SetIsProfiling(bool isProfiling)
    {
        isProfiling_ = isProfiling;
    }

    bool GetIsProfiling() const
    {
        return isProfiling_;
    }

    void SetGcState(bool gcState)
    {
        gcState_ = gcState;
    }

    bool GetGcState() const
    {
        return gcState_;
    }

    void SetRuntimeState(bool runtimeState)
    {
        runtimeState_ = runtimeState;
    }

    bool GetRuntimeState() const
    {
        return runtimeState_;
    }

    bool SetMainThread()
    {
        return isMainThread_ = true;
    }

    bool IsMainThreadFast() const
    {
        return isMainThread_;
    }

    void SetCpuProfileName(std::string &profileName)
    {
        profileName_ = profileName;
    }

    void EnableAsmInterpreter()
    {
        isAsmInterpreter_ = true;
    }

    bool IsAsmInterpreter() const
    {
        return isAsmInterpreter_;
    }

    VmThreadControl *GetVmThreadControl() const
    {
        return vmThreadControl_;
    }

    void SetEnableStackSourceFile(bool value)
    {
        enableStackSourceFile_ = value;
    }

    bool GetEnableStackSourceFile() const
    {
        return enableStackSourceFile_;
    }

    void SetEnableLazyBuiltins(bool value)
    {
        enableLazyBuiltins_ = value;
    }

    bool GetEnableLazyBuiltins() const
    {
        return enableLazyBuiltins_;
    }

    void SetReadyForGCIterating(bool flag)
    {
        readyForGCIterating_ = flag;
    }

    bool ReadyForGCIterating() const
    {
        return readyForGCIterating_;
    }

    static constexpr size_t GetGlueDataOffset()
    {
        return MEMBER_OFFSET(JSThread, glueData_);
    }

    uintptr_t GetGlueAddr() const
    {
        return reinterpret_cast<uintptr_t>(this) + GetGlueDataOffset();
    }

    static JSThread *GlueToJSThread(uintptr_t glue)
    {
        // very careful to modify here
        return reinterpret_cast<JSThread *>(glue - GetGlueDataOffset());
    }

    void SetCheckSafePointStatus()
    {
        ASSERT(static_cast<uint8_t>(glueData_.interruptVector_ & 0xFF) <= 1);
        SetInterruptValue<CheckSafePointBit>(true);
    }

    void ResetCheckSafePointStatus()
    {
        ASSERT(static_cast<uint8_t>(glueData_.interruptVector_ & 0xFF) <= 1);
        SetInterruptValue<CheckSafePointBit>(false);
    }

    void SetVMNeedSuspension(bool flag)
    {
        SetInterruptValue<VMNeedSuspensionBit>(flag);
    }

    bool VMNeedSuspension()
    {
        return VMNeedSuspensionBit::Decode(glueData_.interruptVector_);
    }

    void SetVMSuspended(bool flag)
    {
        SetInterruptValue<VMHasSuspendedBit>(flag);
    }

    bool IsVMSuspended()
    {
        return VMHasSuspendedBit::Decode(glueData_.interruptVector_);
    }

    bool HasTerminationRequest() const
    {
        return needTermination_;
    }

    void SetTerminationRequest(bool flag)
    {
        needTermination_ = flag;
    }

    void SetVMTerminated(bool flag)
    {
        hasTerminated_ = flag;
    }

    bool HasTerminated() const
    {
        return hasTerminated_;
    }

    void TerminateExecution();

    void SetInstallMachineCode(bool flag)
    {
        SetInterruptValue<InstallMachineCodeBit>(flag);
    }

    bool HasInstallMachineCode() const
    {
        return InstallMachineCodeBit::Decode(glueData_.interruptVector_);
    }

    static uintptr_t GetCurrentStackPosition()
    {
        return reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    }

    bool IsLegalAsmSp(uintptr_t sp) const;

    bool IsLegalThreadSp(uintptr_t sp) const;

    bool IsLegalSp(uintptr_t sp) const;

    void SetCheckAndCallEnterState(bool state)
    {
        finalizationCheckState_ = state;
    }

    bool GetCheckAndCallEnterState() const
    {
        return finalizationCheckState_;
    }

    uint64_t GetStackStart() const
    {
        return glueData_.stackStart_;
    }

    uint64_t GetStackLimit() const
    {
        return glueData_.stackLimit_;
    }

    GlobalEnv *GetGlueGlobalEnv()
    {
        return glueData_.glueGlobalEnv_;
    }

    void SetGlueGlobalEnv(GlobalEnv *global)
    {
        ASSERT(global != nullptr);
        glueData_.glueGlobalEnv_ = global;
    }

    inline uintptr_t NewGlobalHandle(JSTaggedType value)
    {
        return newGlobalHandle_(value);
    }

    inline void DisposeGlobalHandle(uintptr_t nodeAddr)
    {
        disposeGlobalHandle_(nodeAddr);
    }

    inline uintptr_t SetWeak(uintptr_t nodeAddr, void *ref = nullptr, WeakClearCallback freeGlobalCallBack = nullptr,
                             WeakClearCallback nativeFinalizeCallBack = nullptr)
    {
        return setWeak_(nodeAddr, ref, freeGlobalCallBack, nativeFinalizeCallBack);
    }

    inline uintptr_t ClearWeak(uintptr_t nodeAddr)
    {
        return clearWeak_(nodeAddr);
    }

    inline bool IsWeak(uintptr_t addr) const
    {
        return isWeak_(addr);
    }

    void EnableCrossThreadExecution()
    {
        glueData_.allowCrossThreadExecution_ = true;
    }

    bool IsCrossThreadExecutionEnable() const
    {
        return glueData_.allowCrossThreadExecution_;
    }

    bool IsFrameDropped()
    {
        return glueData_.isFrameDropped_;
    }

    void SetFrameDroppedState()
    {
        glueData_.isFrameDropped_ = true;
    }

    void ResetFrameDroppedState()
    {
        glueData_.isFrameDropped_ = false;
    }

    bool IsEntryFrameDroppedTrue()
    {
        return glueData_.entryFrameDroppedState_ == FrameDroppedState::StateTrue;
    }

    bool IsEntryFrameDroppedPending()
    {
        return glueData_.entryFrameDroppedState_ == FrameDroppedState::StatePending;
    }

    void SetEntryFrameDroppedState()
    {
        glueData_.entryFrameDroppedState_ = FrameDroppedState::StateTrue;
    }

    void ResetEntryFrameDroppedState()
    {
        glueData_.entryFrameDroppedState_ = FrameDroppedState::StateFalse;
    }

    void PendingEntryFrameDroppedState()
    {
        glueData_.entryFrameDroppedState_ = FrameDroppedState::StatePending;
    }

    bool IsDebugMode()
    {
        return glueData_.isDebugMode_;
    }

    void SetDebugModeState()
    {
        glueData_.isDebugMode_ = true;
    }

    void ResetDebugModeState()
    {
        glueData_.isDebugMode_ = false;
    }

    template<typename T, typename V>
    void SetInterruptValue(V value)
    {
        volatile auto interruptValue =
            reinterpret_cast<volatile std::atomic<uint64_t> *>(&glueData_.interruptVector_);
        uint64_t oldValue = interruptValue->load(std::memory_order_relaxed);
        auto newValue = oldValue;
        do {
            newValue = oldValue;
            T::Set(value, &newValue);
        } while (!std::atomic_compare_exchange_strong_explicit(interruptValue, &oldValue, newValue,
                                                               std::memory_order_release,
                                                               std::memory_order_relaxed));
    }

    void InvokeWeakNodeFreeGlobalCallBack();
    void InvokeSharedNativePointerCallbacks();
    void InvokeWeakNodeNativeFinalizeCallback();
    bool IsStartGlobalLeakCheck() const;
    bool EnableGlobalObjectLeakCheck() const;
    bool EnableGlobalPrimitiveLeakCheck() const;
    void WriteToStackTraceFd(std::ostringstream &buffer) const;
    void SetStackTraceFd(int32_t fd);
    void CloseStackTraceFd();
    uint32_t IncreaseGlobalNumberCount()
    {
        return ++globalNumberCount_;
    }

    void SetPropertiesGrowStep(uint32_t step)
    {
        glueData_.propertiesGrowStep_ = step;
    }

    uint32_t GetPropertiesGrowStep() const
    {
        return glueData_.propertiesGrowStep_;
    }

    void SetRandomStatePtr(uint64_t *ptr)
    {
        glueData_.randomStatePtr_ = reinterpret_cast<uintptr_t>(ptr);
    }

    void SetTaskInfo(uintptr_t taskInfo)
    {
        glueData_.taskInfo_ = taskInfo;
    }
    
    uintptr_t GetTaskInfo() const
    {
        return glueData_.taskInfo_;
    }

    void SetJitCodeMap(JSTaggedType exception,  MachineCode* machineCode, std::string &methodName, uintptr_t offset)
    {
        auto it = jitCodeMaps_.find(exception);
        if (it != jitCodeMaps_.end()) {
            it->second->push_back(std::make_tuple(machineCode, methodName, offset));
        } else {
            JitCodeVector *jitCode = new JitCodeVector {std::make_tuple(machineCode, methodName, offset)};
            jitCodeMaps_.emplace(exception, jitCode);
        }
    }

    std::map<JSTaggedType, JitCodeVector*> &GetJitCodeMaps()
    {
        return jitCodeMaps_;
    }

    struct GlueData : public base::AlignedStruct<JSTaggedValue::TaggedTypeSize(),
                                                 BCStubEntries,
                                                 JSTaggedValue,
                                                 JSTaggedValue,
                                                 base::AlignedBool,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 RTStubEntries,
                                                 COStubEntries,
                                                 BuiltinStubEntries,
                                                 BuiltinHClassEntries,
                                                 BCDebuggerStubEntries,
                                                 BaselineStubEntries,
                                                 base::AlignedUint64,
                                                 base::AlignedUint64,
                                                 base::AlignedPointer,
                                                 base::AlignedUint64,
                                                 base::AlignedUint64,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 base::AlignedUint64,
                                                 base::AlignedUint64,
                                                 JSTaggedValue,
                                                 base::AlignedBool,
                                                 base::AlignedBool,
                                                 base::AlignedUint32,
                                                 JSTaggedValue,
                                                 base::AlignedPointer,
                                                 BuiltinEntries,
                                                 base::AlignedBool,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 base::AlignedPointer,
                                                 base::AlignedUint32,
                                                 base::AlignedBool> {
        enum class Index : size_t {
            BcStubEntriesIndex = 0,
            ExceptionIndex,
            GlobalObjIndex,
            StableArrayElementsGuardiansIndex,
            CurrentFrameIndex,
            LeaveFrameIndex,
            LastFpIndex,
            NewSpaceAllocationTopAddressIndex,
            NewSpaceAllocationEndAddressIndex,
            SOldSpaceAllocationTopAddressIndex,
            SOldSpaceAllocationEndAddressIndex,
            SNonMovableSpaceAllocationTopAddressIndex,
            SNonMovableSpaceAllocationEndAddressIndex,
            RTStubEntriesIndex,
            COStubEntriesIndex,
            BuiltinsStubEntriesIndex,
            BuiltinHClassEntriesIndex,
            BcDebuggerStubEntriesIndex,
            BaselineStubEntriesIndex,
            GCStateBitFieldIndex,
            SharedGCStateBitFieldIndex,
            FrameBaseIndex,
            StackStartIndex,
            StackLimitIndex,
            GlueGlobalEnvIndex,
            GlobalConstIndex,
            AllowCrossThreadExecutionIndex,
            InterruptVectorIndex,
            IsStartHeapSamplingIndex,
            IsDebugModeIndex,
            IsFrameDroppedIndex,
            PropertiesGrowStepIndex,
            EntryFrameDroppedStateIndex,
            CurrentContextIndex,
            BuiltinEntriesIndex,
            IsTracingIndex,
            UnsharedConstpoolsIndex,
            RandomStatePtrIndex,
            StateAndFlagsIndex,
            TaskInfoIndex,
            IsEnableElementsKindIndex,
            NumOfMembers
        };
        static_assert(static_cast<size_t>(Index::NumOfMembers) == NumOfTypes);

        static size_t GetExceptionOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::ExceptionIndex)>(isArch32);
        }

        static size_t GetGlobalObjOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::GlobalObjIndex)>(isArch32);
        }

        static size_t GetStableArrayElementsGuardiansOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::StableArrayElementsGuardiansIndex)>(isArch32);
        }

        static size_t GetGlobalConstOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::GlobalConstIndex)>(isArch32);
        }

        static size_t GetGCStateBitFieldOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::GCStateBitFieldIndex)>(isArch32);
        }

        static size_t GetSharedGCStateBitFieldOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::SharedGCStateBitFieldIndex)>(isArch32);
        }

        static size_t GetCurrentFrameOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::CurrentFrameIndex)>(isArch32);
        }

        static size_t GetLeaveFrameOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::LeaveFrameIndex)>(isArch32);
        }

        static size_t GetLastFpOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::LastFpIndex)>(isArch32);
        }

        static size_t GetNewSpaceAllocationTopAddressOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::NewSpaceAllocationTopAddressIndex)>(isArch32);
        }

        static size_t GetNewSpaceAllocationEndAddressOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::NewSpaceAllocationEndAddressIndex)>(isArch32);
        }

        static size_t GetSOldSpaceAllocationTopAddressOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::SOldSpaceAllocationTopAddressIndex)>(isArch32);
        }

        static size_t GetSOldSpaceAllocationEndAddressOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::SOldSpaceAllocationEndAddressIndex)>(isArch32);
        }

        static size_t GetSNonMovableSpaceAllocationTopAddressOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::SNonMovableSpaceAllocationTopAddressIndex)>(isArch32);
        }

        static size_t GetSNonMovableSpaceAllocationEndAddressOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::SNonMovableSpaceAllocationEndAddressIndex)>(isArch32);
        }

        static size_t GetBCStubEntriesOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::BcStubEntriesIndex)>(isArch32);
        }

        static size_t GetRTStubEntriesOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::RTStubEntriesIndex)>(isArch32);
        }

        static size_t GetCOStubEntriesOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::COStubEntriesIndex)>(isArch32);
        }

        static size_t GetBaselineStubEntriesOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::BaselineStubEntriesIndex)>(isArch32);
        }

        static size_t GetBuiltinsStubEntriesOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::BuiltinsStubEntriesIndex)>(isArch32);
        }

        static size_t GetBuiltinHClassEntriesOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::BuiltinHClassEntriesIndex)>(isArch32);
        }

        static size_t GetBuiltinHClassOffset(BuiltinTypeId type, bool isArch32)
        {
            return GetBuiltinHClassEntriesOffset(isArch32) + BuiltinHClassEntries::GetBuiltinHClassOffset(type);
        }

        static size_t GetBuiltinInstanceHClassOffset(BuiltinTypeId type, bool isArch32)
        {
            return GetBuiltinHClassEntriesOffset(isArch32) + BuiltinHClassEntries::GetInstanceHClassOffset(type);
        }

        static size_t GetBuiltinPrototypeHClassOffset(BuiltinTypeId type, bool isArch32)
        {
            return GetBuiltinHClassEntriesOffset(isArch32) + BuiltinHClassEntries::GetPrototypeHClassOffset(type);
        }

        static size_t GetBuiltinPrototypeOfPrototypeHClassOffset(BuiltinTypeId type, bool isArch32)
        {
            return GetBuiltinHClassEntriesOffset(isArch32) +
                   BuiltinHClassEntries::GetPrototypeOfPrototypeHClassOffset(type);
        }

        static size_t GetBuiltinExtraHClassOffset(BuiltinTypeId type, bool isArch32)
        {
            return GetBuiltinHClassEntriesOffset(isArch32) + BuiltinHClassEntries::GetExtraHClassOffset(type);
        }

        static size_t GetBCDebuggerStubEntriesOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::BcDebuggerStubEntriesIndex)>(isArch32);
        }

        static size_t GetFrameBaseOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::FrameBaseIndex)>(isArch32);
        }

        static size_t GetStackLimitOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::StackLimitIndex)>(isArch32);
        }

        static size_t GetGlueGlobalEnvOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::GlueGlobalEnvIndex)>(isArch32);
        }

        static size_t GetAllowCrossThreadExecutionOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::AllowCrossThreadExecutionIndex)>(isArch32);
        }

        static size_t GetInterruptVectorOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::InterruptVectorIndex)>(isArch32);
        }

        static size_t GetIsStartHeapSamplingOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::IsStartHeapSamplingIndex)>(isArch32);
        }

        static size_t GetIsDebugModeOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::IsDebugModeIndex)>(isArch32);
        }

        static size_t GetIsFrameDroppedOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::IsFrameDroppedIndex)>(isArch32);
        }

        static size_t GetPropertiesGrowStepOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::PropertiesGrowStepIndex)>(isArch32);
        }

        static size_t GetEntryFrameDroppedStateOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::EntryFrameDroppedStateIndex)>(isArch32);
        }

        static size_t GetCurrentContextOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::CurrentContextIndex)>(isArch32);
        }

        static size_t GetBuiltinEntriesOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::BuiltinEntriesIndex)>(isArch32);
        }

        static size_t GetIsTracingOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::IsTracingIndex)>(isArch32);
        }

        static size_t GetUnSharedConstpoolsOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::UnsharedConstpoolsIndex)>(isArch32);
        }

        static size_t GetStateAndFlagsOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::StateAndFlagsIndex)>(isArch32);
        }

        static size_t GetRandomStatePtrOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::RandomStatePtrIndex)>(isArch32);
        }

        static size_t GetTaskInfoOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::TaskInfoIndex)>(isArch32);
        }

        static size_t GetIsEnableElementsKindOffset(bool isArch32)
        {
            return GetOffset<static_cast<size_t>(Index::IsEnableElementsKindIndex)>(isArch32);
        }

        alignas(EAS) BCStubEntries bcStubEntries_ {};
        alignas(EAS) JSTaggedValue exception_ {JSTaggedValue::Hole()};
        alignas(EAS) JSTaggedValue globalObject_ {JSTaggedValue::Hole()};
        alignas(EAS) bool stableArrayElementsGuardians_ {true};
        alignas(EAS) JSTaggedType *currentFrame_ {nullptr};
        alignas(EAS) JSTaggedType *leaveFrame_ {nullptr};
        alignas(EAS) JSTaggedType *lastFp_ {nullptr};
        alignas(EAS) const uintptr_t *newSpaceAllocationTopAddress_ {nullptr};
        alignas(EAS) const uintptr_t *newSpaceAllocationEndAddress_ {nullptr};
        alignas(EAS) const uintptr_t *sOldSpaceAllocationTopAddress_ {nullptr};
        alignas(EAS) const uintptr_t *sOldSpaceAllocationEndAddress_ {nullptr};
        alignas(EAS) const uintptr_t *sNonMovableSpaceAllocationTopAddress_ {nullptr};
        alignas(EAS) const uintptr_t *sNonMovableSpaceAllocationEndAddress_ {nullptr};
        alignas(EAS) RTStubEntries rtStubEntries_ {};
        alignas(EAS) COStubEntries coStubEntries_ {};
        alignas(EAS) BuiltinStubEntries builtinStubEntries_ {};
        alignas(EAS) BuiltinHClassEntries builtinHClassEntries_ {};
        alignas(EAS) BCDebuggerStubEntries bcDebuggerStubEntries_ {};
        alignas(EAS) BaselineStubEntries baselineStubEntries_ {};
        alignas(EAS) volatile uint64_t gcStateBitField_ {0ULL};
        alignas(EAS) volatile uint64_t sharedGCStateBitField_ {0ULL};
        alignas(EAS) JSTaggedType *frameBase_ {nullptr};
        alignas(EAS) uint64_t stackStart_ {0};
        alignas(EAS) uint64_t stackLimit_ {0};
        alignas(EAS) GlobalEnv *glueGlobalEnv_ {nullptr};
        alignas(EAS) GlobalEnvConstants *globalConst_ {nullptr};
        alignas(EAS) bool allowCrossThreadExecution_ {false};
        alignas(EAS) volatile uint64_t interruptVector_ {0};
        alignas(EAS) JSTaggedValue isStartHeapSampling_ {JSTaggedValue::False()};
        alignas(EAS) bool isDebugMode_ {false};
        alignas(EAS) bool isFrameDropped_ {false};
        alignas(EAS) uint32_t propertiesGrowStep_ {JSObjectResizingStrategy::PROPERTIES_GROW_SIZE};
        alignas(EAS) uint64_t entryFrameDroppedState_ {FrameDroppedState::StateFalse};
        alignas(EAS) EcmaContext *currentContext_ {nullptr};
        alignas(EAS) BuiltinEntries builtinEntries_ {};
        alignas(EAS) bool isTracing_ {false};
        alignas(EAS) uintptr_t unsharedConstpools_ {0};
        alignas(EAS) uintptr_t randomStatePtr_ {0};
        alignas(EAS) ThreadStateAndFlags stateAndFlags_ {};
        alignas(EAS) uintptr_t taskInfo_ {0};
        alignas(EAS) bool isEnableElementsKind_ {false};
    };
    STATIC_ASSERT_EQ_ARCH(sizeof(GlueData), GlueData::SizeArch32, GlueData::SizeArch64);

    void PushContext(EcmaContext *context);
    void PopContext();

    EcmaContext *GetCurrentEcmaContext() const
    {
        return glueData_.currentContext_;
    }

    JSTaggedValue GetSingleCharTable() const
    {
        ASSERT(glueData_.globalConst_->GetSingleCharTable() != JSTaggedValue::Hole());
        return glueData_.globalConst_->GetSingleCharTable();
    }

    void SwitchCurrentContext(EcmaContext *currentContext, bool isInIterate = false);

    CVector<EcmaContext *> GetEcmaContexts()
    {
        return contexts_;
    }

    bool IsPropertyCacheCleared() const;

    bool EraseContext(EcmaContext *context);

    const GlobalEnvConstants *GetFirstGlobalConst() const;
    bool IsAllContextsInitialized() const;
    bool IsReadyToUpdateDetector() const;
    Area *GetOrCreateRegExpCache();

    void InitializeBuiltinObject(const std::string& key);
    void InitializeBuiltinObject();

    void SetFullMarkRequest()
    {
        fullMarkRequest_ = true;
    }

    inline bool IsThreadSafe() const
    {
        return IsMainThread() || HasSuspendRequest();
    }

    bool IsSuspended() const
    {
        bool f = ReadFlag(ThreadFlag::SUSPEND_REQUEST);
        bool s = (GetState() != ThreadState::RUNNING);
        return f && s;
    }

    inline bool HasSuspendRequest() const
    {
        return ReadFlag(ThreadFlag::SUSPEND_REQUEST);
    }

    void CheckSafepointIfSuspended()
    {
        if (HasSuspendRequest()) {
            WaitSuspension();
        }
    }

    bool IsInSuspendedState() const
    {
        return GetState() == ThreadState::IS_SUSPENDED;
    }

    bool IsInRunningState() const
    {
        return GetState() == ThreadState::RUNNING;
    }

    bool IsInRunningStateOrProfiling() const;

    ThreadState GetState() const
    {
        uint32_t stateAndFlags = glueData_.stateAndFlags_.asAtomicInt.load(std::memory_order_acquire);
        return static_cast<enum ThreadState>(stateAndFlags >> THREAD_STATE_OFFSET);
    }
    void PUBLIC_API UpdateState(ThreadState newState);
    void SuspendThread(bool internalSuspend, SuspendBarrier* barrier = nullptr);
    void ResumeThread(bool internalSuspend);
    void WaitSuspension();
    static bool IsMainThread();
    PUBLIC_API void ManagedCodeBegin();
    PUBLIC_API void ManagedCodeEnd();
#ifndef NDEBUG
    bool IsInManagedState() const;
    MutatorLock::MutatorLockState GetMutatorLockState() const;
    void SetMutatorLockState(MutatorLock::MutatorLockState newState);
#endif
    void SetWeakFinalizeTaskCallback(const WeakFinalizeTaskCallback &callback)
    {
        finalizeTaskCallback_ = callback;
    }

    uint64_t GetJobId()
    {
        if (jobId_ == UINT64_MAX) {
            jobId_ = 0;
        }
        return ++jobId_;
    }

    void SetAsyncCleanTaskCallback(const NativePointerTaskCallback &callback)
    {
        asyncCleanTaskCb_ = callback;
    }

    NativePointerTaskCallback GetAsyncCleanTaskCallback() const
    {
        return asyncCleanTaskCb_;
    }

    static void RegisterThread(JSThread *jsThread);

    static void UnregisterThread(JSThread *jsThread);

    bool IsJSThread() const
    {
        return threadType_ == ThreadType::JS_THREAD;
    }

    bool IsJitThread() const
    {
        return threadType_ == ThreadType::JIT_THREAD;
    }

    bool IsDaemonThread() const
    {
        return threadType_ == ThreadType::DAEMON_THREAD;
    }

    // Daemon_Thread and JS_Thread have some difference in transition, for example, when transition to running,
    // JS_Thread may take some local_gc actions, but Daemon_Thread do not need.
    void TransferDaemonThreadToRunning();

    RecursiveMutex *GetJitLock()
    {
        return &jitMutex_;
    }

    RecursiveMutex &GetProfileTypeAccessorLock()
    {
        return profileTypeAccessorLockMutex_;
    }

    void SetMachineCodeLowMemory(bool isLow)
    {
        machineCodeLowMemory_ = isLow;
    }

    bool IsMachineCodeLowMemory()
    {
        return machineCodeLowMemory_;
    }

    void *GetEnv() const
    {
        return env_;
    }

    void SetEnv(void *env)
    {
        env_ = env;
    }

    void SetIsInConcurrentScope(bool flag)
    {
        isInConcurrentScope_ = flag;
    }

    bool IsInConcurrentScope()
    {
        return isInConcurrentScope_;
    }

    void EnableEdenGCBarriers()
    {
        auto setValueStub = GetFastStubEntry(kungfu::CommonStubCSigns::SetValueWithEdenBarrier);
        SetFastStubEntry(kungfu::CommonStubCSigns::SetValueWithBarrier, setValueStub);
        auto markStub = GetRTInterface(kungfu::RuntimeStubCSigns::ID_MarkingBarrierWithEden);
        RegisterRTInterface(kungfu::RuntimeStubCSigns::ID_MarkingBarrier, markStub);
        auto setNotShareValueStub = GetFastStubEntry(kungfu::CommonStubCSigns::SetNonSValueWithEdenBarrier);
        SetFastStubEntry(kungfu::CommonStubCSigns::SetNonSValueWithBarrier, setNotShareValueStub);
    }

#ifndef NDEBUG
    inline void LaunchSuspendAll()
    {
        launchedSuspendAll_ = true;
    }

    inline bool HasLaunchedSuspendAll() const
    {
        return launchedSuspendAll_;
    }

    inline void CompleteSuspendAll()
    {
        launchedSuspendAll_ = false;
    }
#endif

protected:
    void SetThreadId()
    {
        id_.store(JSThread::GetCurrentThreadId(), std::memory_order_release);
    }

    // When call EcmaVM::PreFork(), the std::thread for Daemon_Thread is finished, but the Daemon_Thread instance
    // is still alive, and need to reset ThreadId to 0.
    void ResetThreadId()
    {
        id_.store(0, std::memory_order_release);
    }
private:
    NO_COPY_SEMANTIC(JSThread);
    NO_MOVE_SEMANTIC(JSThread);
    void SetGlobalConst(GlobalEnvConstants *globalConst)
    {
        glueData_.globalConst_ = globalConst;
    }
    void SetCurrentEcmaContext(EcmaContext *context)
    {
        glueData_.currentContext_ = context;
    }

    void SetArrayHClassIndexMap(const CMap<ElementsKind, std::pair<ConstantIndex, ConstantIndex>> &map)
    {
        arrayHClassIndexMap_ = map;
    }

    void TransferFromRunningToSuspended(ThreadState newState);

    void TransferToRunning();

    inline void StoreState(ThreadState newState);

    void StoreRunningState(ThreadState newState);

    void StoreSuspendedState(ThreadState newState);

    bool ReadFlag(ThreadFlag flag) const
    {
        uint32_t stateAndFlags = glueData_.stateAndFlags_.asAtomicInt.load(std::memory_order_acquire);
        uint16_t flags = (stateAndFlags & THREAD_FLAGS_MASK);
        return (flags & static_cast<uint16_t>(flag)) != 0;
    }

    void SetFlag(ThreadFlag flag)
    {
        glueData_.stateAndFlags_.asAtomicInt.fetch_or(flag, std::memory_order_seq_cst);
    }

    void ClearFlag(ThreadFlag flag)
    {
        glueData_.stateAndFlags_.asAtomicInt.fetch_and(UINT32_MAX ^ flag, std::memory_order_seq_cst);
    }

    void DumpStack() DUMP_API_ATTR;

    static size_t GetAsmStackLimit();

    static constexpr size_t DEFAULT_MAX_SYSTEM_STACK_SIZE = 8_MB;

    GlueData glueData_;
    std::atomic<ThreadId> id_ {0};
    EcmaVM *vm_ {nullptr};
    void *env_ {nullptr};
    Area *regExpCache_ {nullptr};

    // MM: handles, global-handles, and aot-stubs.
    int nestedLevel_ = 0;
    NativeAreaAllocator *nativeAreaAllocator_ {nullptr};
    HeapRegionAllocator *heapRegionAllocator_ {nullptr};
    bool runningNativeFinalizeCallbacks_ {false};
    std::vector<std::pair<WeakClearCallback, void *>> weakNodeFreeGlobalCallbacks_ {};
    std::vector<std::pair<WeakClearCallback, void *>> weakNodeNativeFinalizeCallbacks_ {};

    EcmaGlobalStorage<Node> *globalStorage_ {nullptr};
    EcmaGlobalStorage<DebugNode> *globalDebugStorage_ {nullptr};
    int32_t stackTraceFd_ {-1};

    std::function<uintptr_t(JSTaggedType value)> newGlobalHandle_;
    std::function<void(uintptr_t nodeAddr)> disposeGlobalHandle_;
    std::function<uintptr_t(uintptr_t nodeAddr, void *ref, WeakClearCallback freeGlobalCallBack_,
         WeakClearCallback nativeFinalizeCallBack)> setWeak_;
    std::function<uintptr_t(uintptr_t nodeAddr)> clearWeak_;
    std::function<bool(uintptr_t addr)> isWeak_;
    NativePointerTaskCallback asyncCleanTaskCb_ {nullptr};
    WeakFinalizeTaskCallback finalizeTaskCallback_ {nullptr};
    uint32_t globalNumberCount_ {0};

    // Run-time state
    bool getStackSignal_ {false};
    bool runtimeState_ {false};
    bool isAsmInterpreter_ {false};
    VmThreadControl *vmThreadControl_ {nullptr};
    bool enableStackSourceFile_ {true};
    bool enableLazyBuiltins_ {false};
    bool readyForGCIterating_ {false};
    // CpuProfiler
    bool isProfiling_ {false};
    bool gcState_ {false};
    std::atomic_bool needProfiling_ {false};
    std::string profileName_ {""};

    bool finalizationCheckState_ {false};
    // Shared heap
    bool isMainThread_ {false};
    bool fullMarkRequest_ {false};

    // { ElementsKind, (hclass, hclassWithProto) }
    CMap<ElementsKind, std::pair<ConstantIndex, ConstantIndex>> arrayHClassIndexMap_;
    CMap<JSHClass *, GlobalIndex> ctorHclassEntries_;

    CVector<EcmaContext *> contexts_;
    EcmaContext *currentContext_ {nullptr};

    Mutex suspendLock_;
    int32_t suspendCount_ {0};
    ConditionVariable suspendCondVar_;
    SuspendBarrier *suspendBarrier_ {nullptr};

    uint64_t jobId_ {0};

    ThreadType threadType_ {ThreadType::JS_THREAD};
    RecursiveMutex jitMutex_;
    bool machineCodeLowMemory_ {false};
    RecursiveMutex profileTypeAccessorLockMutex_;

#ifndef NDEBUG
    MutatorLock::MutatorLockState mutatorLockState_ = MutatorLock::MutatorLockState::UNLOCKED;
    std::atomic<bool> launchedSuspendAll_ {false};
#endif
    // Collect a map from JsError to MachineCode objects, JsError objects with stack frame generated by jit in the map.
    // It will be used to keep MachineCode objects alive (for dump) before JsError object be free.
    std::map<JSTaggedType, JitCodeVector*> jitCodeMaps_;

    std::atomic<bool> needTermination_ {false};
    std::atomic<bool> hasTerminated_ {false};

    bool isInConcurrentScope_ {false};

    friend class GlobalHandleCollection;
    friend class EcmaVM;
    friend class EcmaContext;
    friend class JitVM;
};
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_JS_THREAD_H
