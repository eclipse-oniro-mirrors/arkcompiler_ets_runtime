/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "ecmascript/ecma_string_table.h"

#include "common_components/taskpool/taskpool.h"
#include "ecmascript/ecma_string-inl.h"
#include "ecmascript/ecma_string_table_optimization-inl.h"
#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/string/chained_hash_map-inl.h"

namespace panda::ecmascript {

#define STRING_TABLE_OPERATION_SCOPE() \
    ChainedHashMapOperation<ChainedHashMapConfig::NoSlotBarrier> chainedHashMapOperation(GetChainedHashMap()); \
    ChainedHashMapInUseScope mapInUse(GetChainedHashMap())

#define STRING_TABLE_READ_BARRIER(thread_name) \
    [thread_name](const void *obj, size_t offset) -> TaggedObject* { \
        return Barriers::GetTaggedObject(thread_name, obj, offset); \
    }

#define ASSERT_STRING_TABLE_INVARIANTS(value) \
    do { \
        ASSERT(!EcmaStringAccessor(value).IsInternString()); \
        ASSERT(EcmaStringAccessor(value).NotTreeString()); \
        ASSERT(Region::ObjectAddressToRange(reinterpret_cast<TaggedObject *>(value))->InSharedHeap()); \
    } while (0)

void EcmaStringTableCleaner::PostSweepWeakRefTask(const WeakRootVisitor &visitor)
{
    StartSweepWeakRefTask();
    iter_ = std::make_shared<std::atomic<uint32_t>>(0U);
    const uint32_t postTaskCount = common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum();
    for (uint32_t i = 0U; i < postTaskCount; ++i) {
        common::Taskpool::GetCurrentTaskpool()->PostTask(std::make_unique<SweepWeakRefTask>(iter_, this, visitor));
    }
}

void EcmaStringTableCleaner::JoinAndWaitSweepWeakRefTask(const WeakRootVisitor &visitor)
{
    ProcessSweepWeakRef(iter_, this, visitor);
    WaitSweepWeakRefTask();
    iter_.reset();
}

void EcmaStringTableCleaner::WaitSweepWeakRefTaskFinished()
{
    WaitSweepWeakRefTask();
}

void EcmaStringTableCleaner::PostConcurrentSweepWeakRefTask(const WeakRootVisitor &visitor)
{
    StartSweepWeakRefTask();
    stringTable_->GetChainedHashMap()->StartSweeping();
    iter_ = std::make_shared<std::atomic<uint32_t>>(0U);
    const uint32_t postTaskCount = common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum();
    for (uint32_t i = 0U; i < postTaskCount; ++i) {
        common::Taskpool::GetCurrentTaskpool()->PostTask(
            std::make_unique<ConcurrentSweepWeakRefTask>(iter_, this, visitor));
    }
}

void EcmaStringTableCleaner::WaitConcurrentSweepWeakRefTaskAndSuspend(JSThread *thread)
{
    WaitConcurrentSweepWeakRefTask();
    SuspendAllAndFinishSweeping(thread);
    SignalSweepWeakRefTaskFinish();
    iter_.reset();
}

void EcmaStringTableCleaner::WaitConcurrentSweepWeakRefTaskAndSuspendByDaemonThread(DaemonThread *dThread)
{
    WaitConcurrentSweepWeakRefTask();
    SuspendAllAndFinishSweepingByDaemonThread(dThread);
    SignalSweepWeakRefTaskFinish();
    iter_.reset();
}

void EcmaStringTableCleaner::FinishConcurrentSweepInSTW(DaemonThread *dThread)
{
    WaitConcurrentSweepWeakRefTask();
    FinishSweepingInternal();
    SignalSweepWeakRefTaskFinish();
    iter_.reset();
}

void EcmaStringTableCleaner::WaitConcurrentSweepWeakRefTaskFinished()
{
    WaitConcurrentSweepWeakRefTask();
}

void EcmaStringTableCleaner::ProcessSweepWeakRef(IteratorPtr &iter, EcmaStringTableCleaner *cleaner,
                                                 const WeakRootVisitor &visitor)
{
    uint32_t index = 0U;
    while ((index = GetNextIndexId(iter)) < ChainedHashMapConfig::SWEEP_PARTITION_COUNT) {
        cleaner->stringTable_->SweepWeakRef(visitor, index);
        if (ReduceCountAndCheckFinish(cleaner)) {
            cleaner->SignalSweepWeakRefTaskFinish();
        }
    }
}

void EcmaStringTableCleaner::ProcessConcurrentSweepWeakRef(IteratorPtr &iter, EcmaStringTableCleaner *cleaner,
                                                           const WeakRootVisitor &visitor)
{
    uint32_t index = 0U;
    while ((index = GetNextIndexId(iter)) < ChainedHashMapConfig::SWEEP_PARTITION_COUNT) {
        std::vector<ChainedHashMapEntry*>& waitFreeEntries = cleaner->waitFreeEntries_[index];
        for (const ChainedHashMapEntry* entry : waitFreeEntries) {
            delete entry;
        }
        waitFreeEntries.clear();
        cleaner->stringTable_->ConcurrentSweepWeakRef(visitor, index, waitFreeEntries);
        if (ReduceCountAndCheckFinish(cleaner)) {
            cleaner->SignalSweepWeakRefTaskPending();
        }
    }
}

void EcmaStringTableCleaner::StartSweepWeakRefTask()
{
    // No need lock here, only the daemon thread will reset the state.
    sweepWeakRefFinished_ = SweepState::SWEEPING;
    PendingTaskCount_.store(ChainedHashMapConfig::SWEEP_PARTITION_COUNT, std::memory_order_relaxed);
}

void EcmaStringTableCleaner::WaitSweepWeakRefTask()
{
    LockHolder holder(sweepWeakRefMutex_);
    while (sweepWeakRefFinished_ != SweepState::FINISHED) {
        sweepWeakRefCV_.Wait(&sweepWeakRefMutex_);
    }
}

void EcmaStringTableCleaner::WaitConcurrentSweepWeakRefTask()
{
    LockHolder holder(sweepWeakRefMutex_);
    while (sweepWeakRefFinished_ == SweepState::SWEEPING) {
        sweepWeakRefCV_.Wait(&sweepWeakRefMutex_);
    }
}

void EcmaStringTableCleaner::SignalSweepWeakRefTaskFinish()
{
    LockHolder holder(sweepWeakRefMutex_);
    sweepWeakRefFinished_ = SweepState::FINISHED;
    sweepWeakRefCV_.SignalAll();
}

void EcmaStringTableCleaner::SignalSweepWeakRefTaskPending()
{
    LockHolder holder(sweepWeakRefMutex_);
    sweepWeakRefFinished_ = SweepState::PENDING;
    sweepWeakRefCV_.SignalAll();
}

void EcmaStringTableCleaner::SuspendAllAndFinishSweeping(JSThread* thread)
{
    SuspendAllScope scope(thread);
    FinishSweepingInternal();
}

void EcmaStringTableCleaner::SuspendAllAndFinishSweepingByDaemonThread(DaemonThread* dThread)
{
    ThreadManagedScope runningScope(dThread);
    SuspendAllScope scope(dThread);
    FinishSweepingInternal();
}

void EcmaStringTableCleaner::FinishSweepingInternal()
{
    ProcessCheckAndFreeHeadEntries();
    ChainedHashMap* chainedHashMap = stringTable_->GetChainedHashMap();
    chainedHashMap->ClearToSpaceTagForFreshEntries();
    chainedHashMap->FinishSweeping();
}

void EcmaStringTableCleaner::ProcessCheckAndFreeHeadEntries()
{
    ECMA_BYTRACE_NAME(
        HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "EcmaStringTableCleaner::ProcessCheckAndFreeHeadEntries", "");
    ChainedHashMap* chainedHashMap = stringTable_->GetChainedHashMap();
    for (uint32_t i = 0; i < ChainedHashMapConfig::SWEEP_PARTITION_COUNT; i++) {
        chainedHashMap->CheckAndFreeHeadEntries(i);
    }
}

bool EcmaStringTableCleaner::SweepWeakRefTask::Run([[maybe_unused]] uint32_t threadIndex)
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "EcmaStringTableCleaner::ProcessSweepWeakRef", "");
    ProcessSweepWeakRef(iter_, cleaner_, visitor_);
    return true;
}

