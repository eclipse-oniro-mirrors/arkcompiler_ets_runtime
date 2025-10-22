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

#include "ecmascript/string/base_string_table.h"

#include "common_components/base/globals.h"
#include "common_components/taskpool/taskpool.h"
#include "common_components/mutator/thread_local.h"
#include "common_interfaces/thread/thread_holder.h"
#include "common_interfaces/thread/thread_state_transition.h"
#include "ecmascript/runtime.h"
#include "ecmascript/string/base_string.h"
#include "ecmascript/string/base_string-inl.h"
#include "ecmascript/string/composite_base_class.h"
#include "ecmascript/string/hashtriemap.h"
#include "ecmascript/string/hashtriemap-inl.h"
#include "ecmascript/string/external_string.h"
#include "ecmascript/string/external_string-inl.h"
#include "ecmascript/string/line_string.h"
#include "ecmascript/string/line_string-inl.h"
#include "ecmascript/string/tree_string.h"
#include "ecmascript/string/tree_string-inl.h"
#include "ecmascript/string/sliced_string.h"
#include "ecmascript/string/sliced_string-inl.h"
#include "ecmascript/string/string_table_internal.h"
#include "heap/heap_allocator.h"

namespace panda::ecmascript {
template <bool ConcurrentSweep>
BaseString* BaseStringTableInternal<ConcurrentSweep>::AllocateLineStringObject(size_t size)
{
    size = common::AlignmentUp(size, common::ALIGN_OBJECT);
    BaseString* str =
        reinterpret_cast<BaseString*>(common::HeapAllocator::AllocateInOldOrHuge(size, common::LanguageType::DYNAMIC));
    BaseStringClass* cls = Runtime::GetInstance()->GetBaseClassRoots().GetBaseClass(EcmaStringType::LINE_STRING);
    str->SetFullBaseClassWithoutBarrier(cls);
    return str;
}

template <bool ConcurrentSweep>
BaseString* BaseStringTableInternal<ConcurrentSweep>::GetOrInternFlattenString(
    common::ThreadHolder* holder, const HandleCreator& handleCreator,
    BaseString* string)
{
    DCHECK_CC(!string->IsTreeString());
    if (string->IsInternString()) {
        return string;
    }
    auto readBarrier = [](void* obj, size_t offset)-> common::BaseObject* {
        return common::BaseObject::Cast(
            reinterpret_cast<common::MAddress>(common::BaseRuntime::ReadBarrier(
                obj, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(obj) + offset))));
    };
    uint32_t hashcode = string->GetHashcode(readBarrier);
    // Strings in string table should not be in the young space.
    auto loadResult = stringTable_.template Load(readBarrier, hashcode, string);
    if (loadResult.value != nullptr) {
        return loadResult.value;
    }
    common::ReadOnlyHandle<BaseString> stringHandle = handleCreator(holder, string);
    BaseString* result = stringTable_.template StoreOrLoad(
        holder, readBarrier, hashcode, loadResult, stringHandle);
    DCHECK_CC(result != nullptr);
    return result;
}

