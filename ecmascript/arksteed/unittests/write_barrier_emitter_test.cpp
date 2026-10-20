/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#include "ecmascript/arksteed/arksteed_write_barrier.h"
#include "ecmascript/arksteed/arksteed_assembler-inl.h"
#include <vector>
#include "ecmascript/js_hclass.h"
#include "ecmascript/mem/chunk.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/mem/region.h"
#include "ecmascript/platform/map.h"
#include "ecmascript/tests/test_helper.h"

namespace panda::ecmascript::arksteed {

using panda::test::BaseTestWithScope;

// Unit domain for matrix row G8.2u: the barrier branch selection of
// ArkSteedWriteBarrierEmitter. The emitter is driven directly over a real
// ArkSteedAssembler; the contract under test is the documented branch structure
// of arksteed_write_barrier.cpp (C13 of `arksteed-cpp-unit-test-domains`):
//
//   * NO_BARRIER and a non-heap value kind must emit exactly the plain store;
//   * a heap value kind with a barrier kind must append the post-store barrier;
//   * an unknown value kind must add the tag/region check in front of the same
//     single post-store barrier.
//
// Emission tests compare code lengths and deferred-block counts, including the
// deferred bodies. Execution tests check the conservative filter with synthetic
// region metadata and sentinel deferred bodies; they never invoke a GC helper.
//
// Every barrier path reads the glue address and the runtime stub table from the
// entry thread (arksteed_assembler-inl.h), so fixture C uses a real JSThread;
// a null thread would trip the production ASSERT in Debug and make the case red.

namespace {

// Result of one cold emission: number of emitted bytes and number of queued
// deferred blocks. Both are byte-independent structural signals (D4).
struct Emission {
    size_t codeSize {0};
    size_t deferredBlocks {0};
    size_t totalCodeSize {0};
};

}  // namespace

class ArkSteedWriteBarrierEmitterTest : public BaseTestWithScope<false> {
protected:
    using Kind = ArkSteedWriteBarrierKind;
    using ValueKind = ArkSteedWriteBarrierValueKind;

    static constexpr int32_t FIELD_OFFSET = 8;

    // Inputs and scratch registers must stay out of each other's way.
    static ArkSteedRegister Glue()
    {
        return ArkSteedAssembler::GetParameterRegister(0);
    }
    static ArkSteedRegister Object()
    {
        return ArkSteedAssembler::GetParameterRegister(1);
    }
    static ArkSteedRegister Value()
    {
        return ArkSteedAssembler::GetParameterRegister(2);  // 2: value parameter
    }
    static ArkSteedRegister ObjectScratch()
    {
        return ArkSteedAssembler::GetParameterRegister(3);  // 3: object scratch parameter
    }
    static ArkSteedRegister ValueScratch()
    {
        return ArkSteedAssembler::GetParameterRegister(4);  // 4: value scratch parameter
    }

    // StoreTaggedElement pins glue/object/byteOffset/value to the first four
    // parameter registers with in-function ASSERTs; the tag scratch is free.
    static ArkSteedRegister ElementValue()
    {
        return ArkSteedAssembler::GetParameterRegister(3);  // 3: element value parameter
    }
    static ArkSteedRegister ElementScratch()
    {
        return ArkSteedAssembler::GetParameterRegister(4);  // 4: element scratch parameter
    }

    // One cold emission fixture (fixture C, H4). The chunk, deferred list and
    // assembler live for exactly one emission, so two emissions are always
    // compared on equal footing.
    class ColdEmit {
    public:
        explicit ColdEmit(JSThread *entryThread)
            : assembler_(&chunk_, nullptr, entryThread),
              emitter_(&assembler_, &chunk_, &deferred_, snapshot_)
        {
            assembler_.EnableComments(true);
        }

        ArkSteedAssembler &Asm()
        {
            return assembler_;
        }

        ArkSteedWriteBarrierEmitter &Emitter()
        {
            return emitter_;
        }

        Emission Finish()
        {
            size_t mainCodeSize = assembler_.GetCodeSize();
            for (auto *block : deferred_) {
                assembler_.Bind(block->GetEntryLabel());
                block->Generate(&assembler_);
            }
            return {mainCodeSize, deferred_.size(), assembler_.GetCodeSize()};
        }

