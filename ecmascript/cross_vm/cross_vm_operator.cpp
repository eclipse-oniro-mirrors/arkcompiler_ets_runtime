/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

#include "ecmascript/checkpoint/thread_state_transition.h"
#include "ecmascript/cross_vm/cross_vm_operator.h"

#include "class_data_accessor-inl.h"
#include "debug_helpers.h"
#include "signal_safe_debug_helpers.h"
#include "ecmascript/compiler/aot_file/aot_file_manager.h"
#include "ecmascript/cross_vm/unified_gc/unified_gc_marker.h"
#include "ecmascript/cross_vm/heap_hybrid-inl.h"
#include "ecmascript/cross_vm/unified_gc/unified_gc.h"
#include "ecmascript/dfx/stackinfo/js_stackgetter.h"
#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
#include "ecmascript/dfx/hprof/dynamic_dump.h"
#endif
#include "ecmascript/ecma_string.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/interpreter/frame_handler.h"
#include "ecmascript/js_file_path.h"
#include "ecmascript/js_function.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/mem/heap-inl.h"
#include "ecmascript/dfx/stackinfo/js_stackinfo.h"

namespace panda::ecmascript {

CrossVMOperator::CrossVMOperator(EcmaVM *vm) : vm_(vm)
{
    ecmaVMInterface_ = std::make_unique<EcmaVMInterfaceImpl>(vm);
}

/*static*/
void CrossVMOperator::DoHandshake(EcmaVM *vm, void *stsIface, void **ecmaIface)
{
    if (g_isEnableCMCGC) {
        auto vmOperator = vm->GetCrossVMOperator();
        *ecmaIface = vmOperator->ecmaVMInterface_.get();
        vmOperator->stsVMInterface_ = static_cast<arkplatform::STSVMInterface *>(stsIface);
        Runtime::GetInstance()->SetSTSVMInterface(vmOperator->stsVMInterface_);
        return;
    }
    auto vmOperator = vm->GetCrossVMOperator();
    *ecmaIface = vmOperator->ecmaVMInterface_.get();
    vmOperator->stsVMInterface_ = static_cast<arkplatform::STSVMInterface *>(stsIface);
    Runtime::GetInstance()->SetSTSVMInterface(vmOperator->stsVMInterface_);
    UnifiedGC *unifiedGC = SharedHeap::GetInstance()->GetUnifiedGC();
    if (unifiedGC->GetSTSVMInterface() == nullptr) {
        unifiedGC->SetSTSVMInterface(vmOperator->stsVMInterface_);
    }
}

void CrossVMOperator::MarkFromObject(JSTaggedType value, std::function<void(uintptr_t)> &visitor)
{
    JSTaggedValue taggedValue(value);
    if (!taggedValue.IsHeapObject()) {
        return;
    }
    TaggedObject *object = taggedValue.GetHeapObject();
    visitor(reinterpret_cast<uintptr_t>(object));  // This is only mark, so could pass a stack reference
}

void CrossVMOperator::MarkFromObject(JSTaggedType value)
{
    JSTaggedValue taggedValue(value);
    if (!taggedValue.IsHeapObject()) {
        return;
    }
    TaggedObject *object = taggedValue.GetHeapObject();
    auto heap = vm_->GetHeap();
    heap->GetUnifiedGCMarker()->MarkFromObject(object);
}

bool CrossVMOperator::IsObjectAlive(JSTaggedType value)
{
    JSTaggedValue taggedValue(value);
    if (!taggedValue.IsHeapObject()) {
        return false;
    }
    TaggedObject *object = taggedValue.GetHeapObject();
    return vm_->GetHeap()->IsAlive(object);
}

bool CrossVMOperator::IsValidHeapObject(JSTaggedType value)
{
    JSTaggedValue taggedValue(value);
    if (!taggedValue.IsHeapObject()) {
        return false;
    }
    TaggedObject *object = taggedValue.GetHeapObject();
    if (g_isEnableCMCGC) {
        return static_cast<BaseObject *>(object)->IsValidObject();
    }
    return vm_->GetHeap()->ContainObject(object);
}

bool CrossVMOperator::UnionStackIsEmpty(bool *isEmpty) const
{
    if (stsVMInterface_ == nullptr) {
        return false;
    }
    return stsVMInterface_->UnionStackIsEmpty(isEmpty);
}

bool CrossVMOperator::ForEachFrameInUnionStack(
    void (*callback)(const void *frame, bool isStaticFrame, void *data), void *data) const
{
    if (stsVMInterface_ == nullptr) {
        return false;
    }
    return stsVMInterface_->ForEachFrameInUnionStack(callback, data);
}

bool CrossVMOperator::GetStaticFrameInfo(const void *frame, arkplatform::HybridFrameInfo &frameInfo) const
{
    if (stsVMInterface_ == nullptr) {
        return false;
    }
    return stsVMInterface_->GetStaticFrameInfo(frame, frameInfo);
}

bool CrossVMOperator::GetDynamicFrameInfo(const void *frame, arkplatform::HybridFrameInfo &frameInfo) const
{
    if (ecmaVMInterface_ == nullptr) {
        return false;
    }
    return ecmaVMInterface_->GetDynamicFrameInfo(frame, frameInfo);
}

bool CrossVMOperator::EcmaVMInterfaceImpl::StartXRefMarking()
{
    return SharedHeap::GetInstance()->TriggerUnifiedGCMark<TriggerGCType::UNIFIED_GC, GCReason::CROSSREF_CAUSE>(
        vm_->GetJSThread());
}

void CrossVMOperator::EcmaVMInterfaceImpl::NotifyXGCInterruption()
{
    UnifiedGC *unifiedGC = SharedHeap::GetInstance()->GetUnifiedGC();
    unifiedGC->SetInterruptUnifiedGC(true);
}

void FillFunctionName(JSThread *jsThread, FrameHandler *frameHandler, arkplatform::HybridFrameInfo &frameInfo)
{
    auto *method = frameHandler->GetMethod();
    if (method == nullptr) {
        return;
    }
    const JSPandaFile *jsPandaFile = method->GetJSPandaFile(jsThread);
    if (jsPandaFile == nullptr) {
        return;
    }
    const char *functionName = jsPandaFile->GetCpuProfilerMethodName(method->GetMethodId()).first.data();
    if (functionName == nullptr || strlen(functionName) == 0) {
        frameInfo.SetFunctionName("anonymous");
        return;
    }
    size_t length = strlen(functionName);
    if (length > 0 && functionName[0] == '#') {
        size_t index = length - 1;
        while (index > 0 && functionName[index] != '#') {
            index--;
        }
        if (functionName[index] == '#') {
            functionName += (index + 1);
        }
    }
    if (strlen(functionName) == 0) {
        frameInfo.SetFunctionName("anonymous");
        return;
    }
    frameInfo.SetFunctionName(functionName);
}

// Signal-safe line/module/source resolution. Uses only read-only access to mmap'd abc data.
void FillModuleAndPositionSignalSafe(JSThread *jsThread, FrameHandler *frameHandler,
                                     arkplatform::HybridFrameInfo &frameInfo)
{
    auto *method = frameHandler->GetMethod();
    if (method == nullptr) {
        return;
    }
    const JSPandaFile *jsPandaFile = method->GetJSPandaFile(jsThread);
    if (jsPandaFile == nullptr) {
        return;
    }
    MethodLiteral *methodLiteral = method->GetMethodLiteral(jsThread);
    if (methodLiteral == nullptr) {
        return;
    }
    uint32_t bytecodeOffset = frameHandler->GetBytecodeOffset();
    frameInfo.bcOffset = bytecodeOffset;
    panda_file::File::EntityId methodId = methodLiteral->GetMethodId();
    frameInfo.fileId = methodId.GetOffset();
    // nativePtr → ManagedStackFrameId.pandaFilePtr, used for frame deduplication (operator==/hash).
    // GetPandaFile() is read-only pointer fetch, safe in signal handler.
    frameInfo.nativePtr = const_cast<panda_file::File *>(jsPandaFile->GetPandaFile());

    // moduleName: lock-free read-only MethodDataAccessor access
    const char *descriptor = MethodLiteral::GetRecordNameWithSymbol(jsPandaFile, methodId);
    frameInfo.SetModuleName(JSPandaFile::ParseEntryPoint(std::string_view(descriptor)));

    // sourceFile/lineNumber: lock-free read-only debug data access.
    // For HAR modules, sourceFile is in the line number program, not ClassDataAccessor.
    const panda_file::File *pf = jsPandaFile->GetPandaFile();
    if (pf != nullptr) {
        panda_file::MethodDataAccessor mda(*pf, methodId);
        auto debugInfoId = mda.GetDebugInfoId();

        // Try ClassDataAccessor::GetSourceFileId() first (works for static-side abc).
        auto classSourceFileId = panda_file::ClassDataAccessor(*pf, mda.GetClassId()).GetSourceFileId();

        if (debugInfoId.has_value()) {
            panda_file::DebugInfoDataAccessor dda(*pf, debugInfoId.value());
            auto initialFile = classSourceFileId.value_or(panda_file::File::EntityId(0));

            panda_file::debug_helpers::SignalSafeFrameResolver resolver(
                pf, initialFile, dda.GetLineStart(), dda.GetConstantPool(), bytecodeOffset);
            panda_file::LineNumberProgramProcessor<panda_file::debug_helpers::SignalSafeFrameResolver> programProcessor(
                dda.GetLineNumberProgram(), &resolver);
            programProcessor.Process();

            if (resolver.GetSourceFile() != nullptr) {
                frameInfo.SetUrl(resolver.GetSourceFile());
            }
            // Declaration line/column (GetFristLine/GetFristColumn semantics), not execution position.
            // Line number program stores 0-based values; +1 converts to 1-based.
            int32_t resolvedLine = resolver.GetLine();
            if (resolvedLine >= 0) {
                frameInfo.lineNumber = resolvedLine + 1;
            }
            int32_t resolvedColumn = resolver.GetColumn();
            if (resolvedColumn >= 0) {
                frameInfo.columnNumber = resolvedColumn + 1;
            }
        } else if (classSourceFileId.has_value()) {
            // No debug info, but ClassDataAccessor has sourceFile.
            frameInfo.SetUrl(reinterpret_cast<const char *>(pf->GetStringData(classSourceFileId.value()).data));
        }
    }
}

bool CrossVMOperator::EcmaVMInterfaceImpl::GetDynamicFrameInfo(const void *dynamicFramePtr,
                                                               arkplatform::HybridFrameInfo &frameInfo)
{
    JSThread *jsThread = vm_->GetJSThread();
    if (jsThread == nullptr || dynamicFramePtr == nullptr) {
        return false;
    }

    auto *frameHandler = static_cast<panda::ecmascript::FrameHandler *>(const_cast<void *>(dynamicFramePtr));

    frameInfo.isStaticFrame = false;
    FillFunctionName(jsThread, frameHandler, frameInfo);
    frameInfo.lineNumber = -1;
    frameInfo.columnNumber = -1;
    // Must use signal-safe version. FillModuleAndPosition uses DebugInfoExtractor which
    // requires recursive_mutex and can malloc — both async-signal-unsafe.
    FillModuleAndPositionSignalSafe(jsThread, frameHandler, frameInfo);
    return true;
}

bool CrossVMOperator::EcmaVMInterfaceImpl::GetSourceMappedFrameInfo(const void *dynamicFramePtr,
                                                                    arkplatform::HybridFrameInfo &frameInfo)
{
    JSThread *jsThread = vm_->GetJSThread();
    if (jsThread == nullptr) {
        return false;
    }

    panda::ecmascript::ThreadManagedScope managedScope(jsThread);
    std::vector<JsFrameInfo> jsStackInfo = ecmascript::JsStackInfo::BuildJsFrameInfo(jsThread, dynamicFramePtr);

    if (jsStackInfo.empty()) {
        return false;
    }

    JsFrameInfo jsFrameInfo = jsStackInfo.front();
    size_t pos = jsFrameInfo.pos.find(':', 0);
    if (pos != CString::npos) {
        int lineNumber = std::stoi(jsFrameInfo.pos.substr(0, pos));
        int columnNumber = std::stoi(jsFrameInfo.pos.substr(pos + 1));
        if (!jsFrameInfo.fileName.empty()) {
            SourceMap::GetInstance().TranslateUrlPositionBySourceMap(
                jsFrameInfo.fileName, lineNumber, columnNumber, jsFrameInfo.packageName);
        }
        frameInfo.SetFunctionName(jsFrameInfo.functionName);
        frameInfo.SetUrl(jsFrameInfo.fileName);
        frameInfo.lineNumber = lineNumber;
        frameInfo.columnNumber = columnNumber;
        frameInfo.SetClassName(jsFrameInfo.packageName);
    }
    return true;
}

JSTaggedType *CrossVMOperator::EcmaVMInterfaceImpl::GetTopFrameSPFromDynamic() const
{
    JSThread *jsThread = vm_->GetJSThread();
    // When DynamicTopFrameIsValid() detected PC in stub/AOT code, it validated
    // the signal context's fp and stored it as override. Use it directly —
    // FrameIterator(sp, thread) does NOT call AdvanceToJSFrame(), so it
    // safely skips the stub's non-standard frame and starts from the caller's
    // JS frame (the fp from signal context). Without override, fall back to
    // FrameHandler(jsThread) which calls AdvanceToJSFrame() from GetCurrentFrame().
    auto *overrideSp = reinterpret_cast<JSTaggedType *>(dynamicFrameOverride_);
    if (overrideSp != nullptr) {
        dynamicFrameOverride_ = nullptr;
        return overrideSp;
    }
    FrameHandler frameHandler(jsThread);
    return frameHandler.GetSp();
}

bool CrossVMOperator::EcmaVMInterfaceImpl::ForEachDynamicFrame([[maybe_unused]]void *currFrameSP,
    [[maybe_unused]] void *toFrameSP, [[maybe_unused]] void (*cb)(const void *frame, void *data),
    [[maybe_unused]] void *data) const
{
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER)
    auto *jsThread = vm_->GetJSThread();
    if (jsThread == nullptr) {
        return false;
    }
    ASSERT(currFrameSP != nullptr);
    FrameHandler currHandle(reinterpret_cast<JSTaggedType *>(currFrameSP), jsThread);
    if (!JsStackGetter::CheckFrameType(jsThread, currHandle.GetSp())) {
        return false;
    }
    for (; currHandle.HasFrame(); currHandle.PrevJSFrame()) {
        if (toFrameSP != nullptr && currHandle.GetSp() == toFrameSP) {
            break;
        }
        if (currHandle.IsEntryFrame() || currHandle.IsBuiltinFrame()) {
            continue;
        }
        cb(static_cast<void *>(&currHandle), data);
    }
#endif
    return true;
}