template <bool ConcurrentSweep>
BaseString* BaseStringTableInternal<ConcurrentSweep>::GetOrInternStringFromCompressedSubString(
    common::ThreadHolder* holder,
    const HandleCreator& handleCreator,
    const common::ReadOnlyHandle<BaseString>& string,
    uint32_t offset, uint32_t utf8Len)
{
    const uint8_t* utf8Data = common::ReadOnlyHandle<LineString>::Cast(string)->GetDataUtf8() + offset;
    uint32_t hashcode = BaseString::ComputeHashcodeUtf8(utf8Data, utf8Len, true);
    auto readBarrier = [](void* obj, size_t offset)-> common::BaseObject* {
        return common::BaseObject::Cast(
            reinterpret_cast<common::MAddress>(common::BaseRuntime::ReadBarrier(
                obj, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(obj) + offset))));
    };
    auto loadResult = stringTable_.template Load(readBarrier, hashcode, string, offset, utf8Len);
    if (loadResult.value != nullptr) {
        return loadResult.value;
    }
    auto allocator = [](size_t size, EcmaStringType type)-> BaseString* {
        DCHECK_CC(type == EcmaStringType::LINE_STRING);
        return AllocateLineStringObject(size);
    };
    BaseString* result = stringTable_.template StoreOrLoad(
        holder, hashcode, loadResult,
        [holder, string, offset, utf8Len, hashcode, handleCreator, allocator]() {
            BaseString* str = LineString::CreateFromUtf8CompressedSubString(
                std::move(allocator), string, offset, utf8Len);
            str->SetMixHashcode(hashcode);
            DCHECK_CC(!str->IsInternString());
            DCHECK_CC(!str->IsTreeString());
            // Strings in string table should not be in the young space.
            common::ReadOnlyHandle<BaseString> strHandle = handleCreator(holder, str);
            return strHandle;
        },
        [utf8Len, string, offset](const BaseString* foundString) {
            const uint8_t* utf8Data = common::ReadOnlyHandle<LineString>::Cast(string)->GetDataUtf8() + offset;
            auto readBarrier = [](void* obj, size_t offset)-> common::BaseObject* {
                return common::BaseObject::Cast(
                    reinterpret_cast<common::MAddress>(common::BaseRuntime::ReadBarrier(
                        obj, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(obj) + offset))));
            };
            return BaseString::StringIsEqualUint8Data(readBarrier, foundString, utf8Data, utf8Len, true);
        });
    DCHECK_CC(result != nullptr);
    return result;
}

template <bool ConcurrentSweep>
BaseString* BaseStringTableInternal<ConcurrentSweep>::GetOrInternString(common::ThreadHolder* holder,
                                                                        const HandleCreator& handleCreator,
                                                                        const uint8_t* utf8Data,
                                                                        uint32_t utf8Len,
                                                                        bool canBeCompress)
{
    uint32_t hashcode = BaseString::ComputeHashcodeUtf8(utf8Data, utf8Len, canBeCompress);
    auto allocator = [](size_t size, EcmaStringType type)-> BaseString* {
        DCHECK_CC(type == EcmaStringType::LINE_STRING);
        return AllocateLineStringObject(size);
    };
    BaseString* result = stringTable_.template LoadOrStore<true>(
        holder, hashcode,
        [holder, hashcode, utf8Data, utf8Len, canBeCompress, handleCreator, allocator]() {
            BaseString* value = LineString::CreateFromUtf8(std::move(allocator), utf8Data, utf8Len, canBeCompress);
            value->SetMixHashcode(hashcode);
            DCHECK_CC(!value->IsInternString());
            DCHECK_CC(!value->IsTreeString());
            common::ReadOnlyHandle<BaseString> stringHandle = handleCreator(holder, value);
            return stringHandle;
        },
        [utf8Data, utf8Len, canBeCompress](BaseString* foundString) {
            auto readBarrier = [](void* obj, size_t offset)-> common::BaseObject* {
                return common::BaseObject::Cast(
                    reinterpret_cast<common::MAddress>(common::BaseRuntime::ReadBarrier(
                        obj, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(obj) + offset))));
            };
            return BaseString::StringIsEqualUint8Data(readBarrier, foundString, utf8Data, utf8Len,
                                                      canBeCompress);
        });
    DCHECK_CC(result != nullptr);
    return result;
}