        // Execute the actual filter, replacing each deferred body with a
        // sentinel return. No GC helper sees the synthetic region or object.
        uint64_t RunFilter(uintptr_t glue, uintptr_t object, uintptr_t value)
        {
#if defined(PANDA_TARGET_AMD64)
            ArkSteedRegister result = x64::rax;
#elif defined(PANDA_TARGET_ARM64)
            ArkSteedRegister result = aarch64::x0;
#endif
            assembler_.Move(result, static_cast<int64_t>(0));
            assembler_.Return();
            for (auto *block : deferred_) {
                assembler_.Bind(block->GetEntryLabel());
                assembler_.Move(result, static_cast<int64_t>(1));
                assembler_.Return();
            }
            MemMap code = PageMap(PageSize(), PAGE_PROT_READWRITE);
            EXPECT_NE(code.GetMem(), nullptr);
            if (code.GetMem() == nullptr) {
                return UINT64_MAX;
            }
            errno_t copyStatus =
                memcpy_s(code.GetMem(), code.GetSize(), assembler_.GetCodeBuffer(), assembler_.GetCodeSize());
            EXPECT_EQ(copyStatus, EOK);
            if (copyStatus != EOK) {
                PageUnmap(code);
                return UINT64_MAX;
            }
            int status = PageProtect(code.GetMem(), code.GetSize(), PAGE_PROT_EXEC_READ);
            EXPECT_EQ(status, 0);
            if (status != 0) {
                PageUnmap(code);
                return UINT64_MAX;
            }
            auto *begin = static_cast<char *>(code.GetMem());
            __builtin___clear_cache(begin, begin + assembler_.GetCodeSize());
            using Filter = uint64_t (*)(uintptr_t, uintptr_t, uintptr_t);
            uint64_t called = reinterpret_cast<Filter>(code.GetMem())(glue, object, value);
            PageUnmap(code);
            return called;
        }

    private:
        NativeAreaAllocator native_;
        Chunk chunk_ {&native_};
        ArkSteedDeferredCodeList deferred_ {&chunk_};
        DeferredRegisterSnapshot snapshot_ {};
        ArkSteedAssembler assembler_;
        ArkSteedWriteBarrierEmitter emitter_;
    };

    // Reference for the scalar element store performed by the emitter's
    // NonHeap path: StoreField(value, object, byteOffset).
    Emission PlainElementStore()
    {
        ColdEmit fixture(thread);
        fixture.Asm().StoreField(ElementValue(), Object(), Value());
        return fixture.Finish();
    }

    Emission ElementStore(ValueKind valueKind)
    {
        ColdEmit fixture(thread);
        fixture.Emitter().StoreTaggedElement(Glue(), Object(), Value(), ElementValue(), ElementScratch(),
                                             valueKind);
        return fixture.Finish();
    }

    // Reference for the plain store performed by the emitter's NO_BARRIER and
    // NonHeap field paths: StoreField(value, object, offset).
    Emission PlainFieldStore(int32_t offset = FIELD_OFFSET)
    {
        ColdEmit fixture(thread);
        fixture.Asm().StoreField(Value(), Object(), offset);
        return fixture.Finish();
    }

    // Reference for the HClass store performed by TransitionHClass before its
    // post-store barrier: StoreInt32FieldRelease(hclass, object, HCLASS_OFFSET).
    Emission PlainHClassStore()
    {
        ColdEmit fixture(thread);
        fixture.Asm().StoreInt32FieldRelease(Value(), Object(), static_cast<int32_t>(TaggedObject::HCLASS_OFFSET));
        return fixture.Finish();
    }

    Emission FieldStore(Kind barrierKind, ValueKind valueKind)
    {
        ColdEmit fixture(thread);
        fixture.Emitter().StoreTaggedField(Glue(), Object(), Value(), FIELD_OFFSET, barrierKind, ObjectScratch(),
                                           ValueScratch(), valueKind);
        return fixture.Finish();
    }

