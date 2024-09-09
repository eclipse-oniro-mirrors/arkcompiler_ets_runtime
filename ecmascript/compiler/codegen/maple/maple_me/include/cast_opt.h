/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef MAPLE_ME_INCLUDE_CAST_OPT_H
#define MAPLE_ME_INCLUDE_CAST_OPT_H
#include "mir_nodes.h"
#include "me_ir.h"

namespace maple {
// The order matters
enum CastKind {
    CAST_intTrunc = 0,
    CAST_zext = 1,
    CAST_sext = 2,
    CAST_int2fp = 3,
    CAST_fp2int = 4,
    CAST_fpTrunc = 5,
    CAST_fpExt = 6,
    CAST_retype = 7,
    CAST_unknown = 8
};

template <typename T,
          std::enable_if_t<std::is_base_of<MeExpr, T>::value || std::is_base_of<BaseNode, T>::value, bool> = true>
class CastInfo {
public:
    explicit CastInfo(T *expr) : expr(expr) {}
    virtual ~CastInfo() = default;
    virtual Opcode GetOp()
    {
        CHECK_FATAL(false, "NYI");
    }
    PrimType GetPrimType() const
    {
        return expr->GetPrimType();
    }
    virtual size_t GetBitsSize()
    {
        CHECK_FATAL(false, "NYI");
    }
    virtual T *GetOpnd(size_t index __attribute__((unused)))
    {
        CHECK_FATAL(false, "NYI");
    }

    bool IsInvalid() const
    {
        return kind == CAST_unknown;
    }
    CastKind kind = CAST_unknown;  // CastInfo is invalid if kind is CAST_unknown
    PrimType srcType = PTY_begin;
    PrimType dstType = PTY_end;
    T *expr = nullptr;  // expr's type must be MeExpr* or BaseNode*
};

class BaseNodeCastInfo : public CastInfo<BaseNode> {
public:
    explicit BaseNodeCastInfo(BaseNode *expr) : CastInfo(expr) {}
    ~BaseNodeCastInfo() = default;

    Opcode GetOp() override
    {
        return expr->GetOpCode();
    }

    size_t GetBitsSize() override
    {
        switch (GetOp()) {
            case OP_zext:
            case OP_sext:
                return static_cast<const ExtractbitsNode *>(expr)->GetBitsSize();
            default:
                CHECK_FATAL(false, "NYI");
                break;
        }
    }

    BaseNode *GetOpnd(size_t index) override
    {
        return expr->Opnd(index);
    }
};

class CastOpt {
public:
    static bool IsExplicitCastOp(Opcode op);
    static bool IsImplicitCastOp(Opcode op);
    static bool IsCompareOp(Opcode op);
};

class MapleCastOpt : public CastOpt {
public:
    static BaseNode *CreateMapleExprByCastKind(MIRBuilder &mirBuilder, CastKind castKind, PrimType srcType,
                                               PrimType dstType, BaseNode *opnd, TyIdx dstTyIdx = TyIdx(0));
    static BaseNode *SimplifyCastSingle(MIRBuilder &mirBuilder, const BaseNodeCastInfo &castInfo);
    static BaseNode *TransformCvtU1ToNe(MIRBuilder &mirBuilder, const TypeCvtNode *cvtExpr);
};
}  // namespace maple
#endif
