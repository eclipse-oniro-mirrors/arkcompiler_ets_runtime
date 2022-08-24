/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include "ecmascript/js_thread.h"

#include "ecmascript/ecma_global_storage.h"
#include "ecmascript/ecma_param_configuration.h"
#include "ecmascript/global_env_constants-inl.h"
#include "ecmascript/ic/properties_cache.h"
#include "ecmascript/interpreter/interpreter-inl.h"
#include "ecmascript/llvm_stackmap_parser.h"
#include "ecmascript/mem/mark_word.h"


namespace panda::ecmascript {
using CommonStubCSigns = panda::ecmascript::kungfu::CommonStubCSigns;
using BytecodeStubCSigns = panda::ecmascript::kungfu::BytecodeStubCSigns;

// static
JSThread *JSThread::Create(EcmaVM *vm)
{
    auto jsThread = new JSThread(vm);
    AsmInterParsedOption asmInterOpt = vm->GetJSOptions().GetAsmInterParsedOption();
    if (asmInterOpt.enableAsm) {
        jsThread->EnableAsmInterpreter();
    }

    jsThread->nativeAreaAllocator_ = vm->GetNativeAreaAllocator();
    jsThread->heapRegionAllocator_ = vm->GetHeapRegionAllocator();
    // algin with 16
    size_t maxStackSize = vm->GetEcmaParamConfiguration().GetMaxStackSize();
    jsThread->glueData_.frameBase_ = static_cast<JSTaggedType *>(
        vm->GetNativeAreaAllocator()->Allocate(sizeof(JSTaggedType) * maxStackSize));
    jsThread->glueData_.currentFrame_ = jsThread->glueData_.frameBase_ + maxStackSize;
    EcmaInterpreter::InitStackFrame(jsThread);

    if (jsThread->IsAsmInterpreter()) {
        size_t stackSize = GetAsmStackSize();
        if (stackSize <= EcmaParamConfiguration::GetDefalutReservedStackSize()) {
            LOG_ECMA(FATAL) << "Too small stackSize to run jsvm:" << stackSize;
        }
        // init stack limit of asm interpreter
        ASSERT(GetCurrentStackPosition() > (stackSize - EcmaParamConfiguration::GetDefalutReservedStackSize()));
        // To avoid too much times of stack overflow checking, we only check stack overflow before push vregs or
        // parameters of variable length. So we need a reserved size of stack to make sure stack won't be overflowed
        // when push other data.
        jsThread->glueData_.stackLimit_ =
            GetCurrentStackPosition() - (stackSize - EcmaParamConfiguration::GetDefalutReservedStackSize());
    }
    return jsThread;
}

JSThread::JSThread(EcmaVM *vm) : id_(os::thread::GetCurrentThreadId()), vm_(vm)
{
    auto chunk = vm->GetChunk();
    globalStorage_ = chunk->New<EcmaGlobalStorage>(this, chunk);
    propertiesCache_ = new PropertiesCache();
    vmThreadControl_ = new VmThreadControl();
}

JSThread::~JSThread()
{
    for (auto n : handleStorageNodes_) {
        delete n;
    }
    handleStorageNodes_.clear();
    currentHandleStorageIndex_ = -1;
    handleScopeCount_ = 0;
    handleScopeStorageNext_ = handleScopeStorageEnd_ = nullptr;
    GetEcmaVM()->GetChunk()->Delete(globalStorage_);

    GetNativeAreaAllocator()->Free(glueData_.frameBase_, sizeof(JSTaggedType) *
        vm_->GetEcmaParamConfiguration().GetMaxStackSize());
    glueData_.frameBase_ = nullptr;
    nativeAreaAllocator_ = nullptr;
    heapRegionAllocator_ = nullptr;
    if (propertiesCache_ != nullptr) {
        delete propertiesCache_;
        propertiesCache_ = nullptr;
    }
    if (vmThreadControl_ != nullptr) {
        delete vmThreadControl_;
        vmThreadControl_ = nullptr;
    }
}

void JSThread::SetException(JSTaggedValue exception)
{
    glueData_.exception_ = exception;
}

void JSThread::ClearException()
{
    glueData_.exception_ = JSTaggedValue::Hole();
}

JSTaggedValue JSThread::GetCurrentLexenv() const
{
    FrameHandler frameHandler(this);
    return frameHandler.GetEnv();
}

const JSTaggedType *JSThread::GetCurrentFrame() const
{
    if (IsAsmInterpreter()) {
        return GetLastLeaveFrame();
    }
    return GetCurrentSPFrame();
}

void JSThread::SetCurrentFrame(JSTaggedType *sp)
{
    if (IsAsmInterpreter()) {
        return SetLastLeaveFrame(sp);
    }
    return SetCurrentSPFrame(sp);
}

const JSTaggedType *JSThread::GetCurrentInterpretedFrame() const
{
    if (IsAsmInterpreter()) {
        auto frameHandler = FrameHandler(this);
        return frameHandler.GetSp();
    }
    return GetCurrentSPFrame();
}

void JSThread::Iterate(const RootVisitor &visitor, const RootRangeVisitor &rangeVisitor,
    const RootBaseAndDerivedVisitor &derivedVisitor)
{
    if (propertiesCache_ != nullptr) {
        propertiesCache_->Clear();
    }

    if (!glueData_.exception_.IsHole()) {
        visitor(Root::ROOT_VM, ObjectSlot(ToUintPtr(&glueData_.exception_)));
    }
    // visit global Constant
    glueData_.globalConst_.VisitRangeSlot(rangeVisitor);
    // visit stack roots
    FrameHandler frameHandler(this);
    frameHandler.Iterate(visitor, rangeVisitor, derivedVisitor);
    // visit tagged handle storage roots
#if ECMASCRIPT_ENABLE_HANDLE_LEAK_CHECK
    IterateHandleWithCheck(visitor, rangeVisitor);
#else
    if (currentHandleStorageIndex_ != -1) {
        int32_t nid = currentHandleStorageIndex_;
        for (int32_t i = 0; i <= nid; ++i) {
            auto node = handleStorageNodes_.at(i);
            auto start = node->data();
            auto end = (i != nid) ? &(node->data()[NODE_BLOCK_SIZE]) : handleScopeStorageNext_;
            rangeVisitor(ecmascript::Root::ROOT_HANDLE, ObjectSlot(ToUintPtr(start)), ObjectSlot(ToUintPtr(end)));
        }
    }

    globalStorage_->IterateUsageGlobal([visitor](EcmaGlobalStorage::Node *node) {
        JSTaggedValue value(node->GetObject());
        if (value.IsHeapObject()) {
            visitor(ecmascript::Root::ROOT_HANDLE, ecmascript::ObjectSlot(node->GetObjectAddress()));
        }
    });
#endif
}

#if ECMASCRIPT_ENABLE_HANDLE_LEAK_CHECK
void JSThread::IterateHandleWithCheck(const RootVisitor &visitor, const RootRangeVisitor &rangeVisitor)
{
    size_t handleCount = 0;
    if (currentHandleStorageIndex_ != -1) {
        int32_t nid = currentHandleStorageIndex_;
        for (int32_t i = 0; i <= nid; ++i) {
            auto node = handleStorageNodes_.at(i);
            auto start = node->data();
            auto end = (i != nid) ? &(node->data()[NODE_BLOCK_SIZE]) : handleScopeStorageNext_;
            rangeVisitor(ecmascript::Root::ROOT_HANDLE, ObjectSlot(ToUintPtr(start)), ObjectSlot(ToUintPtr(end)));
            handleCount += (ToUintPtr(end) - ToUintPtr(start)) / sizeof(JSTaggedType);
        }
    }

    size_t globalCount = 0;
    static const int JS_TYPE_LAST = static_cast<int>(JSType::TYPE_LAST);
    int typeCount[JS_TYPE_LAST] = { 0 };
    int primitiveCount = 0;
    globalStorage_->IterateUsageGlobal(
        [visitor, &globalCount, &typeCount, &primitiveCount](EcmaGlobalStorage::Node *node) {
        node->MarkCount();
        JSTaggedValue value(node->GetObject());
        if (value.IsHeapObject()) {
            visitor(ecmascript::Root::ROOT_HANDLE, ecmascript::ObjectSlot(node->GetObjectAddress()));
            TaggedObject *object = value.GetTaggedObject();
            MarkWord word(value.GetTaggedObject());
            if (word.IsForwardingAddress()) {
                object = word.ToForwardingAddress();
            }
            typeCount[static_cast<int>(object->GetClass()->GetObjectType())]++;

            // There are some reasonable framework-level global objects in the initial phase.
            // The value can be adjusted as required.
            static const int MIN_NUMBER_COUNT = 110000;
            static const int MARK_INTERVAL_TIMES = 10;
            // Print global information about possible memory leaks.
            // You can print the global new stack within the range of the leaked global number.
            if (node->GetGlobalNumber() > MIN_NUMBER_COUNT && ((node->GetMarkCount() % MARK_INTERVAL_TIMES) == 0)) {
                LOG_ECMA(INFO) << "Global maybe leak object address:" << std::hex << object
                               << ", type:" << JSHClass::DumpJSType(JSType(object->GetClass()->GetObjectType()))
                               << ", node address:" << node
                               << ", number:" << std::dec <<  node->GetGlobalNumber()
                               << ", markCount:" << node->GetMarkCount();
            }
        } else {
            primitiveCount++;
        }
        globalCount++;
    });

    // Determine whether memory leakage by checking handle and global count.
    LOG_ECMA(INFO) << "Iterate root handle count:" << handleCount << ", global handle count:" << globalCount;
    LOG_ECMA(INFO) << "Global type Primitive count:" << primitiveCount;
    // Print global object type statistic.
    static const int MIN_COUNT_THRESHOLD = 50;
    for (int i = 0; i < JS_TYPE_LAST; i++) {
        if (typeCount[i] > MIN_COUNT_THRESHOLD) {
            LOG_ECMA(INFO) << "Global type " << JSHClass::DumpJSType(JSType(i)) << " count:" << typeCount[i];
        }
    }
}
#endif

void JSThread::IterateWeakEcmaGlobalStorage(const WeakRootVisitor &visitor)
{
    globalStorage_->IterateWeakUsageGlobal([visitor](EcmaGlobalStorage::Node *node) {
        JSTaggedValue value(node->GetObject());
        if (value.IsHeapObject()) {
            auto object = value.GetTaggedObject();
            auto fwd = visitor(object);
            if (fwd == nullptr) {
                // undefind
                node->SetObject(JSTaggedValue::Undefined().GetRawData());
                reinterpret_cast<EcmaGlobalStorage::WeakNode *>(node)->CallWeakCallback();
            } else if (fwd != object) {
                // update
                node->SetObject(JSTaggedValue(fwd).GetRawData());
            }
        }
    });
}

bool JSThread::DoStackOverflowCheck(const JSTaggedType *sp)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (UNLIKELY(sp <= glueData_.frameBase_ + RESERVE_STACK_SIZE)) {
        LOG_ECMA(ERROR) << "Stack overflow! Remaining stack size is: " << (sp - glueData_.frameBase_);
        if (LIKELY(!HasPendingException())) {
            ObjectFactory *factory = GetEcmaVM()->GetFactory();
            JSHandle<JSObject> error = factory->GetJSError(base::ErrorType::RANGE_ERROR, "Stack overflow!", false);
            SetException(error.GetTaggedValue());
        }
        return true;
    }
    return false;
}

