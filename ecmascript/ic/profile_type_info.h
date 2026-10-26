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

#ifndef ECMASCRIPT_IC_PROFILE_TYPE_INFO_H
#define ECMASCRIPT_IC_PROFILE_TYPE_INFO_H

#include "ecmascript/ic/ic_info.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_tagged_value_wrapper.h"
#include "ecmascript/pgo_profiler/pgo_extra_profiler.h"
#include "ecmascript/tagged_array.h"
#include "ecmascript/tagged_dictionary.h"

namespace panda::ecmascript {

/**
 *              ProfileTypeInfo  (each row = 64 bits / 8 bytes)
 *
 *      +0x00 | TaggedStateWord
 *      +0x08 | length(32)    |  invocation(32)
 *      +0x10 | period(32)    | padding(32)
 *      +0x18 | method
 *      +0x20 | extra_info_map (JSTaggedValue)
 *      +0x28 | jit_osr (JSTaggedValue)
 *      +0x30 | ic_slot[0..length-1] (JSTaggedValue, variable)
 */
class ProfileTypeInfo : public TaggedObject {
public:
    static const uint32_t MAX_FUNC_CACHE_INDEX = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t INVALID_SLOT_INDEX = 0xFF;
    static constexpr uint32_t MAX_SLOT_INDEX = 0xFFFF;

    static constexpr size_t LENGTH_OFFSET = TaggedObjectSize();

    ACCESSORS_PRIMITIVE_FIELD(Length, uint32_t, LENGTH_OFFSET, INVOCATION_COUNT_OFFSET)
    ACCESSORS_PRIMITIVE_FIELD(InvocationCount, int32_t, INVOCATION_COUNT_OFFSET, PERIOD_INDEX_OFFSET)
    ACCESSORS_PRIMITIVE_FIELD(PeriodIndex, uint32_t, PERIOD_INDEX_OFFSET, PADDING_OFFSET)
    ACCESSORS_PRIMITIVE_FIELD(Padding, uint32_t, PADDING_OFFSET, METHOD_OFFSET)
    ACCESSORS(Method, METHOD_OFFSET, EXTRA_INFO_MAP_OFFSET)

    ACCESSORS(ExtraInfoMap, EXTRA_INFO_MAP_OFFSET, JIT_OSR_OFFSET)
    ACCESSORS(JitOsr, JIT_OSR_OFFSET, SIZE)

    static constexpr size_t DATA_OFFSET = SIZE;
    static constexpr uint32_t HEADER_TAGGED_SLOTS =
        (SIZE - METHOD_OFFSET) / JSTaggedValue::TaggedTypeSize();

    static constexpr size_t INITIAL_PERIOD_INDEX = 0;
    static constexpr size_t PRE_DUMP_PERIOD_INDEX = 1;
    static constexpr size_t DUMP_PERIOD_INDEX = 2;
    static constexpr size_t BIG_METHOD_PERIOD_INDEX = 3;

    static ProfileTypeInfo *Cast(TaggedObject *object)
    {
        ASSERT(JSTaggedValue(object).IsProfileTypeInfo());
        return static_cast<ProfileTypeInfo *>(object);
    }

    static size_t ComputeSize(uint32_t icSlotSize)
    {
        return SIZE + JSTaggedValue::TaggedTypeSize() * AdjustSlotSize(icSlotSize);
    }

    JSTaggedType *GetData() const
    {
        return reinterpret_cast<JSTaggedType *>(ToUintPtr(this) + DATA_OFFSET);
    }

    static inline uint32_t AdjustSlotSize(uint32_t icSlotSize)
    {
        // if ic slot size is 0xff comes from frontend, it means the actual size is 0x100.
        // 0xff is a invalid slot index, which value is hole.
        if (icSlotSize == INVALID_SLOT_INDEX) {
            ++icSlotSize;
        }
        return icSlotSize;
    }

    inline uint32_t GetICSlotLength() const
    {
        return GetLength();
    }

    inline JSTaggedValue GetICSlot(const JSThread *thread, uint32_t idx) const
    {
        ASSERT(idx < GetICSlotLength());
        size_t offset = JSTaggedValue::TaggedTypeSize() * idx;
        return JSTaggedValue(Barriers::GetTaggedValue(thread,
            reinterpret_cast<JSTaggedType *>(ToUintPtr(this)), DATA_OFFSET + offset));
    }