const void *CrossVMOperator::EcmaVMInterfaceImpl::GetEcmaVM() const
{
    return static_cast<const void *>(vm_);
}

#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER)
static uint64_t GetPcFromContext(void *context)
{
    [[maybe_unused]] ucontext_t *ucontext = reinterpret_cast<ucontext_t*>(context);
    [[maybe_unused]] mcontext_t &mcontext = ucontext->uc_mcontext;
    uint64_t pc = 0;
#if defined(PANDA_TARGET_AMD64)
    pc = static_cast<uint64_t>(mcontext.gregs[REG_RIP]);
#elif defined(PANDA_TARGET_ARM64)
    pc = static_cast<uint64_t>(mcontext.pc);
#else
    LOG_FULL(FATAL) << "AsmInterpreter does not currently support other platforms, please run on x64 and arm64";
    pc = 0;
#endif
    return pc;
}

static bool IsAddrAtStubOrAot(EcmaVM *vm, uint64_t pc)
{
    AOTFileManager *loader = vm->GetAOTFileManager();
    return loader->InsideStub(pc) || loader->InsideAOT(pc);
}

static bool InHeaderOrTail(uint64_t pc, uint64_t entryBegin, uint64_t entryDuration, uint64_t headerSize,
                           uint64_t tailSize)
{
    uintptr_t entryEnd = entryBegin + entryDuration;
    if (pc >= entryBegin && pc <= (entryBegin + headerSize)) {
        return true;
    }
    if (pc <= entryEnd && pc >= (entryEnd - tailSize)) {
        return true;
    }
    return false;
}