uintptr_t *JSThread::ExpandHandleStorage()
{
    uintptr_t *result = nullptr;
    int32_t lastIndex = static_cast<int32_t>(handleStorageNodes_.size() - 1);
    if (currentHandleStorageIndex_ == lastIndex) {
        auto n = new std::array<JSTaggedType, NODE_BLOCK_SIZE>();
        handleStorageNodes_.push_back(n);
        currentHandleStorageIndex_++;
        result = reinterpret_cast<uintptr_t *>(&n->data()[0]);
        handleScopeStorageEnd_ = &n->data()[NODE_BLOCK_SIZE];
    } else {
        currentHandleStorageIndex_++;
        auto lastNode = handleStorageNodes_[currentHandleStorageIndex_];
        result = reinterpret_cast<uintptr_t *>(&lastNode->data()[0]);
        handleScopeStorageEnd_ = &lastNode->data()[NODE_BLOCK_SIZE];
    }

    return result;
}

void JSThread::ShrinkHandleStorage(int prevIndex)
{
    currentHandleStorageIndex_ = prevIndex;
    int32_t lastIndex = static_cast<int32_t>(handleStorageNodes_.size() - 1);
#if ECMASCRIPT_ENABLE_ZAP_MEM
    uintptr_t size = ToUintPtr(handleScopeStorageEnd_) - ToUintPtr(handleScopeStorageNext_);
    if (memset_s(handleScopeStorageNext_, size, 0, size) != EOK) {
        LOG_FULL(FATAL) << "memcpy_s failed";
        UNREACHABLE();
    }
    for (int32_t i = currentHandleStorageIndex_ + 1; i < lastIndex; i++) {
        if (memset_s(handleStorageNodes_[i],
                     NODE_BLOCK_SIZE * sizeof(JSTaggedType), 0,
                     NODE_BLOCK_SIZE * sizeof(JSTaggedType)) !=
                     EOK) {
            LOG_FULL(FATAL) << "memcpy_s failed";
            UNREACHABLE();
        }
    }
#endif

    if (lastIndex > MIN_HANDLE_STORAGE_SIZE && currentHandleStorageIndex_ < MIN_HANDLE_STORAGE_SIZE) {
        for (int i = MIN_HANDLE_STORAGE_SIZE; i < lastIndex; i++) {
            auto node = handleStorageNodes_.back();
            delete node;
            handleStorageNodes_.pop_back();
        }
    }
}