    inline void SetICSlot(const JSThread *thread, uint32_t idx, const JSTaggedValue &value)
    {
        ASSERT(idx < GetICSlotLength());
        size_t offset = JSTaggedValue::TaggedTypeSize() * idx;
        if (value.IsHeapObject()) {
            Barriers::SetObject<true>(thread, this, DATA_OFFSET + offset, value.GetRawData());
        } else {
            Barriers::SetPrimitive<JSTaggedType>(
                reinterpret_cast<JSTaggedType *>(ToUintPtr(this)), DATA_OFFSET + offset, value.GetRawData());
        }
    }

    template<typename T>
    inline void SetICSlot(const JSThread *thread, uint32_t idx, const JSHandle<T> &value)
    {
        SetICSlot(thread, idx, value.GetTaggedValue());
    }

    inline void SetPrimitiveOfSlot(JSTaggedValue initValue, uint32_t icSlotSize)
    {
        for (uint32_t i = 0; i < icSlotSize; i++) {
            size_t offset = JSTaggedValue::TaggedTypeSize() * i;
            if (i == INVALID_SLOT_INDEX) {
                Barriers::SetPrimitive<JSTaggedType>(GetData(), offset, JSTaggedValue::Hole().GetRawData());
            } else {
                Barriers::SetPrimitive<JSTaggedType>(GetData(), offset, initValue.GetRawData());
            }
        }
    }

    inline void SetSpecialValue()
    {
        SetPeriodIndex(INITIAL_PERIOD_INDEX);
        SetInvocationCount(0);
    }

    inline void InitializeExtraInfoMap()
    {
        SetExtraInfoMap<SKIP_BARRIER>(nullptr, JSTaggedValue::Undefined());
    }

    inline void InitializeJitOsr()
    {
        SetJitOsr<SKIP_BARRIER>(nullptr, JSTaggedValue::Undefined());
    }

    inline void InitializeWithSpecialValue(JSTaggedValue initValue, uint32_t icSlotSize)
    {
        ASSERT(initValue.IsSpecial());
        icSlotSize = AdjustSlotSize(icSlotSize);
        SetLength(icSlotSize);
        SetPrimitiveOfSlot(initValue, icSlotSize);
        InitializeExtraInfoMap();
        InitializeJitOsr();
        SetSpecialValue();
    }

    bool IsProfileTypeInfoInit() const
    {
        return GetPeriodIndex() == INITIAL_PERIOD_INDEX;
    }

    void SetPreDumpPeriodIndex()
    {
        SetPeriodIndex(PRE_DUMP_PERIOD_INDEX);
    }

    bool IsProfileTypeInfoPreDumped() const
    {
        return GetPeriodIndex() == PRE_DUMP_PERIOD_INDEX;
    }

    bool IsProfileTypeInfoDumped() const
    {
        return GetPeriodIndex() == DUMP_PERIOD_INDEX;
    }

    void SetBigMethodPeriodIndex()
    {
        SetPeriodIndex(BIG_METHOD_PERIOD_INDEX);
    }

    bool IsProfileTypeInfoWithBigMethod() const
    {
        return GetPeriodIndex() == BIG_METHOD_PERIOD_INDEX;
    }

    // ExtraInfoMap helpers
    static JSHandle<NumberDictionary> CreateOrGetExtraInfoMap(const JSThread *thread,
                                                              JSHandle<ProfileTypeInfo> profileTypeInfo)
    {
        if (profileTypeInfo->GetExtraInfoMap(thread).IsUndefined()) {
            JSHandle<NumberDictionary> dictJShandle = NumberDictionary::Create(thread);
            profileTypeInfo->SetExtraInfoMap(thread, dictJShandle);
            return dictJShandle;
        }
        JSHandle<NumberDictionary> dictJShandle(thread, profileTypeInfo->GetExtraInfoMap(thread));
        return dictJShandle;
    }