template <bool ConcurrentSweep>
BaseString* BaseStringTableInternal<ConcurrentSweep>::GetOrInternString(
    common::ThreadHolder* holder, const HandleCreator& handleCreator,
    const uint16_t* utf16Data, uint32_t utf16Len,
    bool canBeCompress)
{
    uint32_t hashcode = BaseString::ComputeHashcodeUtf16(const_cast<uint16_t*>(utf16Data), utf16Len);
    auto allocator = [](size_t size, EcmaStringType type)-> BaseString* {
        DCHECK_CC(type == EcmaStringType::LINE_STRING);
        return AllocateLineStringObject(size);
    };
    BaseString* result = stringTable_.template LoadOrStore<true>(
        holder, hashcode,
        [holder, utf16Data, utf16Len, canBeCompress, hashcode, handleCreator, allocator]() {
            BaseString* value = LineString::CreateFromUtf16(std::move(allocator), utf16Data, utf16Len,
                                                            canBeCompress);
            value->SetMixHashcode(hashcode);
            DCHECK_CC(!value->IsInternString());
            DCHECK_CC(!value->IsTreeString());
            // Strings in string table should not be in the young space.
            common::ReadOnlyHandle<BaseString> stringHandle = handleCreator(holder, value);
            return stringHandle;
        },
        [utf16Data, utf16Len](BaseString* foundString) {
            auto readBarrier = [](void* obj, size_t offset)-> common::BaseObject* {
                return common::BaseObject::Cast(
                    reinterpret_cast<common::MAddress>(common::BaseRuntime::ReadBarrier(
                        obj, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(obj) + offset))));
            };
            return BaseString::StringsAreEqualUtf16(readBarrier, foundString, utf16Data, utf16Len);
        });
    DCHECK_CC(result != nullptr);
    return result;
}

template <bool ConcurrentSweep>
BaseString* BaseStringTableInternal<ConcurrentSweep>::TryGetInternString(
    const common::ReadOnlyHandle<BaseString>& string)
{
    auto readBarrier = [](void* obj, size_t offset)-> common::BaseObject* {
        return common::BaseObject::Cast(
            reinterpret_cast<common::MAddress>(common::BaseRuntime::ReadBarrier(
                obj, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(obj) + offset))));
    };
    uint32_t hashcode = string->GetHashcode(readBarrier);
    return stringTable_.template Load<false>(readBarrier, hashcode, *string);
}

template <bool ConcurrentSweep>
template <bool B, std::enable_if_t<B, int>>
void BaseStringTableInternal<ConcurrentSweep>::SweepWeakRef(const WeakRefFieldVisitor& visitor, uint32_t rootID,
                                                            std::vector<HashTrieMapEntry*>& waitDeleteEntries)
{
    DCHECK_CC(rootID >= 0 && rootID < TrieMapConfig::ROOT_SIZE);
    auto rootNode = stringTable_.root_[rootID].load(std::memory_order_relaxed);
    if (rootNode == nullptr) {
        return;
    }
    for (uint32_t index = 0; index < TrieMapConfig::INDIRECT_SIZE; ++index) {
        stringTable_.ClearNodeFromGC(rootNode, index, visitor, waitDeleteEntries);
    }
}

template <bool ConcurrentSweep>
template <bool B, std::enable_if_t<B, int>>
void BaseStringTableInternal<ConcurrentSweep>::CleanUp()
{
    stringTable_.CleanUp();
}

template <bool ConcurrentSweep>
template <bool B, std::enable_if_t<!B, int>>
void BaseStringTableInternal<ConcurrentSweep>::SweepWeakRef(const WeakRefFieldVisitor& visitor)
{
    // No need lock here, only shared gc will sweep string table, meanwhile other threads are suspended.
    for (uint32_t rootID = 0; rootID < TrieMapConfig::ROOT_SIZE; ++rootID) {
        auto rootNode = stringTable_.root_[rootID].load(std::memory_order_relaxed);
        if (rootNode == nullptr) {
            continue;
        }
        for (uint32_t index = 0; index < TrieMapConfig::INDIRECT_SIZE; ++index) {
            stringTable_.ClearNodeFromGC(rootNode, index, visitor);
        }
    }
}

template void BaseStringTableInternal<false>::SweepWeakRef<false>(const WeakRefFieldVisitor& visitor);

BaseString* BaseStringTableImpl::GetOrInternFlattenString(common::ThreadHolder* holder,
                                                          const HandleCreator& handleCreator, BaseString* string)
{
    return stringTable_.GetOrInternFlattenString(holder, handleCreator, string);
}

BaseString* BaseStringTableImpl::GetOrInternStringFromCompressedSubString(
    common::ThreadHolder* holder, const HandleCreator& handleCreator,
    const common::ReadOnlyHandle<BaseString>& string, uint32_t offset, uint32_t utf8Len)
{
    return stringTable_.GetOrInternStringFromCompressedSubString(holder, handleCreator, string, offset, utf8Len);
}