void JSThread::NotifyStableArrayElementsGuardians(JSHandle<JSObject> receiver)
{
    if (!receiver->GetJSHClass()->IsPrototype()) {
        return;
    }
    if (!stableArrayElementsGuardians_) {
        return;
    }
    auto env = GetEcmaVM()->GetGlobalEnv();
    if (receiver.GetTaggedValue() == env->GetObjectFunctionPrototype().GetTaggedValue() ||
        receiver.GetTaggedValue() == env->GetArrayPrototype().GetTaggedValue()) {
        stableArrayElementsGuardians_ = false;
    }
}

void JSThread::ResetGuardians()
{
    stableArrayElementsGuardians_ = true;
}

void JSThread::CheckSwitchDebuggerBCStub()
{
    auto isDebug = GetEcmaVM()->GetJsDebuggerManager()->IsDebugMode();
    if (isDebug &&
        glueData_.bcDebuggerStubEntries_.Get(0) == glueData_.bcDebuggerStubEntries_.Get(1)) {
        for (size_t i = 0; i < BCStubEntries::BC_HANDLER_COUNT; i++) {
            auto stubEntry = glueData_.bcStubEntries_.Get(i);
            auto debuggerStubEbtry = glueData_.bcDebuggerStubEntries_.Get(i);
            glueData_.bcDebuggerStubEntries_.Set(i, stubEntry);
            glueData_.bcStubEntries_.Set(i, debuggerStubEbtry);
        }
    } else if (!isDebug &&
        glueData_.bcStubEntries_.Get(0) == glueData_.bcStubEntries_.Get(1)) {
        for (size_t i = 0; i < BCStubEntries::BC_HANDLER_COUNT; i++) {
            auto stubEntry = glueData_.bcDebuggerStubEntries_.Get(i);
            auto debuggerStubEbtry = glueData_.bcStubEntries_.Get(i);
            glueData_.bcStubEntries_.Set(i, stubEntry);
            glueData_.bcDebuggerStubEntries_.Set(i, debuggerStubEbtry);
        }
    }
}

