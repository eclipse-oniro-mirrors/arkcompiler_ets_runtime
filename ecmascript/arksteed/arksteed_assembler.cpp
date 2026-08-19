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

namespace panda::ecmascript::arksteed {

ArkSteedAssembler::ArkSteedAssembler(Chunk *chunk, JSThread *compilerThread, JSThread *entryThread)
    : assembler_(chunk),
#if defined(PANDA_TARGET_ARM64)
      chunk_(chunk),
      veneerBranches_(chunk),
#endif
      compilerThread_(compilerThread),
      entryThread_(entryThread)
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

void ArkSteedAssembler::JumpIfNotECMAObject(ArkSteedRegister value, Label *target)
{
    JumpIfNotStrongHeapObject(value, target);
    TemporaryRegisterScope scope(this);
    ArkSteedRegister type = scope.Acquire();
    LoadField(type, value, TaggedObject::HCLASS_OFFSET);
    And(type, static_cast<int64_t>(TaggedObject::GC_STATE_MASK));
    LoadUint8Field(type, type, JSHClass::BIT_FIELD_OFFSET);
    Sub(type, static_cast<int32_t>(JSType::ECMA_OBJECT_FIRST));
    Compare(type, static_cast<int32_t>(JSType::ECMA_OBJECT_LAST) - static_cast<int32_t>(JSType::ECMA_OBJECT_FIRST));
    JumpIf(Condition::ABOVE, target);
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

}  // namespace panda::ecmascript::arksteed
