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

#ifndef ECMASCRIPT_ARKSTEED_INLINE_FRAME_STATE_H
#define ECMASCRIPT_ARKSTEED_INLINE_FRAME_STATE_H

#include "ecmascript/arksteed/arksteed_bc_frame_state.h"
#include "ecmascript/arksteed/arksteed_compilation_unit.h"

namespace panda::ecmascript::arksteed {

// Immutable snapshot of an inlined function's caller at the call bytecode.  A
// child GraphBuilder keeps the head of this chain and expands every snapshot
// into its eager/lazy deopt translation so the interpreter can reconstruct the
// complete JavaScript frame chain.
class ArkSteedInlinedCallerFrame {
public:
    ArkSteedInlinedCallerFrame(ArkSteedCompilationUnit *unit,
                               SharedBCFrameState frameState,
                               ValueVertex *taggedActualArgc,
                               const ChunkVector<ValueVertex *> &actualArguments,
                               uint32_t resumeBcOffset,
                               const ArkSteedInlinedCallerFrame *parent,
                               Chunk *chunk)
        : unit_(unit),
          frameState_(frameState.Snapshot(chunk)),
          taggedActualArgc_(taggedActualArgc),
          actualArguments_(chunk),
          resumeBcOffset_(resumeBcOffset),
          parent_(parent)
    {
        ASSERT(unit_ != nullptr);
        ASSERT(taggedActualArgc_ != nullptr);
        VRegIDType numLocal = unit_->GetMethod()->GetNumVregsWithCallField();
        VRegIDType numParams = unit_->GetMethod()->GetNumArgsForArkSteed();
        ASSERT(frameState_.NumVRegs() == NumVRegs(numLocal, numParams));
        actualArguments_.insert(actualArguments_.end(), actualArguments.begin(), actualArguments.end());
    }

    ArkSteedCompilationUnit *GetCompilationUnit() const
    {
        return unit_;
    }

    SharedBCFrameState GetFrameState() const
    {
        return frameState_;
    }

    ValueVertex *GetTaggedActualArgc() const
    {
        return taggedActualArgc_;
    }

    const ChunkVector<ValueVertex *> &GetActualArguments() const
    {
        return actualArguments_;
    }

    uint32_t GetResumeBcOffset() const
    {
        return resumeBcOffset_;
    }

    const ArkSteedInlinedCallerFrame *GetParent() const
    {
        return parent_;
    }

private:
    ArkSteedCompilationUnit *unit_ {nullptr};
    SharedBCFrameState frameState_;
    ValueVertex *taggedActualArgc_ {nullptr};
    ChunkVector<ValueVertex *> actualArguments_;
    uint32_t resumeBcOffset_ {0};
    const ArkSteedInlinedCallerFrame *parent_ {nullptr};
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_INLINE_FRAME_STATE_H