bool JSThread::CheckSafepoint() const
{
    if (vmThreadControl_->VMNeedSuspension()) {
        vmThreadControl_->SuspendVM();
    }
#ifndef NDEBUG
    if (vm_->GetJSOptions().EnableForceGC()) {
        GetEcmaVM()->CollectGarbage(TriggerGCType::FULL_GC);
        return true;
    }
#endif
    if (IsMarkFinished()) {
        auto heap = GetEcmaVM()->GetHeap();
        heap->GetConcurrentMarker()->HandleMarkingFinished();
        return true;
    }
    return false;
}

void JSThread::CheckJSTaggedType(JSTaggedType value) const
{
    if (JSTaggedValue(value).IsHeapObject() &&
        !GetEcmaVM()->GetHeap()->IsAlive(reinterpret_cast<TaggedObject *>(value))) {
        LOG_FULL(FATAL) << "value:" << value << " is invalid!";
    }
}

void JSThread::CollectBCOffsetInfo()
{
    FrameBcCollector collector(this);
    collector.CollectBCOffsetInfo();
}

// static
size_t JSThread::GetAsmStackSize()
{
    size_t result = EcmaParamConfiguration::GetDefalutStackSize();
    pthread_attr_t attr;
    int ret = pthread_attr_init(&attr);
    if (ret != 0) {
        LOG_ECMA(ERROR) << "Get current thread attr failed";
        return result;
    }
    size_t size = 0;
    ret = pthread_attr_getstacksize(&attr, &size);
    if (ret != 0) {
        LOG_ECMA(ERROR) << "Get current thread stack size failed";
        return result;
    }

    if (size < result) {
        result = size;
    }
    LOG_ECMA(INFO) << "Current thread asm stack size:" << result;
    ret = pthread_attr_destroy(&attr);
    if (ret != 0) {
        LOG_ECMA(ERROR) << "Destroy current thread attr failed";
    }
    return result;
}
}  // namespace panda::ecmascript