    static void UpdateExtraInfoMap(const JSThread *thread, JSHandle<NumberDictionary> dictJShandle,
                            JSHandle<JSTaggedValue> key, JSHandle<JSTaggedValue> receiverHClassHandle,
                            JSHandle<JSTaggedValue> holderHClassHandle,
                            JSHandle<ProfileTypeInfo> profileTypeInfo)
    {
        JSHandle<JSTaggedValue> info(thread->GetEcmaVM()->GetFactory()->NewExtraProfileTypeInfo());
        JSHandle<ExtraProfileTypeInfo> infoHandle(info);
        infoHandle->SetReceiver(thread, receiverHClassHandle.GetTaggedValue().CreateAndGetWeakRef());
        infoHandle->SetHolder(thread, holderHClassHandle.GetTaggedValue());
        JSHandle<NumberDictionary> dict = NumberDictionary::PutIfAbsent(thread,
                                                                        dictJShandle,
                                                                        key,
                                                                        info,
                                                                        PropertyAttributes::Default());
        profileTypeInfo->SetExtraInfoMap(thread, dict);
    }

    DECL_VISIT_ARRAY(METHOD_OFFSET, GetLength() + HEADER_TAGGED_SLOTS, GetLength() + HEADER_TAGGED_SLOTS)

    DECL_DUMP()
};

// ProfileTypeInfoNexus: IC state machine for ProfileTypeInfo (mirrors IcAccessor for ICInfo).
// Uses ProfileTypeInfo's native GetICSlot/SetICSlot instead of TaggedArray-based ICInfo.
class ProfileTypeInfoNexus {
public:
    static constexpr size_t CACHE_MAX_LEN = 8;
    static constexpr size_t MONO_CASE_NUM = 2;
    static constexpr size_t POLY_CASE_NUM = 4;

    enum ICState { UNINIT, MONO, POLY, IC_MEGA, MEGA };
#if ECMASCRIPT_ENABLE_TRACE_LOAD
    enum MegaState { NONE, NOTFOUND_MEGA, DICT_MEGA };
#endif

    ProfileTypeInfoNexus() = default;
    ProfileTypeInfoNexus(JSThread *thread, JSHandle<ProfileTypeInfo> pti, uint32_t slotId, ICKind kind);
    ~ProfileTypeInfoNexus() = default;

    ICState GetMegaState() const;
    ICState GetICState() const;
    static std::string ICStateToString(ICState state);

    void AddHandlerWithoutKey(JSHandle<JSTaggedValue> hclass, JSHandle<JSTaggedValue> handler,
                              JSHandle<JSTaggedValue> keyForMegaIC = JSHandle<JSTaggedValue>(),
                              MegaICCache::MegaICKind kind = MegaICCache::MegaICKind::None) const;
    void AddElementHandler(JSHandle<JSTaggedValue> hclass, JSHandle<JSTaggedValue> handler) const;
    void AddHandlerWithKey(JSHandle<JSTaggedValue> key, JSHandle<JSTaggedValue> hclass,
                           JSHandle<JSTaggedValue> handler) const;
    void AddGlobalHandlerKey(JSHandle<JSTaggedValue> key, JSHandle<JSTaggedValue> handler) const;
    void AddGlobalRecordHandler(JSHandle<JSTaggedValue> handler) const;

    void SetAsMega() const;
    void SetAsMegaIfUndefined() const;
    void SetAsMegaForTraceSlowMode(ObjectOperator &op) const;
    void SetAsMegaForTrace(JSTaggedValue value) const;

    JSTaggedValue GetWeakRef(JSTaggedValue value) const
    {
        return JSTaggedValue(value.CreateAndGetWeakRef());
    }
    JSTaggedValue GetRefFromWeak(const JSTaggedValue &value) const
    {
        return JSTaggedValue(value.GetWeakReferent());
    }

    ICKind GetKind() const { return kind_; }
    uint32_t GetSlotId() const { return slotId_; }

private:
    void AddWithoutKeyPoly(JSHandle<JSTaggedValue> hclass, JSHandle<JSTaggedValue> handler,
                           uint32_t index, JSTaggedValue profileData,
                           JSHandle<JSTaggedValue> keyForMegaIC = JSHandle<JSTaggedValue>(),
                           MegaICCache::MegaICKind kind = MegaICCache::MegaICKind::None) const;

    inline void SetMultiIcSlotLocked(uint32_t firstIdx, const JSTaggedValue &firstValue,
                                     uint32_t secondIdx, const JSTaggedValue &secondValue) const;

    JSThread *thread_;
    JSHandle<ProfileTypeInfo> pti_;
    uint32_t slotId_;
    ICKind kind_;
    bool enableICMega_;
};

}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_IC_PROFILE_TYPE_INFO_H