static bool IsEntryFrameHeaderOrTail(JSThread *thread, uint64_t pc)
{
    uint64_t headerSize = 0;
    uint64_t tailSize = 0;
    uint64_t entryDuration = 0;
    Assembler::GetFrameCompletionPos(headerSize, tailSize, entryDuration);
    uintptr_t entryBegin = thread->GetRTInterface(kungfu::RuntimeStubCSigns::ID_AsmInterpreterEntry);
    bool inAsmInterpreterEntry = InHeaderOrTail(pc, entryBegin, entryDuration, headerSize, tailSize);
    entryBegin = thread->GetRTInterface(kungfu::RuntimeStubCSigns::ID_GeneratorReEnterAsmInterp);
    bool inGeneratorReEnterAsmInterp = InHeaderOrTail(pc, entryBegin, entryDuration, headerSize, tailSize);
    return (inAsmInterpreterEntry || inGeneratorReEnterAsmInterp);
}

static bool IfNeedSkipBarrierStubHeaderOrTail(JSThread *thread, uint64_t pc)
{
    uint64_t headerSize = 20;
    uintptr_t entryBegin = thread->GetFastStubEntry(kungfu::CommonStubCSigns::ID::GetValueWithBarrier);
    bool needSkip = InHeaderOrTail(pc, entryBegin, 0, headerSize, 0);
    return needSkip;
}