    Emission TransitionHClassStore()
    {
        ColdEmit fixture(thread);
        fixture.Emitter().TransitionHClass(Glue(), Object(), Value(), ObjectScratch(), ValueScratch());
        return fixture.Finish();
    }
};

HWTEST_F_L0(ArkSteedWriteBarrierEmitterTest, FieldBarrierUsesOneCommonCall)
{
    for (Kind kind : {Kind::GENERIC_BARRIER, Kind::SHARED_BARRIER}) {
        for (ValueKind valueKind : {ValueKind::HeapObject, ValueKind::Unknown}) {
            Emission emitted = FieldStore(kind, valueKind);
            EXPECT_EQ(emitted.deferredBlocks, 1u);
            EXPECT_GT(emitted.totalCodeSize, emitted.codeSize);
        }
    }
    Emission heap = FieldStore(Kind::GENERIC_BARRIER, ValueKind::HeapObject);
    Emission unknown = FieldStore(Kind::GENERIC_BARRIER, ValueKind::Unknown);
    RecordProperty("heap_main_bytes", static_cast<int>(heap.codeSize));
    RecordProperty("heap_total_bytes", static_cast<int>(heap.totalCodeSize));
    RecordProperty("unknown_main_bytes", static_cast<int>(unknown.codeSize));
    RecordProperty("unknown_total_bytes", static_cast<int>(unknown.totalCodeSize));
}

#if !USE_STICKY_CMS_GC
HWTEST_F_L0(ArkSteedWriteBarrierEmitterTest, FieldConservativeFilter)
{
    MemMap region = PageMap(DEFAULT_REGION_SIZE, PAGE_PROT_READWRITE, DEFAULT_REGION_SIZE);
    ASSERT_NE(region.GetMem(), nullptr);
    auto *flags = static_cast<uint8_t *>(region.GetMem()) + Region::PackedData::GetFlagsOffset(false);
    uintptr_t value = reinterpret_cast<uintptr_t>(region.GetMem()) + DEFAULT_REGION_SIZE / 2;
    size_t gcOffset = JSThread::GlueData::GetGCStateBitFieldOffset(false);
    std::vector<uint64_t> glue(gcOffset / sizeof(uint64_t) + 1, 0);
    uint64_t object[2] = {0, 0};
    for (MarkStatus marking : {MarkStatus::READY_TO_MARK, MarkStatus::MARKING, MarkStatus::MARK_FINISHED}) {
        glue[gcOffset / sizeof(uint64_t)] = static_cast<uint64_t>(marking);
        for (int space = RegionSpaceFlag::HEAP_SPACE_BEGIN; space <= RegionSpaceFlag::HEAP_SPACE_END; ++space) {
            *flags = static_cast<uint8_t>(space);
            bool needsBarrier = space != RegionSpaceFlag::IN_SHARED_READ_ONLY_SPACE &&
                (space == RegionSpaceFlag::IN_YOUNG_SPACE || space >= RegionSpaceFlag::SHARED_SPACE_BEGIN ||
                 marking != MarkStatus::READY_TO_MARK);
            ColdEmit fixture(thread);
            fixture.Emitter().StoreTaggedField(Glue(), Object(), Value(), FIELD_OFFSET, Kind::GENERIC_BARRIER,
                                               ObjectScratch(), ValueScratch(), ValueKind::HeapObject);
            EXPECT_EQ(fixture.RunFilter(reinterpret_cast<uintptr_t>(glue.data()),
                                        reinterpret_cast<uintptr_t>(object), value), needsBarrier)
                << "space=" << space << " marking=" << static_cast<int>(marking);
            EXPECT_EQ(object[1], value);
        }
    }
    PageUnmap(region);
}

HWTEST_F_L0(ArkSteedWriteBarrierEmitterTest, UnknownScalarSkipsRegionAccess)
{
    uint64_t object[2] = {0, 0};
    uintptr_t value = JSTaggedValue(17).GetRawData();
    ColdEmit fixture(thread);
    fixture.Emitter().StoreTaggedField(Glue(), Object(), Value(), FIELD_OFFSET, Kind::GENERIC_BARRIER,
                                       ObjectScratch(), ValueScratch(), ValueKind::Unknown);
    EXPECT_EQ(fixture.RunFilter(0, reinterpret_cast<uintptr_t>(object), value), 0u);
    EXPECT_EQ(object[1], value);
}
#endif

// Whole {barrier kind} x {value kind} matrix for StoreTaggedField plus the
// barrier-kind precedence: a non-heap value kind and NO_BARRIER both select the
// plain store, a heap value kind selects the post-store barrier, and an unknown
// value kind selects the tag/region check plus the same barrier.
HWTEST_F_L0(ArkSteedWriteBarrierEmitterTest, FieldBarrierMatrix)
{
    Emission plain = PlainFieldStore();
    ASSERT_GT(plain.codeSize, 0u);

    Emission noBarrier = FieldStore(Kind::NO_BARRIER, ValueKind::Unknown);
    EXPECT_EQ(noBarrier.codeSize, plain.codeSize) << "NO_BARRIER must emit the plain field store only";
    EXPECT_EQ(noBarrier.deferredBlocks, 0u) << "NO_BARRIER must not queue a barrier call";

    // NO_BARRIER wins over a heap value kind: no barrier is emitted at all.
    Emission noBarrierHeap = FieldStore(Kind::NO_BARRIER, ValueKind::HeapObject);
    EXPECT_EQ(noBarrierHeap.codeSize, plain.codeSize) << "NO_BARRIER must ignore a heap value kind";
    EXPECT_EQ(noBarrierHeap.deferredBlocks, 0u);

    // Both short-circuit operands hold together: NO_BARRIER with a scalar value
    // kind must still take the plain-store branch. An implementation that
    // selected the barrier path when exactly one operand holds (XOR instead of
    // OR) would keep the two one-sided cases above green and fail here.
    Emission noBarrierScalar = FieldStore(Kind::NO_BARRIER, ValueKind::NonHeap);
    EXPECT_EQ(noBarrierScalar.codeSize, plain.codeSize) << "NO_BARRIER must ignore a scalar value kind";
    EXPECT_EQ(noBarrierScalar.deferredBlocks, 0u);

    for (Kind kind : {Kind::GENERIC_BARRIER, Kind::SHARED_BARRIER}) {
        Emission nonHeap = FieldStore(kind, ValueKind::NonHeap);
        EXPECT_EQ(nonHeap.codeSize, plain.codeSize) << "a scalar value must skip the post-store barrier";
        EXPECT_EQ(nonHeap.deferredBlocks, 0u) << "a scalar value must not queue a barrier call";

        Emission heap = FieldStore(kind, ValueKind::HeapObject);
        EXPECT_GT(heap.codeSize, plain.codeSize) << "a heap value must append a post-store barrier";
        EXPECT_GE(heap.deferredBlocks, 1u) << "a heap value must queue a deferred barrier call";

        Emission unknown = FieldStore(kind, ValueKind::Unknown);
        EXPECT_GT(unknown.codeSize, heap.codeSize)
            << "an unknown value must add the tag/region check before the barrier";
        EXPECT_EQ(unknown.deferredBlocks, heap.deferredBlocks)
            << "the tag/region check must not duplicate the barrier call";
    }
}

// "HeapObject adds exactly one post-store barrier". StoreTaggedField(HeapObject)
// and TransitionHClass append the very same GENERIC barrier body to their own
// store instruction, so the size difference between the two paths must equal the
// difference between the two store instructions alone: the barrier body cancels
// out. If the heap path emitted the barrier twice (or not at all) the left side
// would be off by one whole barrier body.
HWTEST_F_L0(ArkSteedWriteBarrierEmitterTest, FieldPostStoreBarrierSingleBody)
{
    Emission plainField = PlainFieldStore();
    Emission plainHClass = PlainHClassStore();
    Emission heapField = FieldStore(Kind::GENERIC_BARRIER, ValueKind::HeapObject);
    Emission transition = TransitionHClassStore();

    EXPECT_GT(heapField.codeSize, plainField.codeSize) << "heap value path carries no barrier bytes";
    EXPECT_GT(transition.codeSize, plainHClass.codeSize) << "TransitionHClass carries no barrier bytes";

    // Both paths queue the same single GENERIC barrier body.
    EXPECT_EQ(heapField.deferredBlocks, transition.deferredBlocks)
        << "the two GENERIC barrier sites must queue the same deferred calls";

#ifndef ARK_USE_SATB_BARRIER
    // TransitionHClass stores the HClass word and then emits the GENERIC
    // post-store barrier (arksteed_write_barrier.cpp). The plain references
    // reproduce exactly the store instruction of each path.
    int64_t fieldBarrier = static_cast<int64_t>(heapField.codeSize) - static_cast<int64_t>(plainField.codeSize);
    int64_t transitionBarrier =
        static_cast<int64_t>(transition.codeSize) - static_cast<int64_t>(plainHClass.codeSize);
    EXPECT_GT(fieldBarrier, 0) << "the heap field path must carry a post-store barrier";
    EXPECT_GT(transitionBarrier, 0) << "TransitionHClass must carry a post-store barrier";
    EXPECT_EQ(fieldBarrier, transitionBarrier)
        << "the GENERIC post-store barrier body did not cancel: not exactly one barrier body";
#endif
}

// "Unknown adds a region check and a barrier branch, strictly larger than
// HeapObject". The tag check is emitted before the barrier and does not depend
// on the barrier kind, so the two deltas against the heap path are equal.
HWTEST_F_L0(ArkSteedWriteBarrierEmitterTest, FieldUnknownRegionCheckOverhead)
{
    Emission heapGeneric = FieldStore(Kind::GENERIC_BARRIER, ValueKind::HeapObject);
    Emission heapShared = FieldStore(Kind::SHARED_BARRIER, ValueKind::HeapObject);
    Emission unknownGeneric = FieldStore(Kind::GENERIC_BARRIER, ValueKind::Unknown);
    Emission unknownShared = FieldStore(Kind::SHARED_BARRIER, ValueKind::Unknown);

    EXPECT_GT(unknownGeneric.codeSize, heapGeneric.codeSize)
        << "GENERIC unknown value path must be larger than the heap path";
    EXPECT_GT(unknownShared.codeSize, heapShared.codeSize)
        << "SHARED unknown value path must be larger than the heap path";

    EXPECT_EQ(unknownGeneric.codeSize - heapGeneric.codeSize, unknownShared.codeSize - heapShared.codeSize)
        << "the tag/region check prefix must not depend on the barrier kind";

    EXPECT_EQ(unknownGeneric.deferredBlocks, heapGeneric.deferredBlocks)
        << "the tag/region check must not duplicate the barrier call";
    EXPECT_EQ(unknownShared.deferredBlocks, heapShared.deferredBlocks)
        << "the tag/region check must not duplicate the barrier call";
}

// StoreTaggedElement keeps the same order of sizes as StoreTaggedField:
// non-heap (plain store) < heap object (inline runtime barrier call) < unknown
// (tag check plus the same call). The element path uses the inline
// CallNGCRuntime barrier, so it queues no deferred block.
HWTEST_F_L0(ArkSteedWriteBarrierEmitterTest, ElementBarrierMatrix)
{
    Emission plain = PlainElementStore();
    ASSERT_GT(plain.codeSize, 0u);

    Emission nonHeap = ElementStore(ValueKind::NonHeap);
    EXPECT_EQ(nonHeap.codeSize, plain.codeSize) << "a scalar element value must skip the barrier";
    EXPECT_EQ(nonHeap.deferredBlocks, 0u);

    Emission heap = ElementStore(ValueKind::HeapObject);
    EXPECT_GT(heap.codeSize, nonHeap.codeSize) << "a heap element value must call the barrier runtime";
    EXPECT_EQ(heap.deferredBlocks, 0u) << "the element barrier is an inline runtime call, not a deferred block";

    Emission unknown = ElementStore(ValueKind::Unknown);
    EXPECT_GT(unknown.codeSize, heap.codeSize)
        << "an unknown element value must add the tag check before the same call";
    EXPECT_EQ(unknown.deferredBlocks, 0u);
}

// TransitionHClass stores the HClass word and appends a GENERIC post-store
// barrier to it (non-SATB configuration).
HWTEST_F_L0(ArkSteedWriteBarrierEmitterTest, TransitionHClassPostStoreBarrier)
{
    Emission plainHClass = PlainHClassStore();
    Emission transition = TransitionHClassStore();

    EXPECT_GT(transition.codeSize, plainHClass.codeSize)
        << "TransitionHClass must append a post-store barrier to the HClass store";
    EXPECT_GE(transition.deferredBlocks, 1u) << "the appended barrier must queue a deferred call";
    EXPECT_EQ(plainHClass.deferredBlocks, 0u);
}

// P0 negative control (H6, D5): the detector "barrier bytes present" must
// distinguish a heap value, which requires the post-store barrier, from a scalar
// value, which must not carry one. If the heap path silently lost its barrier it
// would look exactly like the scalar path, and the detector below would report
// false for it. The barrier-triggering branch is therefore genuinely validated.
HWTEST_F_L0(ArkSteedWriteBarrierEmitterTest, NegativeControlMissingBarrierDetected)
{
    Emission plain = PlainFieldStore();
    Emission scalar = FieldStore(Kind::GENERIC_BARRIER, ValueKind::NonHeap);
    Emission heap = FieldStore(Kind::GENERIC_BARRIER, ValueKind::HeapObject);

    auto hasPostStoreBarrier = [](const Emission &candidate, const Emission &baseline) {
        return candidate.codeSize > baseline.codeSize;
    };

    EXPECT_FALSE(hasPostStoreBarrier(scalar, plain)) << "the scalar path must not carry barrier bytes";
    EXPECT_TRUE(hasPostStoreBarrier(heap, plain))
        << "a missing post-store barrier for a heap value would corrupt the GC";
}

}  // namespace panda::ecmascript::arksteed
