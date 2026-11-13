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

#include "ecmascript/arksteed/arksteed_assembler.h"

#include "ecmascript/arksteed/arksteed_disassembler.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_object.h"
#include "ecmascript/tagged_array.h"

namespace panda::ecmascript::arksteed {

ArkSteedAssembler::ArkSteedAssembler(Chunk *chunk, JSThread *compilerThread)
    : assembler_(chunk),
#if defined(PANDA_TARGET_ARM64)
      chunk_(chunk),
      veneerBranches_(chunk),
#endif
      compilerThread_(compilerThread)
{}

ArkSteedAssembler::~ArkSteedAssembler() = default;

// =============================================================================
// Platform-independent implementations
// =============================================================================

void ArkSteedAssembler::JumpIfNotStrongHeapObject(ArkSteedRegister value, Label *target)
{
    TemporaryRegisterScope scope(this);
    ArkSteedRegister bits = scope.Acquire();
    Move(bits, static_cast<int64_t>(JSTaggedValue::TAG_WEAK_MASK));
    And(bits, value);
#if defined(PANDA_TARGET_ARM64)
    Compare(bits, 0);
#endif
    JumpIf(Condition::NOT_EQUAL, target);
}

void ArkSteedAssembler::LoadHClassFromObject(ArkSteedRegister dst, ArkSteedRegister object)
{
    // The object-header word packs the GC state into the high bits; every hclass
    // read from an object header goes through here so the mask is never skipped.
    // Tagged-field hclass references (proto-or-hclass, global-env slots, embedded
    // constants) are clean pointers and must NOT be masked.
    LoadField(dst, object, TaggedObject::HCLASS_OFFSET);
    And(dst, static_cast<int64_t>(TaggedObject::GC_STATE_MASK));
}

void ArkSteedAssembler::JumpIfNotECMAObject(ArkSteedRegister value, Label *target)
{
    JumpIfNotStrongHeapObject(value, target);
    TemporaryRegisterScope scope(this);
    ArkSteedRegister type = scope.Acquire();
    LoadHClassFromObject(type, value);
    LoadUint8Field(type, type, JSHClass::BIT_FIELD_OFFSET);
    Sub(type, static_cast<int32_t>(JSType::ECMA_OBJECT_FIRST));
    Compare(type, static_cast<int32_t>(JSType::ECMA_OBJECT_LAST) - static_cast<int32_t>(JSType::ECMA_OBJECT_FIRST));
    JumpIf(Condition::ABOVE, target);
}

void ArkSteedAssembler::JumpIfStoreArraySpreadNotAdmitted(ArkSteedRegister array, ArkSteedRegister value,
                                                          ArkSteedRegister globalEnv, Label *target)
{
    // Destination admission: a heap object of JSType JS_ARRAY, mirroring the
    // BranchIfObjectType lowering.
    JumpIfNotTaggedHeapObject(array, target);
    {
        TemporaryRegisterScope scope(this);
        ArkSteedRegister hclass = scope.Acquire();
        ArkSteedRegister bitField = scope.Acquire();
        LoadHClassFromObject(hclass, array);
        LoadField(bitField, hclass, JSHClass::BIT_FIELD_OFFSET);
        static_assert(JSHClass::ObjectTypeBits::START_BIT == 0);
        And(bitField, static_cast<int32_t>((1U << JSHClass::ObjectTypeBits::SIZE) - 1));
        Compare(bitField, static_cast<int32_t>(JSType::JS_ARRAY));
        JumpIf(Condition::NOT_EQUAL, target);
    }
    // Source admission: the full fast-spread check set including the hole scan.
    // The scan and the copy loop walk [0, array length), trusting the fast-mode
    // invariant JSArray::SetCapacity maintains: the elements backing is at least
    // the array length, the same invariant the runtime arm relies on.
    JumpIfNotFastSpreadArray(value, globalEnv, target, true);
}

void ArkSteedAssembler::LoadTaggedValue(ArkSteedRegister dst, uint64_t taggedValue)
{
    Move(dst, taggedValue);
}

void ArkSteedAssembler::RecordComment(const char *str)
{
    if (!enableComments_) {
        return;
    }
    uint32_t pcOffset = static_cast<uint32_t>(assembler_.GetCurrentPosition());
    comments_.Add(pcOffset, str);
}

void ArkSteedAssembler::RecordCommentAt(uint32_t pcOffset, const char *str)
{
    if (enableComments_) {
        comments_.Add(pcOffset, str);
    }
}

void ArkSteedAssembler::Disassemble(std::ostream &os, const uint8_t *commentsData, uint32_t commentsSize)
{
    ArkSteedDisassembler disassembler(GetCodeBuffer(), GetCodeSize(), commentsData, commentsSize);
    disassembler.Disassemble(os);
}

void TemporaryRegisterScope::AbortByExhaustedGPR()
{
    LOG_JIT(FATAL) << "RA temporary GPR pool exhausted";  // FATAL log triggers abort()
    std::terminate();
}

void TemporaryRegisterScope::AbortByExhaustedFPR()
{
    LOG_JIT(FATAL) << "RA temporary FPR pool exhausted";  // FATAL log triggers abort()
    std::terminate();
}

}  // namespace panda::ecmascript::arksteed