static bool GetFpAndSpFromContext(void *signalContextPtr, void **fp, void **sp)
{
    ucontext_t *ucontext = reinterpret_cast<ucontext_t *>(signalContextPtr);
    mcontext_t &mcontext = ucontext->uc_mcontext;
#if defined(PANDA_TARGET_AMD64)
    *fp = reinterpret_cast<void *>(mcontext.gregs[REG_RBP]);
    *sp = reinterpret_cast<void *>(mcontext.gregs[REG_RSP]);
#elif defined(PANDA_TARGET_ARM64)
    *fp = reinterpret_cast<void *>(mcontext.regs[29]); // FP is an alias for x29.
    *sp = reinterpret_cast<void *>(mcontext.sp);
#else
    LOG_FULL(FATAL) << "AsmInterpreter does not currently support other platforms, please run on x64 and arm64";
    return false;
#endif
    return true;
}

static bool TrySetDynamicFrameOverrideForStubAot(JSThread *jsThread, void *signalContextPtr, uint64_t pc,
                                                 void *&dynamicFrameOverride)
{
    if (IfNeedSkipBarrierStubHeaderOrTail(jsThread, pc)) {
        return false;
    }
    void *fp = nullptr;
    void *sp = nullptr;
    if (!GetFpAndSpFromContext(signalContextPtr, &fp, &sp)) {
        return false;
    }
    if (sp > fp) {
        return false;
    }
    if (!JsStackGetter::CheckFrameType(jsThread, reinterpret_cast<JSTaggedType *>(fp))) {
        return false;
    }
    dynamicFrameOverride = fp;
    return true;
}
#endif