bool EcmaStringTableCleaner::ConcurrentSweepWeakRefTask::Run([[maybe_unused]] uint32_t threadIndex)
{
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK,
        "EcmaStringTableCleaner::ProcessConcurrentSweepWeakRef", "");
    ProcessConcurrentSweepWeakRef(iter_, cleaner_, visitor_);
    return true;
}

EcmaString* EcmaStringTableImpl::GetOrInternFlattenString(EcmaVM* vm, EcmaString* string)
{
    STRING_TABLE_OPERATION_SCOPE();
    ASSERT(EcmaStringAccessor(string).NotTreeString());
    if (EcmaStringAccessor(string).IsInternString()) {
        return string;
    }
    JSThread *thread = vm->GetAssociatedJSThread();
    uint32_t hashcode = EcmaStringAccessor(string).GetHashcode(thread);
    ASSERT_STRING_TABLE_INVARIANTS(string);
    auto readBarrierForLoad = STRING_TABLE_READ_BARRIER(thread);
    auto *existing = chainedHashMapOperation.template Load(std::move(readBarrierForLoad), hashcode,
                                                           string->ToBaseString());
    if (existing != nullptr) {
        return EcmaString::FromBaseString(existing);
    }
    JSHandle<EcmaString> stringHandle(thread, string);
    auto readBarrierForStoreOrLoad = STRING_TABLE_READ_BARRIER(thread);
    BaseString *result = chainedHashMapOperation.template StoreOrLoad<
        true, decltype(readBarrierForStoreOrLoad), common::ReadOnlyHandle<BaseString>>(
        thread, std::move(readBarrierForStoreOrLoad), hashcode, stringHandle);
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

EcmaString* EcmaStringTableImpl::GetOrInternFlattenStringNoGC(EcmaVM* vm, EcmaString* string)
{
    STRING_TABLE_OPERATION_SCOPE();
    ASSERT(EcmaStringAccessor(string).NotTreeString());
    if (EcmaStringAccessor(string).IsInternString()) {
        return string;
    }
    JSThread *thread = vm->GetAssociatedJSThread();
    uint32_t hashcode = EcmaStringAccessor(string).GetHashcode(thread);
    ASSERT_STRING_TABLE_INVARIANTS(string);
    auto readBarrier = STRING_TABLE_READ_BARRIER(thread);
    auto *existing = chainedHashMapOperation.template Load(readBarrier, hashcode, string->ToBaseString());
    if (existing != nullptr) {
        return EcmaString::FromBaseString(existing);
    }
    JSHandle<EcmaString> stringHandle(thread, string);
    BaseString* result = chainedHashMapOperation.template StoreOrLoad<
        false, decltype(readBarrier), common::ReadOnlyHandle<BaseString>>(
        thread, std::move(readBarrier), hashcode, stringHandle);
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

EcmaString* EcmaStringTableImpl::GetOrInternStringFromCompressedSubString(
    EcmaVM* vm, const JSHandle<EcmaString>& string, uint32_t offset, uint32_t utf8Len)
{
    STRING_TABLE_OPERATION_SCOPE();
    const uint8_t* utf8Data = EcmaStringAccessor(string).GetDataUtf8() + offset;
    uint32_t hashcode = EcmaStringAccessor::ComputeHashcodeUtf8(utf8Data, utf8Len, true);
    JSThread *thread = vm->GetAssociatedJSThread();
    auto readBarrier = STRING_TABLE_READ_BARRIER(thread);
    auto *existing = chainedHashMapOperation.template Load(std::move(readBarrier), hashcode, string, offset, utf8Len);
    if (existing != nullptr) {
        return EcmaString::FromBaseString(existing);
    }
    BaseString *result = chainedHashMapOperation.template StoreOrLoad(
        thread, hashcode,
        [vm, string, offset, utf8Len, hashcode]() -> common::ReadOnlyHandle<BaseString> {
            EcmaString* str = EcmaStringAccessor::CreateFromUtf8CompressedSubString(vm, string, offset, utf8Len,
                MemSpaceType::SHARED_OLD_SPACE);
            str->SetMixHashcode(hashcode);
            ASSERT_STRING_TABLE_INVARIANTS(str);
            JSThread *thread = vm->GetJSThread();
            JSHandle<EcmaString> strHandle(thread, str);
            return strHandle;
        },
        [vm, utf8Len, string, offset](BaseString *foundString) {
            JSThread *thread = vm->GetAssociatedJSThread();
            const uint8_t *utf8Data = EcmaStringAccessor(string).GetDataUtf8() + offset;
            return EcmaStringAccessor::StringIsEqualUint8Data(thread, EcmaString::FromBaseString(foundString), utf8Data,
                                                              utf8Len, true);
        });
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

EcmaString *EcmaStringTableImpl::GetOrInternString(EcmaVM *vm, EcmaString *string)
{
    STRING_TABLE_OPERATION_SCOPE();
    if (EcmaStringAccessor(string).IsInternString()) {
        return string;
    }
    JSThread *thread = vm->GetJSThread();
    JSHandle<EcmaString> stringHandle(thread, string);
    // may gc
    EcmaString *strFlat = EcmaStringAccessor::Flatten(vm, stringHandle, MemSpaceType::SHARED_OLD_SPACE);
    if (EcmaStringAccessor(strFlat).IsInternString()) {
        return strFlat;
    }
    uint32_t hashcode = EcmaStringAccessor(strFlat).GetHashcode(thread);
    auto readBarrier = STRING_TABLE_READ_BARRIER(thread);
    auto *existing = chainedHashMapOperation.template Load(readBarrier, hashcode, strFlat->ToBaseString());
    if (existing != nullptr) {
        return EcmaString::FromBaseString(existing);
    }
    JSHandle<EcmaString> strFlatHandle(thread, strFlat);
    BaseString* result = chainedHashMapOperation.template StoreOrLoad<
        true, decltype(readBarrier), common::ReadOnlyHandle<BaseString>>(
        thread, std::move(readBarrier), hashcode, strFlatHandle);
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

EcmaString* EcmaStringTableImpl::GetOrInternString(EcmaVM* vm, const JSHandle<EcmaString>& firstString,
                                                   const JSHandle<EcmaString>& secondString)
{
    STRING_TABLE_OPERATION_SCOPE();
    bool signalState = vm->GetJsDebuggerManager()->GetSignalState();
    if (UNLIKELY(signalState)) {
        return GetOrInternStringThreadUnsafe(vm, firstString, secondString);
    }
    JSThread *thread = vm->GetJSThread();
    JSHandle<EcmaString> firstFlat(thread, EcmaStringAccessor::Flatten(vm, firstString));
    JSHandle<EcmaString> secondFlat(thread, EcmaStringAccessor::Flatten(vm, secondString));
    uint32_t hashcode = EcmaStringAccessor::CalculateAllConcatHashCode(thread, firstFlat, secondFlat);
    ASSERT(EcmaStringAccessor(firstFlat).NotTreeString());
    ASSERT(EcmaStringAccessor(secondFlat).NotTreeString());
    BaseString *result = chainedHashMapOperation.template LoadOrStore<true>(
        thread, hashcode,
        [vm, hashcode, thread, firstFlat, secondFlat]() {
            JSHandle<EcmaString> concatHandle(
                thread, EcmaStringAccessor::Concat(vm, firstFlat, secondFlat, MemSpaceType::SHARED_OLD_SPACE));
            EcmaString *value = EcmaStringAccessor::Flatten(vm, concatHandle, MemSpaceType::SHARED_OLD_SPACE);
            value->SetMixHashcode(hashcode);
            ASSERT_STRING_TABLE_INVARIANTS(value);
            JSHandle<EcmaString> stringHandle(thread, value);
            return stringHandle;
        },
        [thread, firstFlat, secondFlat](BaseString *foundString) {
            EcmaString *firstStr = *firstFlat;
            EcmaString *secondStr = *secondFlat;
            return EcmaStringAccessor(EcmaString::FromBaseString(foundString))
                .EqualToSplicedString(thread, firstStr, secondStr);
        });
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

EcmaString* EcmaStringTableImpl::GetOrInternString(EcmaVM* vm, const uint8_t* utf8Data, uint32_t utf8Len,
                                                   bool canBeCompress, [[maybe_unused]] MemSpaceType type)
{
    STRING_TABLE_OPERATION_SCOPE();
    ASSERT(IsSMemSpace(type));
    bool signalState = vm->GetJsDebuggerManager()->GetSignalState();
    if (UNLIKELY(signalState)) {
        return GetOrInternStringThreadUnsafe(vm, utf8Data, utf8Len, canBeCompress);
    }
    JSThread* thread = vm->GetJSThread();
    uint32_t hashcode = EcmaStringAccessor::ComputeHashcodeUtf8(utf8Data, utf8Len, canBeCompress);
    BaseString *result = chainedHashMapOperation.template LoadOrStore<true>(
        thread, hashcode,
        [vm, hashcode, utf8Data, utf8Len, canBeCompress, type]() {
            EcmaString* value = EcmaStringAccessor::CreateFromUtf8(vm, utf8Data, utf8Len, canBeCompress, type);
            value->SetMixHashcode(hashcode);
            ASSERT_STRING_TABLE_INVARIANTS(value);
            JSThread *thread = vm->GetJSThread();
            JSHandle<EcmaString> stringHandle(thread, value);
            return stringHandle;
        },
        [vm, utf8Data, utf8Len, canBeCompress](BaseString *foundString) {
            JSThread *thread = vm->GetAssociatedJSThread();
            return EcmaStringAccessor::StringIsEqualUint8Data(thread, EcmaString::FromBaseString(foundString), utf8Data,
                                                              utf8Len, canBeCompress);
        });
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

EcmaString* EcmaStringTableImpl::GetOrInternString(EcmaVM* vm,
                                                   const uint8_t* utf8Data,
                                                   uint32_t utf16Len,
                                                   MemSpaceType type)
{
    STRING_TABLE_OPERATION_SCOPE();
    ASSERT(IsSMemSpace(type));
    ASSERT(type == MemSpaceType::SHARED_NON_MOVABLE || type == MemSpaceType::SHARED_OLD_SPACE);
    JSThread* thread = vm->GetJSThread();
    EcmaString* str = EcmaStringAccessor::CreateUtf16StringFromUtf8(vm, utf8Data, utf16Len, type);
    uint32_t hashcode = EcmaStringAccessor(str).GetHashcode(thread);
    auto readBarrierForLoad = STRING_TABLE_READ_BARRIER(thread);
    auto* existing = chainedHashMapOperation.template Load(
        std::move(readBarrierForLoad), hashcode, str->ToBaseString());
    if (existing != nullptr) {
        return EcmaString::FromBaseString(existing);
    }
    JSHandle<EcmaString> strHandle(thread, str);
    auto readBarrierForStoreOrLoad = STRING_TABLE_READ_BARRIER(thread);
    BaseString* result = chainedHashMapOperation.template StoreOrLoad<true,
                                                                      decltype(readBarrierForStoreOrLoad),
                                                                      common::ReadOnlyHandle<BaseString>>(
        thread, std::move(readBarrierForStoreOrLoad), hashcode, strHandle);
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

EcmaString* EcmaStringTableImpl::GetOrInternString(EcmaVM* vm, const uint16_t* utf16Data, uint32_t utf16Len,
                                                   bool canBeCompress)
{
    STRING_TABLE_OPERATION_SCOPE();
    JSThread* thread = vm->GetJSThread();
    uint32_t hashcode = EcmaStringAccessor::ComputeHashcodeUtf16(const_cast<uint16_t*>(utf16Data), utf16Len);
    BaseString *result = chainedHashMapOperation.template LoadOrStore<true>(
        thread, hashcode,
        [vm, utf16Data, utf16Len, canBeCompress, hashcode]() {
            EcmaString* value = EcmaStringAccessor::CreateFromUtf16(vm, utf16Data, utf16Len, canBeCompress,
                                                                    MemSpaceType::SHARED_OLD_SPACE);
            value->SetMixHashcode(hashcode);
            ASSERT_STRING_TABLE_INVARIANTS(value);
            JSThread *thread = vm->GetJSThread();
            JSHandle<EcmaString> stringHandle(thread, value);
            return stringHandle;
        },
        [vm, utf16Data, utf16Len](BaseString *foundString) {
            JSThread *thread = vm->GetAssociatedJSThread();
            return EcmaStringAccessor::StringsAreEqualUtf16(thread, EcmaString::FromBaseString(foundString), utf16Data,
                                                            utf16Len);
        });
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

EcmaString *EcmaStringTableImpl::TryGetInternString(JSThread *thread, const JSHandle<EcmaString> &string)
{
    STRING_TABLE_OPERATION_SCOPE();
    uint32_t hashcode = EcmaStringAccessor(*string).GetHashcode(thread);
    EcmaString *str = *string;
    auto readBarrier = STRING_TABLE_READ_BARRIER(thread);
    return EcmaString::FromBaseString(
        chainedHashMapOperation.template Load<false>(std::move(readBarrier), hashcode, str->ToBaseString()));
}

// used in jit thread, which unsupport create jshandle
EcmaString* EcmaStringTableImpl::GetOrInternStringWithoutJSHandleForJit(EcmaVM* vm, const uint8_t* utf8Data,
    uint32_t utf8Len, bool canBeCompress, MemSpaceType type)
{
    STRING_TABLE_OPERATION_SCOPE();
    ASSERT(IsSMemSpace(type));
    ASSERT(type == MemSpaceType::SHARED_NON_MOVABLE || type == MemSpaceType::SHARED_OLD_SPACE);
    uint32_t hashcode = EcmaStringAccessor::ComputeHashcodeUtf8(utf8Data, utf8Len, canBeCompress);
    JSThread* thread = vm->GetJSThread();
    BaseString *result = chainedHashMapOperation.LoadOrStoreForJit(
        thread, hashcode,
        [vm, utf8Data, utf8Len, canBeCompress, type, hashcode]() {
            EcmaString *value = EcmaStringAccessor::CreateFromUtf8(vm, utf8Data, utf8Len, canBeCompress, type);
            value->SetMixHashcode(hashcode);
            ASSERT_STRING_TABLE_INVARIANTS(value);
            return value->ToBaseString();
        },
        [vm, utf8Data, utf8Len, canBeCompress](BaseString *foundString) {
            JSThread *thread = vm->GetAssociatedJSThread();
            return EcmaStringAccessor::StringIsEqualUint8Data(thread, EcmaString::FromBaseString(foundString), utf8Data,
                                                              utf8Len, canBeCompress);
        });
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

// used in jit thread, which unsupport create jshandle
EcmaString* EcmaStringTableImpl::GetOrInternStringWithoutJSHandleForJit(EcmaVM* vm, const uint8_t* utf8Data,
    uint32_t utf16Len, MemSpaceType type)
{
    STRING_TABLE_OPERATION_SCOPE();
    ASSERT(vm->GetJSThread()->IsJitThread());
    ASSERT(IsSMemSpace(type));
    type = (type == MemSpaceType::SHARED_NON_MOVABLE) ? type : MemSpaceType::SHARED_OLD_SPACE;
    CVector<uint16_t> u16Buffer(utf16Len);
    utf::ConvertRegionMUtf8ToUtf16(utf8Data, u16Buffer.data(), utf::Mutf8Size(utf8Data), utf16Len, 0);
    uint32_t hashcode = EcmaStringAccessor::ComputeHashcodeUtf16(u16Buffer.data(), utf16Len);
    const uint16_t *utf16Data = u16Buffer.data();
    JSThread* thread = vm->GetJSThread();
    BaseString *result = chainedHashMapOperation.LoadOrStoreForJit(
        thread, hashcode,
        [vm, u16Buffer, utf16Len, hashcode, type]() {
            EcmaString *value = EcmaStringAccessor::CreateFromUtf16(vm, u16Buffer.data(), utf16Len, false, type);
            value->SetMixHashcode(hashcode);
            ASSERT_STRING_TABLE_INVARIANTS(value);
            return value->ToBaseString();
        },
        [vm, utf16Data, utf16Len](BaseString *foundString) {
            JSThread *thread = vm->GetAssociatedJSThread();
            return EcmaStringAccessor::StringsAreEqualUtf16(thread, EcmaString::FromBaseString(foundString), utf16Data,
                                                            utf16Len);
        });
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

void EcmaStringTableImpl::SweepWeakRef(const WeakRootVisitor &visitor, uint32_t partitionID)
{
    auto *chainedHashMap = GetChainedHashMap();
    ChainedHashMapOperation<ChainedHashMapConfig::NoSlotBarrier> chainedHashMapOperation(chainedHashMap);
    chainedHashMap->ForEachBucketHeadInPartition(partitionID, [&](ChainedHashMapEntry* bucketHead, uint32_t index) {
        chainedHashMapOperation.ClearNodeFromGC(bucketHead, index, visitor);
    });
}

void EcmaStringTableImpl::ConcurrentSweepWeakRef(const WeakRootVisitor &visitor, uint32_t partitionID,
                                                 std::vector<ChainedHashMapEntry*>& waitDeleteEntries)
{
    auto *chainedHashMap = GetChainedHashMap();
    using ConcurrentSweepOperation = ChainedHashMapOperation<ChainedHashMapConfig::NeedSlotBarrier>;
    ConcurrentSweepOperation chainedHashMapOperation(chainedHashMap);
    chainedHashMap->ForEachBucketHeadInPartition(partitionID, [&](ChainedHashMapEntry* bucketHead, uint32_t index) {
        chainedHashMapOperation.ClearNodeFromGC(bucketHead, index, visitor, waitDeleteEntries);
    });
}

bool EcmaStringTableImpl::CheckStringTableValidity(JSThread *thread)
{
    STRING_TABLE_OPERATION_SCOPE();
    bool isValid = true;
    auto readBarrier = STRING_TABLE_READ_BARRIER(thread);
    chainedHashMapOperation.Range(std::move(readBarrier), isValid);
    return isValid;
}

JSTaggedValue SingleCharTable::CreateSingleCharTable(JSThread *thread)
{
    auto table = thread->GetEcmaVM()->GetFactory()->NewTaggedArray(MAX_ONEBYTE_CHARCODE, JSTaggedValue::Undefined(),
                                                                   MemSpaceType::SHARED_NON_MOVABLE);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    for (uint32_t i = 1; i < MAX_ONEBYTE_CHARCODE; ++i) {
        std::string tmp(1, i + 0X00);  // 1: size
        table->Set(thread, i, factory->NewFromASCIIReadOnly(tmp).GetTaggedValue());
    }
    return table.GetTaggedValue();
}

// This should only call in Debugger Signal, and need to fix and remove
EcmaString* EcmaStringTableImpl::GetOrInternStringThreadUnsafe(
    EcmaVM* vm, const JSHandle<EcmaString> firstString,
    const JSHandle<EcmaString> secondString)
{
    STRING_TABLE_OPERATION_SCOPE();
    ASSERT(vm->GetJsDebuggerManager()->GetSignalState());
    JSThread *thread = vm->GetJSThreadNoCheck();
    JSHandle<EcmaString> firstFlat(thread, EcmaStringAccessor::Flatten(vm, firstString));
    JSHandle<EcmaString> secondFlat(thread, EcmaStringAccessor::Flatten(vm, secondString));
    uint32_t hashcode = EcmaStringAccessor::CalculateAllConcatHashCode(thread, firstFlat, secondFlat);
    ASSERT(EcmaStringAccessor(firstFlat).NotTreeString());
    ASSERT(EcmaStringAccessor(secondFlat).NotTreeString());
    BaseString *result = chainedHashMapOperation.template LoadOrStore<false>(
        thread, hashcode,
        [hashcode, thread, vm, firstFlat, secondFlat]() {
            JSHandle<EcmaString> concatHandle(
                thread, EcmaStringAccessor::Concat(vm, firstFlat, secondFlat, MemSpaceType::SHARED_OLD_SPACE));
            EcmaString *concatString = EcmaStringAccessor::Flatten(vm, concatHandle, MemSpaceType::SHARED_OLD_SPACE);
            concatString->SetMixHashcode(hashcode);
            ASSERT_STRING_TABLE_INVARIANTS(concatString);
            JSHandle<EcmaString> stringHandle(thread, concatString);
            return stringHandle;
        },
        [vm, firstFlat, secondFlat](BaseString *foundString) {
            EcmaString *firstStr = *firstFlat;
            EcmaString *secondStr = *secondFlat;
            JSThread *thread = vm->GetAssociatedJSThread();
            return EcmaStringAccessor(EcmaString::FromBaseString(foundString))
                .EqualToSplicedString(thread, firstStr, secondStr);
        });
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

// This should only call in Debugger Signal, and need to fix and remove
EcmaString* EcmaStringTableImpl::GetOrInternStringThreadUnsafe(EcmaVM* vm, const uint8_t* utf8Data,
                                                               uint32_t utf8Len, bool canBeCompress)
{
    STRING_TABLE_OPERATION_SCOPE();
    ASSERT(vm->GetJsDebuggerManager()->GetSignalState());
    uint32_t hashcode = EcmaStringAccessor::ComputeHashcodeUtf8(utf8Data, utf8Len, canBeCompress);
    JSThread* thread = vm->GetJSThread();
    BaseString *result = chainedHashMapOperation.template LoadOrStore<false>(
        thread, hashcode,
        [vm, utf8Data, utf8Len, canBeCompress, hashcode]() {
            EcmaString *value = EcmaStringAccessor::CreateFromUtf8(vm, utf8Data, utf8Len, canBeCompress,
                                                                   MemSpaceType::SHARED_OLD_SPACE);
            value->SetMixHashcode(hashcode);
            JSThread *thread = vm->GetJSThread();
            JSHandle<EcmaString> stringHandle(thread, value);
            return stringHandle;
        },
        [vm, utf8Data, utf8Len, canBeCompress](BaseString *foundString) {
            JSThread *thread = vm->GetAssociatedJSThread();
            return EcmaStringAccessor::StringIsEqualUint8Data(thread, EcmaString::FromBaseString(foundString), utf8Data,
                                                              utf8Len, canBeCompress);
        });
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

EcmaString* EcmaStringTable::GetOrInternFlattenString(EcmaVM* vm, EcmaString* string)
{
    return impl_.GetOrInternFlattenString(vm, string);
}

EcmaString* EcmaStringTable::GetOrInternFlattenStringNoGC(EcmaVM* vm, EcmaString* string)
{
    return impl_.GetOrInternFlattenStringNoGC(vm, string);
}

EcmaString* EcmaStringTable::GetOrInternStringFromCompressedSubString(EcmaVM* vm, const JSHandle<EcmaString>& string,
                                                                      uint32_t offset, uint32_t utf8Len)
{
    return impl_.GetOrInternStringFromCompressedSubString(vm, string, offset, utf8Len);
}

EcmaString* EcmaStringTable::GetOrInternString(EcmaVM* vm, EcmaString* string)
{
    return impl_.GetOrInternString(vm, string);
}

EcmaString* EcmaStringTable::GetOrInternString(EcmaVM* vm, const JSHandle<EcmaString>& firstString,
                                               const JSHandle<EcmaString>& secondString)
{
    return impl_.GetOrInternString(vm, firstString, secondString);
}

EcmaString* EcmaStringTable::GetOrInternString(EcmaVM* vm, const uint8_t* utf8Data, uint32_t utf8Len,
                                               bool canBeCompress,
                                               MemSpaceType type)
{
    return impl_.GetOrInternString(vm, utf8Data, utf8Len, canBeCompress, type);
}

EcmaString* EcmaStringTable::GetOrInternString(EcmaVM* vm, const uint8_t* utf8Data, uint32_t utf16Len,
                                               MemSpaceType type)
{
    return impl_.GetOrInternString(vm, utf8Data, utf16Len, type);
}

EcmaString* EcmaStringTable::GetOrInternString(EcmaVM* vm, const uint16_t* utf16Data, uint32_t utf16Len,
                                               bool canBeCompress)
{
    return impl_.GetOrInternString(vm, utf16Data, utf16Len, canBeCompress);
}

// This is ONLY for JIT Thread, since JIT could not create JSHandle so need to allocate String with holding
// lock_ --- need to support JSHandle
EcmaString* EcmaStringTable::GetOrInternStringWithoutJSHandleForJit(EcmaVM* vm, const uint8_t* utf8Data,
                                                                    uint32_t utf16Len,
                                                                    MemSpaceType type)
{
    return impl_.GetOrInternStringWithoutJSHandleForJit(vm, utf8Data, utf16Len, type);
}

EcmaString* EcmaStringTable::GetOrInternStringWithoutJSHandleForJit(EcmaVM* vm, const uint8_t* utf8Data,
                                                                    uint32_t utf8Len,
                                                                    bool canBeCompress, MemSpaceType type)
{
    return impl_.GetOrInternStringWithoutJSHandleForJit(vm, utf8Data, utf8Len, canBeCompress, type);
}

EcmaString *EcmaStringTable::TryGetInternString(JSThread *thread, const JSHandle<EcmaString> &string)
{
    return impl_.TryGetInternString(thread, string);
}

void EcmaStringTable::SweepWeakRef(const WeakRootVisitor& visitor, uint32_t partitionID)
{
    impl_.SweepWeakRef(visitor, partitionID);
}

void EcmaStringTable::ConcurrentSweepWeakRef(const WeakRootVisitor& visitor, uint32_t partitionID,
                                             std::vector<ChainedHashMapEntry*>& waitDeleteEntries)
{
    impl_.ConcurrentSweepWeakRef(visitor, partitionID, waitDeleteEntries);
}

bool EcmaStringTable::CheckStringTableValidity(JSThread *thread)
{
    return impl_.CheckStringTableValidity(thread);
}

EcmaString* EcmaStringTable::GetOrInternStringThreadUnsafe(EcmaVM* vm, const JSHandle<EcmaString> firstString,
                                                           const JSHandle<EcmaString> secondString)
{
    return impl_.GetOrInternStringThreadUnsafe(vm, firstString, secondString);
}

// This should only call in Debugger Signal, and need to fix and remove
EcmaString* EcmaStringTable::GetOrInternStringThreadUnsafe(EcmaVM* vm, const uint8_t* utf8Data, uint32_t utf8Len,
                                                           bool canBeCompress)
{
    return impl_.GetOrInternStringThreadUnsafe(vm, utf8Data, utf8Len, canBeCompress);
}

void EcmaStringTable::TransferToNativeAndWaitSweepWeakRefTaskFinished(JSThread *thread)
{
    panda::ecmascript::ThreadNativeScope scope(thread);
    cleaner_->WaitSweepWeakRefTaskFinished();
}
}  // namespace panda::ecmascript