BaseString* BaseStringTableImpl::GetOrInternString(common::ThreadHolder* holder, const HandleCreator& handleCreator,
                                                   const uint8_t* utf8Data, uint32_t utf8Len, bool canBeCompress)
{
    return stringTable_.GetOrInternString(holder, handleCreator, utf8Data, utf8Len, canBeCompress);
}

BaseString* BaseStringTableImpl::GetOrInternString(common::ThreadHolder* holder, const HandleCreator& handleCreator,
                                                   const uint16_t* utf16Data, uint32_t utf16Len, bool canBeCompress)
{
    return stringTable_.GetOrInternString(holder, handleCreator, utf16Data, utf16Len, canBeCompress);
}

BaseString* BaseStringTableImpl::TryGetInternString(const common::ReadOnlyHandle<BaseString>& string)
{
    return stringTable_.TryGetInternString(string);
}

void BaseStringTableCleaner::StartSweepWeakRefTask()
{
    // No need lock here, only the daemon thread will reset the state.
    sweepWeakRefFinished_ = false;
    PendingTaskCount_.store(TrieMapConfig::ROOT_SIZE, std::memory_order_relaxed);
}

void BaseStringTableCleaner::WaitSweepWeakRefTask()
{
    common::LockHolder holder(sweepWeakRefMutex_);
    while (!sweepWeakRefFinished_) {
        sweepWeakRefCV_.Wait(&sweepWeakRefMutex_);
    }
}

void BaseStringTableCleaner::SignalSweepWeakRefTask()
{
    common::LockHolder holder(sweepWeakRefMutex_);
    sweepWeakRefFinished_ = true;
    sweepWeakRefCV_.SignalAll();
}


void BaseStringTableCleaner::PostSweepWeakRefTask(const WeakRefFieldVisitor &visitor)
{
    StartSweepWeakRefTask();
    stringTable_->GetHashTrieMap().StartSweeping();
    iter_ = std::make_shared<std::atomic<uint32_t>>(0U);
    const uint32_t postTaskCount =
        common::Taskpool::GetCurrentTaskpool()->GetTotalThreadNum();
    for (uint32_t i = 0U; i < postTaskCount; ++i) {
        common::Taskpool::GetCurrentTaskpool()->PostTask(
            std::make_unique<CMCSweepWeakRefTask>(iter_, this, visitor));
    }
}

void BaseStringTableCleaner::JoinAndWaitSweepWeakRefTask(
    const WeakRefFieldVisitor &visitor)
{
    ProcessSweepWeakRef(iter_, this, visitor);
    WaitSweepWeakRefTask();
    iter_.reset();
    stringTable_->GetHashTrieMap().FinishSweeping();
}

void BaseStringTableCleaner::CleanUp()
{
    for (std::vector<HashTrieMapEntry*>& waitFrees : waitFreeEntries_) {
        for (const HashTrieMapEntry* entry : waitFrees) {
            delete entry;
        }
        waitFrees.clear();
    }
    stringTable_->CleanUp();
}

void BaseStringTableCleaner::ProcessSweepWeakRef(
    IteratorPtr &iter, BaseStringTableCleaner *cleaner,
    const WeakRefFieldVisitor &visitor)
{
    uint32_t index = 0U;
    while ((index = GetNextIndexId(iter)) < TrieMapConfig::ROOT_SIZE) {
        cleaner->waitFreeEntries_[index].clear();
        cleaner->stringTable_->SweepWeakRef(visitor, index, cleaner->waitFreeEntries_[index]);
        if (ReduceCountAndCheckFinish(cleaner)) {
            cleaner->SignalSweepWeakRefTask();
        }
    }
}

bool BaseStringTableCleaner::CMCSweepWeakRefTask::Run([[maybe_unused]] uint32_t threadIndex)
{
    common::ThreadLocal::SetThreadType(common::ThreadType::GC_THREAD);
    ProcessSweepWeakRef(iter_, cleaner_, visitor_);
    common::ThreadLocal::SetThreadType(common::ThreadType::ARK_PROCESSOR);
    common::ThreadLocal::ClearAllocBufferRegion();
    return true;
}
}