bool CrossVMOperator::EcmaVMInterfaceImpl::DynamicTopFrameIsValid([[maybe_unused]] void *signalContextPtr)
{
#if defined(ECMASCRIPT_SUPPORT_CPUPROFILER)
    auto *jsThread = vm_->GetJSThread();
    if (jsThread == nullptr) {
        return false;
    }

    uint64_t pc = 0;
    if (jsThread->IsAsmInterpreter()) {
        // If the attempt fails, the callback will be terminated directly to avoid the reentrancy deadlock,
        // and a sampling will be abandoned. Failures are rare, so the impact on the overall sampling results
        // is very limited.
        if (!vm_->GetAOTFileManager()->TryReadLock()) {
            return false;
        }
        pc = GetPcFromContext(signalContextPtr);
    }

    if (jsThread->IsAsmInterpreter() && IsAddrAtStubOrAot(vm_, pc) &&
        !IsEntryFrameHeaderOrTail(jsThread, pc)) {
        return TrySetDynamicFrameOverrideForStubAot(jsThread, signalContextPtr, pc, dynamicFrameOverride_);
    }
    if (jsThread->IsAsmInterpreter()) {
        auto *leaveFrame = const_cast<JSTaggedType *>(jsThread->GetLastLeaveFrame());
        if (leaveFrame == nullptr || !JsStackGetter::CheckFrameType(jsThread, leaveFrame)) {
            return false;
        }
        dynamicFrameOverride_ = leaveFrame;
    }
    return true;
#else
    return false;
#endif
}

bool CrossVMOperator::EcmaVMInterfaceImpl::IsJSGCRunning()
{
    JSThread *jsThread = vm_->GetJSThread();
    return jsThread->GetGcState();
}

std::string CrossVMOperator::EcmaVMInterfaceImpl::GetArkTSMode() const
{
    ArkTSMode mode = vm_->GetArkTSMode();
    switch (mode) {
        case ArkTSMode::STATIC:
            return "static";
        case ArkTSMode::HYBRID:
            return "hybrid";
        default:
            return "dynamic";
    }
}

std::unique_ptr<AbstractDumper> CrossVMOperator::EcmaVMInterfaceImpl::CreateHeapDumper(const DumpRequest &request)
{
#if defined(ECMASCRIPT_SUPPORT_HEAPPROFILER)
    return DynamicDump::Create(vm_, request);
#else
    (void)request;
    return nullptr;
#endif
}
}  // namespace panda::ecmascript