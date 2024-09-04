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

#include "mir_nodes.h"

#include <algorithm>
#include <stack>

#include "maple_string.h"
#include "mir_function.h"
#include "namemangler.h"
#include "opcode_info.h"
#include "printing.h"
#include "utils.h"

namespace maple {
MIRModule *theMIRModule = nullptr;
std::atomic<uint32> StmtNode::stmtIDNext(1);  // 0 is reserved
uint32 StmtNode::lastPrintedLineNum = 0;
uint16 StmtNode::lastPrintedColumnNum = 0;
const int32 CondGotoNode::probAll = 10000;

const char *GetIntrinsicName(MIRIntrinsicID intrn)
{
    switch (intrn) {
        default:
#define DEF_MIR_INTRINSIC(STR, NAME, INTRN_CLASS, RETURN_TYPE, ...) \
    case INTRN_##STR:                                               \
        return #STR;
#include "intrinsics.def"
#undef DEF_MIR_INTRINSIC
    }
}

const char *BaseNode::GetOpName() const
{
    return kOpcodeInfo.GetTableItemAt(GetOpCode()).name.c_str();
}

bool BaseNode::MayThrowException()
{
    if (kOpcodeInfo.MayThrowException(GetOpCode())) {
        if (GetOpCode() != OP_array) {
            return true;
        }
        auto *arry = static_cast<ArrayNode *>(this);
        if (arry->GetBoundsCheck()) {
            return true;
        }
    }
    for (size_t i = 0; i < NumOpnds(); ++i) {
        if (Opnd(i)->MayThrowException()) {
            return true;
        }
    }
    return false;
}

bool AddrofNode::CheckNode(const MIRModule &mod) const
{
    const MIRSymbol *st = mod.CurFunction()->GetLocalOrGlobalSymbol(GetStIdx());
    DEBUG_ASSERT(st != nullptr, "null ptr check");
    MIRType *ty = st->GetType();
    switch (ty->GetKind()) {
        case kTypeScalar: {
            return IsPrimitiveScalar(GetPrimType());
        }
        case kTypeArray: {
            return GetPrimType() == PTY_agg;
        }
        case kTypeUnion:
        case kTypeStruct:
        case kTypeStructIncomplete: {
            if (GetFieldID() == 0) {
                return GetPrimType() == PTY_agg;
            }
            auto *structType = static_cast<MIRStructType *>(ty);
            TyIdx fTyIdx = structType->GetFieldTyIdx(fieldID);
            MIRType *subType = GlobalTables::GetTypeTable().GetTypeFromTyIdx(fTyIdx);
            MIRTypeKind subKind = subType->GetKind();
            return (subKind == kTypeBitField) ||
                   (subKind == kTypeScalar && IsPrimitiveScalar(GetPrimType())) ||
                   (subKind == kTypePointer && IsPrimitivePoint(GetPrimType())) ||
                   (subKind == kTypeStruct && GetPrimType() == PTY_agg) || (fTyIdx != 0u && GetPrimType() == PTY_agg);
        }
        case kTypeClass:
        case kTypeClassIncomplete: {
            if (fieldID == 0) {
                return GetPrimType() == PTY_agg;
            }
            auto *classType = static_cast<MIRClassType *>(ty);
            MIRType *subType = classType->GetFieldType(fieldID);
            MIRTypeKind subKind = subType->GetKind();
            return (subKind == kTypeBitField) ||
                   (subKind == kTypeScalar && IsPrimitiveScalar(GetPrimType())) ||
                   (subKind == kTypePointer && IsPrimitivePoint(GetPrimType())) ||
                   (subKind == kTypeStruct && GetPrimType() == PTY_agg);
        }
        case kTypeInterface:
        case kTypeInterfaceIncomplete: {
            if (fieldID == 0) {
                return GetPrimType() == PTY_agg;
            }
            auto *interfaceType = static_cast<MIRInterfaceType *>(ty);
            MIRType *subType = interfaceType->GetFieldType(fieldID);
            MIRTypeKind subKind = subType->GetKind();
            return (subKind == kTypeBitField) ||
                   (subKind == kTypeScalar && IsPrimitiveScalar(GetPrimType())) ||
                   (subKind == kTypePointer && IsPrimitivePoint(GetPrimType())) ||
                   (subKind == kTypeStruct && GetPrimType() == PTY_agg);
        }
        case kTypePointer:
            return IsPrimitivePoint(GetPrimType());
        case kTypeParam:
        case kTypeGenericInstant:
            return true;
        default:
            return false;
    }
}

MIRType *IreadNode::GetType() const
{
    MIRPtrType *ptrtype = static_cast<MIRPtrType *>(GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx));
    if (fieldID == 0) {
        return ptrtype->GetPointedType();
    }
    return GlobalTables::GetTypeTable().GetTypeFromTyIdx(ptrtype->GetPointedTyIdxWithFieldID(fieldID));
}

bool IreadNode::IsVolatile() const
{
    MIRType *type = GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx);
    DEBUG_ASSERT(type != nullptr, "null ptr check");
    DEBUG_ASSERT(type->IsMIRPtrType(), "type of iread should be pointer type");
    return static_cast<MIRPtrType *>(type)->IsPointedTypeVolatile(fieldID);
}

bool AddrofNode::IsVolatile(const MIRModule &mod) const
{
    DEBUG_ASSERT(mod.CurFunction() != nullptr, "mod.CurFunction() should not be nullptr");
    auto *symbol = mod.CurFunction()->GetLocalOrGlobalSymbol(stIdx);
    DEBUG_ASSERT(symbol != nullptr, "null ptr check on symbol");
    return symbol->IsVolatile();
}

bool DreadoffNode::IsVolatile(const MIRModule &mod) const
{
    auto *symbol = mod.CurFunction()->GetLocalOrGlobalSymbol(stIdx);
    DEBUG_ASSERT(symbol != nullptr, "null ptr check on symbol");
    return symbol->IsVolatile();
}

bool DassignNode::AssigningVolatile(const MIRModule &mod) const
{
    auto *symbol = mod.CurFunction()->GetLocalOrGlobalSymbol(stIdx);
    DEBUG_ASSERT(symbol != nullptr, "null ptr check on symbol");
    return symbol->IsVolatile();
}

bool IassignNode::AssigningVolatile() const
{
    MIRType *type = GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx);
    DEBUG_ASSERT(type != nullptr, "null ptr check");
    DEBUG_ASSERT(type->IsMIRPtrType(), "type of iassign should be pointer type");
    return static_cast<MIRPtrType *>(type)->IsPointedTypeVolatile(fieldID);
}

void BlockNode::AddStatement(StmtNode *stmt)
{
    DEBUG_ASSERT(stmt != nullptr, "null ptr check");
    stmtNodeList.push_back(stmt);
}

void BlockNode::AppendStatementsFromBlock(BlockNode &blk)
{
    if (blk.GetStmtNodes().empty()) {
        return;
    }
    stmtNodeList.splice(stmtNodeList.end(), blk.GetStmtNodes());
}

/// Insert stmt as the first
void BlockNode::InsertFirst(StmtNode *stmt)
{
    DEBUG_ASSERT(stmt != nullptr, "stmt is null");
    stmtNodeList.push_front(stmt);
}

/// Insert stmt as the last
void BlockNode::InsertLast(StmtNode *stmt)
{
    DEBUG_ASSERT(stmt != nullptr, "stmt is null");
    stmtNodeList.push_back(stmt);
}

void BlockNode::ReplaceStmtWithBlock(StmtNode &stmtNode, BlockNode &blk)
{
    stmtNodeList.splice(&stmtNode, blk.GetStmtNodes());
    stmtNodeList.erase(&stmtNode);
    stmtNode.SetNext(blk.GetLast()->GetNext());
}

void BlockNode::ReplaceStmt1WithStmt2(const StmtNode *stmtNode1, StmtNode *stmtNode2)
{
    if (stmtNode2 == stmtNode1) {
        // do nothing
    } else if (stmtNode2 == nullptr) {
        // delete stmtNode1
        stmtNodeList.erase(stmtNode1);
    } else {
        // replace stmtNode1 with stmtNode2
        stmtNodeList.insert(stmtNode1, stmtNode2);
        (void)stmtNodeList.erase(stmtNode1);
    }
}

// remove sstmtNode1 from block
void BlockNode::RemoveStmt(const StmtNode *stmtNode1)
{
    DEBUG_ASSERT(stmtNode1 != nullptr, "delete a null stmtment");
    (void)stmtNodeList.erase(stmtNode1);
}

/// Insert stmtNode2 before stmtNode1 in current block.
void BlockNode::InsertBefore(const StmtNode *stmtNode1, StmtNode *stmtNode2)
{
    stmtNodeList.insert(stmtNode1, stmtNode2);
}

/// Insert stmtNode2 after stmtNode1 in current block.
void BlockNode::InsertAfter(const StmtNode *stmtNode1, StmtNode *stmtNode2)
{
    stmtNodeList.insertAfter(stmtNode1, stmtNode2);
}

// insert all the stmts in inblock to the current block after stmt1
void BlockNode::InsertBlockAfter(BlockNode &inblock, const StmtNode *stmt1)
{
    DEBUG_ASSERT(stmt1 != nullptr, "null ptr check");
    DEBUG_ASSERT(!inblock.IsEmpty(), "NYI");
    stmtNodeList.splice(stmt1, inblock.GetStmtNodes());
}

BlockNode *BlockNode::CloneTreeWithFreqs(MapleAllocator &allocator, std::unordered_map<uint32_t, uint64_t> &toFreqs,
                                         std::unordered_map<uint32_t, uint64_t> &fromFreqs, uint64_t numer,
                                         uint64_t denom, uint32_t updateOp)
{
    auto *nnode = allocator.GetMemPool()->New<BlockNode>();
    nnode->SetStmtID(stmtIDNext++);
    if (fromFreqs.count(GetStmtID()) > 0) {
        uint64_t oldFreq = fromFreqs[GetStmtID()];
        uint64_t newFreq;
        if (updateOp & kUpdateUnrollRemainderFreq) {
            newFreq = denom > 0 ? (oldFreq * numer % denom) : oldFreq;
        } else {
            newFreq = numer == 0 ? 0 : (denom > 0 ? (oldFreq * numer / denom) : oldFreq);
        }
        toFreqs[nnode->GetStmtID()] = (newFreq > 0 || (numer == 0)) ? newFreq : 1;
        if (updateOp & kUpdateOrigFreq) {  // upateOp & 1 : update from
            int64_t left = ((oldFreq - newFreq) > 0 || (oldFreq == 0)) ? static_cast<int64_t>(oldFreq - newFreq) : 1;
            fromFreqs[GetStmtID()] = static_cast<uint64_t>(left);
        }
    }
    for (auto &stmt : stmtNodeList) {
        StmtNode *newStmt;
        if (stmt.GetOpCode() == OP_block) {
            newStmt = static_cast<StmtNode *>(
                (static_cast<BlockNode *>(&stmt))
                    ->CloneTreeWithFreqs(allocator, toFreqs, fromFreqs, numer, denom, updateOp));
        } else if (stmt.GetOpCode() == OP_if) {
            newStmt = static_cast<StmtNode *>(
                (static_cast<IfStmtNode *>(&stmt))
                    ->CloneTreeWithFreqs(allocator, toFreqs, fromFreqs, numer, denom, updateOp));
        } else {
            newStmt = static_cast<StmtNode *>(stmt.CloneTree(allocator));
            if (fromFreqs.count(stmt.GetStmtID()) > 0) {
                uint64_t oldFreq = fromFreqs[stmt.GetStmtID()];
                uint64_t newFreq;
                if (updateOp & kUpdateUnrollRemainderFreq) {
                    newFreq = denom > 0 ? (oldFreq * numer % denom) : oldFreq;
                } else {
                    newFreq = numer == 0 ? 0 : (denom > 0 ? (oldFreq * numer / denom) : oldFreq);
                }
                toFreqs[newStmt->GetStmtID()] =
                    (newFreq > 0 || oldFreq == 0 || numer == 0) ? static_cast<uint64_t>(newFreq) : 1;
                if (updateOp & kUpdateOrigFreq) {
                    int64_t left = ((oldFreq - newFreq) > 0 || oldFreq == 0) ?
                        static_cast<int64_t>(oldFreq - newFreq) : 1;
                    fromFreqs[stmt.GetStmtID()] = static_cast<uint64_t>(left);
                }
            }
        }
        DEBUG_ASSERT(newStmt != nullptr, "null ptr check");
        newStmt->SetSrcPos(stmt.GetSrcPos());
        newStmt->SetPrev(nullptr);
        newStmt->SetNext(nullptr);
        nnode->AddStatement(newStmt);
    }
    return nnode;
}

#ifdef ARK_LITECG_DEBUG
void BaseNode::DumpBase(int32 indent) const
{
    PrintIndentation(indent);
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " " << GetPrimTypeName(GetPrimType());
}

void UnaryNode::DumpOpnd(const MIRModule &, int32 indent) const
{
    DumpOpnd(indent);
}

void UnaryNode::DumpOpnd(int32 indent) const
{
    LogInfo::MapleLogger() << " (";
    if (uOpnd != nullptr) {
        uOpnd->Dump(indent);
    }
    LogInfo::MapleLogger() << ")";
}

void UnaryNode::Dump(int32 indent) const
{
    BaseNode::DumpBase(0);
    DumpOpnd(*theMIRModule, indent);
}

void TypeCvtNode::Dump(int32 indent) const
{
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " ";
    LogInfo::MapleLogger() << GetPrimTypeName(GetPrimType()) << " " << GetPrimTypeName(FromType());
    DumpOpnd(*theMIRModule, indent);
}

void RetypeNode::Dump(int32 indent) const
{
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " ";
    LogInfo::MapleLogger() << GetPrimTypeName(GetPrimType()) << " ";
    MIRType *ty = GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx);
    if (ty->GetKind() == kTypeScalar) {
        LogInfo::MapleLogger() << "<";
        ty->Dump(indent + 1);
        LogInfo::MapleLogger() << ">";
    } else {
        ty->Dump(indent + 1);
    }
    DumpOpnd(*theMIRModule, indent);
}

void ExtractbitsNode::Dump(int32 indent) const
{
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " " << GetPrimTypeName(GetPrimType());
    if (GetOpCode() == OP_extractbits) {
        LogInfo::MapleLogger() << " " << static_cast<int32>(bitsOffset) << " " << static_cast<int32>(bitsSize);
    } else {
        LogInfo::MapleLogger() << " " << static_cast<int32>(bitsSize);
    }
    DumpOpnd(*theMIRModule, indent);
}

void IreadNode::Dump(int32 indent) const
{
    BaseNode::DumpBase(0);
    LogInfo::MapleLogger() << " ";
    GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx)->Dump(0);
    LogInfo::MapleLogger() << " " << fieldID;
    DumpOpnd(*theMIRModule, indent);
}

void IreadoffNode::Dump(int32 indent) const
{
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " " << GetPrimTypeName(GetPrimType());
    LogInfo::MapleLogger() << " " << offset;
    DumpOpnd(*theMIRModule, indent);
}

void BinaryNode::Dump(int32 indent) const
{
    BaseNode::DumpBase(0);
    BinaryOpnds::Dump(indent);
}

void BinaryOpnds::Dump(int32 indent) const
{
    LogInfo::MapleLogger() << " (";
    if (bOpnd[0]->IsLeaf() && bOpnd[1]->IsLeaf()) {
        bOpnd[0]->Dump(0);
        LogInfo::MapleLogger() << ", ";
        bOpnd[1]->Dump(0);
    } else {
        LogInfo::MapleLogger() << '\n';
        PrintIndentation(indent + 1);
        bOpnd[0]->Dump(indent + 1);
        LogInfo::MapleLogger() << ",\n";
        PrintIndentation(indent + 1);
        bOpnd[1]->Dump(indent + 1);
    }
    LogInfo::MapleLogger() << ")";
}

void CompareNode::Dump(int32 indent) const
{
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " " << GetPrimTypeName(GetPrimType());
    LogInfo::MapleLogger() << " " << GetPrimTypeName(opndType);
    BinaryOpnds::Dump(indent);
}

void DepositbitsNode::Dump(int32 indent) const
{
    BaseNode::DumpBase(0);
    LogInfo::MapleLogger() << " " << static_cast<int32>(bitsOffset) << " " << static_cast<int32>(bitsSize) << " (";
    if (GetBOpnd(0)->IsLeaf() && GetBOpnd(1)->IsLeaf()) {
        GetBOpnd(0)->Dump(0);
        LogInfo::MapleLogger() << ", ";
        GetBOpnd(1)->Dump(0);
    } else {
        LogInfo::MapleLogger() << '\n';
        PrintIndentation(indent + 1);
        GetBOpnd(0)->Dump(indent + 1);
        LogInfo::MapleLogger() << ",\n";
        PrintIndentation(indent + 1);
        GetBOpnd(1)->Dump(indent + 1);
    }
    LogInfo::MapleLogger() << ")";
}

void NaryOpnds::Dump(int32 indent) const
{
    LogInfo::MapleLogger() << " (";
    if (GetNopndSize() == 0) {
        LogInfo::MapleLogger() << ")";
        return;
    }
    if (GetNopndSize() == 1) {
        GetNopndAt(0)->Dump(indent);
    } else {
        bool allisLeaf = true;
        for (size_t i = 0; i < GetNopndSize(); ++i)
            if (!GetNopndAt(i)->IsLeaf()) {
                allisLeaf = false;
                break;
            }
        if (allisLeaf) {
            GetNopndAt(0)->Dump(0);
            for (size_t i = 1; i < GetNopndSize(); ++i) {
                LogInfo::MapleLogger() << ", ";
                GetNopndAt(i)->Dump(0);
            }
        } else {
            LogInfo::MapleLogger() << '\n';
            PrintIndentation(indent + 1);
            GetNopndAt(0)->Dump(indent + 1);
            for (size_t i = 1; i < GetNopndSize(); ++i) {
                LogInfo::MapleLogger() << ",\n";
                PrintIndentation(indent + 1);
                GetNopndAt(i)->Dump(indent + 1);
            }
        }
    }
    LogInfo::MapleLogger() << ")";
}

void DeoptBundleInfo::Dump(int32 indent) const
{
    size_t deoptBundleSize = deoptBundleInfo.size();
    if (deoptBundleSize == 0) {
        return;
    }
    LogInfo::MapleLogger() << " deopt: (";
    bool isFirstItem = true;
    for (const auto &elem : deoptBundleInfo) {
        if (!isFirstItem) {
            LogInfo::MapleLogger() << ", ";
        } else {
            isFirstItem = false;
        }
        LogInfo::MapleLogger() << elem.first << ": ";
        auto valueKind = elem.second.GetMapleValueKind();
        if (valueKind == MapleValue::kPregKind) {
            LogInfo::MapleLogger() << "%" << elem.second.GetPregIdx() << " ";
        } else if (valueKind == MapleValue::kConstKind) {
            if (elem.second.GetConstValue().GetKind() != kConstInt) {
                CHECK_FATAL(false, "not supported currently");
            }
            LogInfo::MapleLogger() << static_cast<const MIRIntConst &>(elem.second.GetConstValue()).GetValue() << " ";
        }
    }
    LogInfo::MapleLogger() << ")";
}

void NaryNode::Dump(int32 indent) const
{
    BaseNode::DumpBase(0);
    NaryOpnds::Dump(indent);
}
#endif

const MIRType *ArrayNode::GetArrayType(const TypeTable &tt) const
{
    const MIRType *type = tt.GetTypeFromTyIdx(tyIdx);
    CHECK_FATAL(type->GetKind() == kTypePointer, "expect array type pointer");
    const auto *pointType = static_cast<const MIRPtrType *>(type);
    return tt.GetTypeFromTyIdx(pointType->GetPointedTyIdx());
}
MIRType *ArrayNode::GetArrayType(const TypeTable &tt)
{
    return const_cast<MIRType *>(const_cast<const ArrayNode *>(this)->GetArrayType(tt));
}

const BaseNode *ArrayNode::GetDim(const MIRModule &mod, TypeTable &tt, int i) const
{
    const auto *arrayType = static_cast<const MIRArrayType *>(GetArrayType(tt));
    auto *mirConst =
        GlobalTables::GetIntConstTable().GetOrCreateIntConst(i, *tt.GetTypeFromTyIdx(arrayType->GetElemTyIdx()));
    return mod.CurFuncCodeMemPool()->New<ConstvalNode>(mirConst);
}
BaseNode *ArrayNode::GetDim(const MIRModule &mod, TypeTable &tt, int i)
{
    return const_cast<BaseNode *>(const_cast<const ArrayNode *>(this)->GetDim(mod, tt, i));
}

#ifdef ARK_LITECG_DEBUG
void ArrayNode::Dump(int32 indent) const
{
    PrintIndentation(0);
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " ";
    if (boundsCheck) {
        LogInfo::MapleLogger() << "1 ";
    } else {
        LogInfo::MapleLogger() << "0 ";
    }
    LogInfo::MapleLogger() << GetPrimTypeName(GetPrimType());
    LogInfo::MapleLogger() << " ";
    GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx)->Dump(0);
    NaryOpnds::Dump(indent);
}
#endif

bool ArrayNode::IsSameBase(ArrayNode *arry)
{
    DEBUG_ASSERT(arry != nullptr, "null ptr check");
    if (arry == this) {
        return true;
    }
    BaseNode *curBase = this->GetBase();
    BaseNode *otherBase = arry->GetBase();
    if (curBase->GetOpCode() != OP_addrof || otherBase->GetOpCode() != OP_addrof) {
        return false;
    }
    return static_cast<AddrofNode *>(curBase)->GetStIdx() == static_cast<AddrofNode *>(otherBase)->GetStIdx();
}

#ifdef ARK_LITECG_DEBUG
void IntrinsicopNode::Dump(int32 indent) const
{
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " " << GetPrimTypeName(GetPrimType());
    LogInfo::MapleLogger() << " " << GetIntrinsicName(GetIntrinsic());
    NaryOpnds::Dump(indent);
}

void ConstvalNode::Dump(int32) const
{
    if (GetConstVal()->GetType().GetKind() != kTypePointer) {
        BaseNode::DumpBase(0);
        LogInfo::MapleLogger() << " ";
    }
    GetConstVal()->Dump();
}

void ConststrNode::Dump(int32) const
{
    BaseNode::DumpBase(0);
    const std::string kStr = GlobalTables::GetUStrTable().GetStringFromStrIdx(UStrIdx(strIdx));
    PrintString(kStr);
}

void Conststr16Node::Dump(int32) const
{
    BaseNode::DumpBase(0);
    const std::u16string kStr16 = GlobalTables::GetU16StrTable().GetStringFromStrIdx(U16StrIdx(strIdx));
    // UTF-16 string are dumped as UTF-8 string in mpl to keep the printable chars in ascii form
    std::string str;
    (void)namemangler::UTF16ToUTF8(str, kStr16);
    PrintString(str);
}

void AddrofNode::Dump(int32) const
{
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " " << GetPrimTypeName(GetPrimType());
    DEBUG_ASSERT(theMIRModule->CurFunction() != nullptr, "theMIRModule->CurFunction() should not be nullptr");
    const MIRSymbol *st = theMIRModule->CurFunction()->GetLocalOrGlobalSymbol(GetStIdx());
    LogInfo::MapleLogger() << (GetStIdx().Islocal() ? " %" : " $");
    DEBUG_ASSERT(st != nullptr, "null ptr check");
    LogInfo::MapleLogger() << st->GetName();
    if (fieldID != 0) {
        LogInfo::MapleLogger() << " " << fieldID;
    }
}

void DreadoffNode::Dump(int32) const
{
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " " << GetPrimTypeName(GetPrimType());
    const MIRSymbol *st = theMIRModule->CurFunction()->GetLocalOrGlobalSymbol(stIdx);
    LogInfo::MapleLogger() << (stIdx.Islocal() ? " %" : " $");
    DEBUG_ASSERT(st != nullptr, "null ptr check");
    LogInfo::MapleLogger() << st->GetName();
    LogInfo::MapleLogger() << " " << offset;
}

void RegreadNode::Dump(int32) const
{
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " " << GetPrimTypeName(GetPrimType());
    if (regIdx >= 0) {
        LogInfo::MapleLogger() << " %"
                               << theMIRModule->CurFunction()->GetPregTab()->PregFromPregIdx(regIdx)->GetPregNo();
        return;
    }
    LogInfo::MapleLogger() << " %%";
    switch (regIdx) {
        case -kSregSp:
            LogInfo::MapleLogger() << "SP";
            break;
        case -kSregFp:
            LogInfo::MapleLogger() << "FP";
            break;
        default:
            int32 retValIdx = (-regIdx) - kSregRetval0;
            LogInfo::MapleLogger() << "retval" << retValIdx;
            break;
    }
}

void AddroffuncNode::Dump(int32) const
{
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " " << GetPrimTypeName(GetPrimType());
    MIRFunction *func = GlobalTables::GetFunctionTable().GetFunctionFromPuidx(puIdx);
    CHECK_FATAL(func != nullptr, "null ptr");
    MIRSymbol *symbol = GlobalTables::GetGsymTable().GetSymbolFromStidx(func->GetStIdx().Idx());
    if (symbol != nullptr) {
        LogInfo::MapleLogger() << " &" << symbol->GetName();
    }
}

void AddroflabelNode::Dump(int32) const
{
    DEBUG_ASSERT(theMIRModule->CurFunction() != nullptr, "theMIRModule->CurFunction() should not be nullptr");
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name << " " << GetPrimTypeName(GetPrimType());
    LogInfo::MapleLogger() << " @" << theMIRModule->CurFunction()->GetLabelName(static_cast<LabelIdx>(offset));
}

void StmtNode::DumpBase(int32 indent) const
{
    srcPosition.DumpLoc(lastPrintedLineNum, lastPrintedColumnNum);
    // dump stmtFreqs
    DEBUG_ASSERT(theMIRModule->CurFunction() != nullptr, "theMIRModule->CurFunction() should not be nullptr");
    PrintIndentation(indent);
    LogInfo::MapleLogger() << kOpcodeInfo.GetTableItemAt(GetOpCode()).name;
}

void StmtNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    LogInfo::MapleLogger() << '\n';
}
#endif

// Get the next stmt skip the comment stmt.
StmtNode *StmtNode::GetRealNext() const
{
    StmtNode *stmt = this->GetNext();
    while (stmt != nullptr) {
        if (stmt->GetOpCode() != OP_comment) {
            break;
        }
        stmt = stmt->GetNext();
    }
    return stmt;
}

// insert this before pos
void StmtNode::InsertAfterThis(StmtNode &pos)
{
    this->SetNext(&pos);
    if (pos.GetPrev()) {
        this->SetPrev(pos.GetPrev());
        pos.GetPrev()->SetNext(this);
    }
    pos.SetPrev(this);
}

// insert stmtnode after pos
void StmtNode::InsertBeforeThis(StmtNode &pos)
{
    this->SetPrev(&pos);
    if (pos.GetNext()) {
        this->SetNext(pos.GetNext());
        pos.GetNext()->SetPrev(this);
    }
    pos.SetNext(this);
}

#ifdef ARK_LITECG_DEBUG
void DassignNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    const MIRSymbol *st = theMIRModule->CurFunction()->GetLocalOrGlobalSymbol(stIdx);
    DEBUG_ASSERT(st != nullptr, "null ptr check");
    LogInfo::MapleLogger() << (st->IsLocal() ? " %" : " $");
    LogInfo::MapleLogger() << st->GetName() << " " << fieldID;
    LogInfo::MapleLogger() << " (";
    if (GetRHS() != nullptr) {
        GetRHS()->Dump(indent + 1);
    } else {
        LogInfo::MapleLogger() << "/*empty-rhs*/";
    }
    LogInfo::MapleLogger() << ")\n";
}

void RegassignNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    LogInfo::MapleLogger() << " " << GetPrimTypeName(GetPrimType());
    if (regIdx >= 0) {
        LogInfo::MapleLogger() << " %"
                               << theMIRModule->CurFunction()->GetPregTab()->PregFromPregIdx(regIdx)->GetPregNo();
    } else {
        LogInfo::MapleLogger() << " %%";
        switch (regIdx) {
            case -kSregSp:
                LogInfo::MapleLogger() << "SP";
                break;
            case -kSregFp:
                LogInfo::MapleLogger() << "FP";
                break;
            case -kSregRetval0:
                LogInfo::MapleLogger() << "retval0";
                break;
            // no default
            default:
                break;
        }
    }
    LogInfo::MapleLogger() << " (";
    UnaryStmtNode::Opnd(0)->Dump(indent + 1);
    LogInfo::MapleLogger() << ")\n";
}

void IassignNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    LogInfo::MapleLogger() << " ";
    GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx)->Dump(0);
    LogInfo::MapleLogger() << " " << fieldID;
    LogInfo::MapleLogger() << " (";
    if (addrExpr->IsLeaf() && rhs->IsLeaf()) {
        addrExpr->Dump(0);
        LogInfo::MapleLogger() << ", ";
        rhs->Dump(0);
    } else {
        LogInfo::MapleLogger() << '\n';
        PrintIndentation(indent + 1);
        addrExpr->Dump(indent + 1);
        LogInfo::MapleLogger() << ", \n";
        PrintIndentation(indent + 1);
        rhs->Dump(indent + 1);
    }
    LogInfo::MapleLogger() << ")\n";
}

void IassignoffNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    LogInfo::MapleLogger() << " " << GetPrimTypeName(GetPrimType()) << " " << offset;
    BinaryOpnds::Dump(indent);
    LogInfo::MapleLogger() << '\n';
}

void GotoNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    if (offset == 0) {
        LogInfo::MapleLogger() << '\n';
    } else {
        LogInfo::MapleLogger() << " @" << theMIRModule->CurFunction()->GetLabelName(static_cast<LabelIdx>(offset))
                               << '\n';
    }
}

void CondGotoNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    LogInfo::MapleLogger() << " @" << theMIRModule->CurFunction()->GetLabelName(static_cast<LabelIdx>(offset));
    LogInfo::MapleLogger() << " (";
    Opnd(0)->Dump(indent);
    LogInfo::MapleLogger() << ")\n";
}

void SwitchNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    LogInfo::MapleLogger() << " (";
    switchOpnd->Dump(indent);
    if (defaultLabel == 0) {
        LogInfo::MapleLogger() << ") 0 {";
    } else {
        LogInfo::MapleLogger() << ") @" << theMIRModule->CurFunction()->GetLabelName(defaultLabel) << " {";
    }
    for (auto it = switchTable.begin(); it != switchTable.end(); it++) {
        LogInfo::MapleLogger() << '\n';
        PrintIndentation(indent + 1);
        LogInfo::MapleLogger() << std::hex << "0x" << it->first << std::dec;
        LogInfo::MapleLogger() << ": goto @" << theMIRModule->CurFunction()->GetLabelName(it->second);
    }
    LogInfo::MapleLogger() << " }\n";
}

void RangeGotoNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    LogInfo::MapleLogger() << " (";
    Opnd(0)->Dump(indent);
    LogInfo::MapleLogger() << ") " << tagOffset << " {";
    for (auto it = rangegotoTable.begin(); it != rangegotoTable.end(); it++) {
        LogInfo::MapleLogger() << '\n';
        PrintIndentation(indent + 1);
        LogInfo::MapleLogger() << std::hex << "0x" << it->first << std::dec;
        LogInfo::MapleLogger() << ": goto @" << theMIRModule->CurFunction()->GetLabelName(it->second);
    }
    LogInfo::MapleLogger() << " }\n";
}

void UnaryStmtNode::DumpOpnd(const MIRModule &, int32 indent) const
{
    DumpOpnd(indent);
}

void UnaryStmtNode::DumpOpnd(int32 indent) const
{
    LogInfo::MapleLogger() << " (";
    if (uOpnd != nullptr) {
        uOpnd->Dump(indent);
    }
    LogInfo::MapleLogger() << ")\n";
}

void UnaryStmtNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    DumpOpnd(indent);
}

void IfStmtNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    LogInfo::MapleLogger() << " (";
    Opnd()->Dump(indent);
    LogInfo::MapleLogger() << ")";
    thenPart->Dump(indent);
    if (elsePart) {
        PrintIndentation(indent);
        LogInfo::MapleLogger() << "else {\n";
        for (auto &stmt : elsePart->GetStmtNodes()) {
            stmt.Dump(indent + 1);
        }
        PrintIndentation(indent);
        LogInfo::MapleLogger() << "}\n";
    }
}

void BinaryStmtNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    BinaryOpnds::Dump(indent);
    LogInfo::MapleLogger() << '\n';
}

void NaryStmtNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    NaryOpnds::Dump(indent);
    LogInfo::MapleLogger() << '\n';
}

void AssertNonnullStmtNode::Dump(int32 indent) const
{
    StmtNode::DumpBase(indent);
    if (theMIRModule->IsCModule()) {
        SafetyCheckStmtNode::Dump();
    }
    UnaryStmtNode::DumpOpnd(indent);
}

void DumpCallReturns(const MIRModule &mod, CallReturnVector nrets, int32 indent)
{
    const MIRFunction *mirFunc = mod.CurFunction();
    if (nrets.empty()) {
        LogInfo::MapleLogger() << " {}\n";
        return;
    } else if (nrets.size() == 1) {
        StIdx stIdx = nrets.begin()->first;
        RegFieldPair regFieldPair = nrets.begin()->second;
        if (!regFieldPair.IsReg()) {
            const MIRSymbol *st = mirFunc->GetLocalOrGlobalSymbol(stIdx);
            DEBUG_ASSERT(st != nullptr, "st is null");
            FieldID fieldID = regFieldPair.GetFieldID();
            LogInfo::MapleLogger() << " { dassign ";
            LogInfo::MapleLogger() << (stIdx.Islocal() ? "%" : "$");
            LogInfo::MapleLogger() << st->GetName() << " " << fieldID << " }\n";
            return;
        } else {
            PregIdx regIdx = regFieldPair.GetPregIdx();
            const MIRPreg *mirPreg = mirFunc->GetPregItem(static_cast<PregIdx>(regIdx));
            DEBUG_ASSERT(mirPreg != nullptr, "mirPreg is null");
            LogInfo::MapleLogger() << " { regassign";
            LogInfo::MapleLogger() << " " << GetPrimTypeName(mirPreg->GetPrimType());
            LogInfo::MapleLogger() << " %" << mirPreg->GetPregNo() << "}\n";
            return;
        }
    }
    LogInfo::MapleLogger() << " {\n";
    constexpr int32 spaceNum = 2;
    for (auto it = nrets.begin(); it != nrets.end(); it++) {
        PrintIndentation(indent + spaceNum);
        StIdx stIdx = (it)->first;
        RegFieldPair regFieldPair = it->second;
        if (!regFieldPair.IsReg()) {
            FieldID fieldID = regFieldPair.GetFieldID();
            LogInfo::MapleLogger() << "dassign";
            const MIRSymbol *st = mirFunc->GetLocalOrGlobalSymbol(stIdx);
            DEBUG_ASSERT(st != nullptr, "st is null");
            LogInfo::MapleLogger() << (stIdx.Islocal() ? " %" : " $");
            LogInfo::MapleLogger() << st->GetName() << " " << fieldID << '\n';
        } else {
            PregIdx regIdx = regFieldPair.GetPregIdx();
            const MIRPreg *mirPreg = mirFunc->GetPregItem(static_cast<PregIdx>(regIdx));
            DEBUG_ASSERT(mirPreg != nullptr, "mirPreg is null");
            LogInfo::MapleLogger() << "regassign"
                                   << " " << GetPrimTypeName(mirPreg->GetPrimType());
            LogInfo::MapleLogger() << " %" << mirPreg->GetPregNo() << '\n';
        }
    }
    PrintIndentation(indent + 1);
    LogInfo::MapleLogger() << "}\n";
}
#endif

// iread expr has sideeffect, may cause derefference error
bool HasIreadExpr(const BaseNode *expr)
{
    if (expr->GetOpCode() == OP_iread) {
        return true;
    }
    for (size_t i = 0; i < expr->GetNumOpnds(); ++i) {
        if (HasIreadExpr(expr->Opnd(i))) {
            return true;
        }
    }
    return false;
}

// layer to leaf node
size_t MaxDepth(const BaseNode *expr)
{
    if (expr->IsLeaf()) {
        return 1;
    }
    size_t maxSubDepth = 0;
    for (size_t i = 0; i < expr->GetNumOpnds(); ++i) {
        size_t depth = MaxDepth(expr->Opnd(i));
        maxSubDepth = (depth > maxSubDepth) ? depth : maxSubDepth;
    }
    return maxSubDepth + 1;  // expr itself
}

MIRType *CallNode::GetCallReturnType()
{
    if (!kOpcodeInfo.IsCallAssigned(GetOpCode())) {
        return nullptr;
    }
    DEBUG_ASSERT(GlobalTables::GetFunctionTable().GetFuncTable().empty() == false, "container check");
    MIRFunction *mirFunc = GlobalTables::GetFunctionTable().GetFunctionFromPuidx(puIdx);
    return mirFunc->GetReturnType();
}

const MIRSymbol *CallNode::GetCallReturnSymbol(const MIRModule &mod) const
{
    if (!kOpcodeInfo.IsCallAssigned(GetOpCode())) {
        return nullptr;
    }
    const CallReturnVector &nRets = this->GetReturnVec();
    if (nRets.size() == 1) {
        StIdx stIdx = nRets.begin()->first;
        RegFieldPair regFieldPair = nRets.begin()->second;
        if (!regFieldPair.IsReg()) {
            const MIRFunction *mirFunc = mod.CurFunction();
            const MIRSymbol *st = mirFunc->GetLocalOrGlobalSymbol(stIdx);
            return st;
        }
    }
    return nullptr;
}

#ifdef ARK_LITECG_DEBUG
void CallNode::Dump(int32 indent, bool newline) const
{
    StmtNode::DumpBase(indent);
    if (tyIdx != 0u) {
        LogInfo::MapleLogger() << " ";
        GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx)->Dump(indent + 1);
    }
    CHECK(puIdx < GlobalTables::GetFunctionTable().GetFuncTable().size(), "index out of range in CallNode::Dump");
    MIRFunction *func = GlobalTables::GetFunctionTable().GetFunctionFromPuidx(puIdx);
    DumpCallConvInfo();
    LogInfo::MapleLogger() << " &" << func->GetName();
    NaryOpnds::Dump(indent);
    DeoptBundleInfo::Dump(indent);
    if (kOpcodeInfo.IsCallAssigned(GetOpCode())) {
        DumpCallReturns(*theMIRModule, this->GetReturnVec(), indent);
    } else if (newline) {
        LogInfo::MapleLogger() << '\n';
    }
}
#endif

MIRType *IcallNode::GetCallReturnType()
{
    if (op == OP_icall || op == OP_icallassigned) {
        return GlobalTables::GetTypeTable().GetTypeFromTyIdx(retTyIdx);
    }
    // icallproto or icallprotoassigned
    MIRFuncType *funcType = static_cast<MIRFuncType *>(GlobalTables::GetTypeTable().GetTypeFromTyIdx(retTyIdx));
    return GlobalTables::GetTypeTable().GetTypeFromTyIdx(funcType->GetRetTyIdx());
}

const MIRSymbol *IcallNode::GetCallReturnSymbol(const MIRModule &mod) const
{
    if (!kOpcodeInfo.IsCallAssigned(GetOpCode())) {
        return nullptr;
    }
    const CallReturnVector &nRets = this->GetReturnVec();
    if (nRets.size() == 1) {
        StIdx stIdx = nRets.begin()->first;
        RegFieldPair regFieldPair = nRets.begin()->second;
        if (!regFieldPair.IsReg()) {
            const MIRFunction *mirFunc = mod.CurFunction();
            const MIRSymbol *st = mirFunc->GetLocalOrGlobalSymbol(stIdx);
            return st;
        }
    }
    return nullptr;
}

#ifdef ARK_LITECG_DEBUG
void IcallNode::Dump(int32 indent, bool newline) const
{
    StmtNode::DumpBase(indent);
    DumpCallConvInfo();
    if (op == OP_icallproto || op == OP_icallprotoassigned) {
        LogInfo::MapleLogger() << " ";
        GlobalTables::GetTypeTable().GetTypeFromTyIdx(retTyIdx)->Dump(indent + 1);
    }
    NaryOpnds::Dump(indent);
    DeoptBundleInfo::Dump(indent);
    if (kOpcodeInfo.IsCallAssigned(GetOpCode())) {
        DumpCallReturns(*theMIRModule, this->returnValues, indent);
    } else if (newline) {
        LogInfo::MapleLogger() << '\n';
    }
}
#endif

MIRType *IntrinsiccallNode::GetCallReturnType()
{
    CHECK_FATAL(intrinsic < INTRN_LAST, "Index out of bound in IntrinsiccallNode::GetCallReturnType");
    IntrinDesc *intrinDesc = &IntrinDesc::intrinTable[intrinsic];
    return intrinDesc->GetReturnType();
}

#ifdef ARK_LITECG_DEBUG
void IntrinsiccallNode::Dump(int32 indent, bool newline) const
{
    StmtNode::DumpBase(indent);
    if (tyIdx != 0u) {
        LogInfo::MapleLogger() << " ";
        GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx)->Dump(indent + 1);
    }
    if (GetOpCode() == OP_intrinsiccall || GetOpCode() == OP_intrinsiccallassigned ||
        GetOpCode() == OP_intrinsiccallwithtype || GetOpCode() == OP_intrinsiccallwithtypeassigned) {
        LogInfo::MapleLogger() << " " << GetIntrinsicName(intrinsic);
    } else {
        LogInfo::MapleLogger() << " " << intrinsic;
    }
    NaryOpnds::Dump(indent);
    if (kOpcodeInfo.IsCallAssigned(GetOpCode())) {
        DumpCallReturns(*theMIRModule, this->GetReturnVec(), indent);
    } else if (newline) {
        LogInfo::MapleLogger() << '\n';
    }
}

void BlockNode::Dump(int32 indent, const MIRSymbolTable *theSymTab, MIRPregTable *thePregTab, bool withInfo,
                     bool isFuncbody, MIRFlavor flavor) const
{
    if (!withInfo) {
        LogInfo::MapleLogger() << " {\n";
    }
    // output puid for debugging purpose
    DEBUG_ASSERT(theMIRModule->CurFunction() != nullptr, "theMIRModule->CurFunction() should not be nullptr");
    if (isFuncbody) {
        theMIRModule->CurFunction()->DumpFuncBody(indent);
        if (theSymTab != nullptr || thePregTab != nullptr) {
            // print the locally declared type names
            if (theMIRModule->CurFunction()->HaveTypeNameTab()) {
                constexpr int32 spaceNum = 2;
                for (auto it : theMIRModule->CurFunction()->GetGStrIdxToTyIdxMap()) {
                    const std::string &name = GlobalTables::GetStrTable().GetStringFromStrIdx(it.first);
                    MIRType *type = GlobalTables::GetTypeTable().GetTypeFromTyIdx(it.second);
                    PrintIndentation(indent + 1);
                    LogInfo::MapleLogger() << "type %" << name << " ";
                    if (type->GetKind() != kTypeByName) {
                        type->Dump(indent + spaceNum, true);
                    } else {
                        type->Dump(indent + spaceNum);
                    }
                    LogInfo::MapleLogger() << '\n';
                }
            }
            // print the locally declared variables
            DEBUG_ASSERT(theSymTab != nullptr, "theSymTab should not be nullptr");
            theSymTab->Dump(true, indent + 1, false, flavor); /* first:isLocal, third:printDeleted */
            if (thePregTab != nullptr) {
                thePregTab->DumpPregsWithTypes(indent + 1);
            }
        }
        LogInfo::MapleLogger() << '\n';
    }
    srcPosition.DumpLoc(lastPrintedLineNum, lastPrintedColumnNum);
    for (auto &stmt : GetStmtNodes()) {
        stmt.Dump(indent + 1);
    }
    PrintIndentation(indent);
    LogInfo::MapleLogger() << "}\n";
}

void LabelNode::Dump(int32) const
{
    DEBUG_ASSERT(theMIRModule->CurFunction() != nullptr, "theMIRModule->CurFunction() should not be nullptr");
    if (theMIRModule->CurFunction()->WithLocInfo()) {
        srcPosition.DumpLoc(lastPrintedLineNum, lastPrintedColumnNum);
    }
    LogInfo::MapleLogger() << "@" << theMIRModule->CurFunction()->GetLabelName(labelIdx) << "\n";
}

void CommentNode::Dump(int32 indent) const
{
    srcPosition.DumpLoc(lastPrintedLineNum, lastPrintedColumnNum);
    PrintIndentation(indent);
    LogInfo::MapleLogger() << "#" << comment << '\n';
}
#endif

void EmitStr(const MapleString &mplStr)
{
    const char *str = mplStr.c_str();
    size_t len = mplStr.length();
    LogInfo::MapleLogger() << "\"";

    // don't expand special character; convert all \s to \\s in string
    for (size_t i = 0; i < len; ++i) {
        /* Referred to GNU AS: 3.6.1.1 Strings */
        constexpr int kBufSize = 5;
        constexpr int kFirstChar = 0;
        constexpr int kSecondChar = 1;
        constexpr int kThirdChar = 2;
        constexpr int kLastChar = 4;
        char buf[kBufSize];
        if (isprint(*str)) {
            buf[kFirstChar] = *str;
            buf[kSecondChar] = 0;
            if (*str == '\\' || *str == '\"') {
                buf[kFirstChar] = '\\';
                buf[kSecondChar] = *str;
                buf[kThirdChar] = 0;
            }
            LogInfo::MapleLogger() << buf;
        } else if (*str == '\b') {
            LogInfo::MapleLogger() << "\\b";
        } else if (*str == '\n') {
            LogInfo::MapleLogger() << "\\n";
        } else if (*str == '\r') {
            LogInfo::MapleLogger() << "\\r";
        } else if (*str == '\t') {
            LogInfo::MapleLogger() << "\\t";
        } else if (*str == '\0') {
            buf[kFirstChar] = '\\';
            buf[kSecondChar] = '0';
            buf[kThirdChar] = 0;
            LogInfo::MapleLogger() << buf;
        } else {
            /* all others, print as number */
            int ret = snprintf_s(buf, sizeof(buf), kBufSize - 1, "\\%03o", static_cast<unsigned char>(*str) & 0xFF);
            if (ret < 0) {
                FATAL(kLncFatal, "snprintf_s failed");
            }
            buf[kLastChar] = '\0';
            LogInfo::MapleLogger() << buf;
        }
        str++;
    }

    LogInfo::MapleLogger() << "\"\n";
}

AsmNode *AsmNode::CloneTree(MapleAllocator &allocator) const
{
    auto *node = allocator.GetMemPool()->New<AsmNode>(allocator, *this);
    for (size_t i = 0; i < GetNopndSize(); ++i) {
        node->GetNopnd().push_back(GetNopndAt(i)->CloneTree(allocator));
    }
    for (size_t i = 0; i < inputConstraints.size(); ++i) {
        node->inputConstraints.push_back(inputConstraints[i]);
    }
    for (size_t i = 0; i < asmOutputs.size(); ++i) {
        node->asmOutputs.push_back(asmOutputs[i]);
    }
    for (size_t i = 0; i < outputConstraints.size(); ++i) {
        node->outputConstraints.push_back(outputConstraints[i]);
    }
    for (size_t i = 0; i < clobberList.size(); ++i) {
        node->clobberList.push_back(clobberList[i]);
    }
    for (size_t i = 0; i < gotoLabels.size(); ++i) {
        node->gotoLabels.push_back(gotoLabels[i]);
    }
    node->SetNumOpnds(static_cast<uint8>(GetNopndSize()));
    return node;
}

#ifdef ARK_LITECG_DEBUG
void AsmNode::DumpOutputs(int32 indent, std::string &uStr) const
{
    PrintIndentation(indent + 1);
    LogInfo::MapleLogger() << " :";
    size_t numOutputs = asmOutputs.size();

    const MIRFunction *mirFunc = theMIRModule->CurFunction();
    if (numOutputs == 0) {
        LogInfo::MapleLogger() << '\n';
    } else {
        for (size_t i = 0; i < numOutputs; i++) {
            if (i != 0) {
                PrintIndentation(indent + 2);  // Increase the indent by 2 bytes.
            }
            uStr = GlobalTables::GetUStrTable().GetStringFromStrIdx(outputConstraints[i]);
            PrintString(uStr);
            LogInfo::MapleLogger() << " ";
            StIdx stIdx = asmOutputs[i].first;
            RegFieldPair regFieldPair = asmOutputs[i].second;
            if (!regFieldPair.IsReg()) {
                FieldID fieldID = regFieldPair.GetFieldID();
                LogInfo::MapleLogger() << "dassign";
                const MIRSymbol *st = mirFunc->GetLocalOrGlobalSymbol(stIdx);
                DEBUG_ASSERT(st != nullptr, "st is null");
                LogInfo::MapleLogger() << (stIdx.Islocal() ? " %" : " $");
                LogInfo::MapleLogger() << st->GetName() << " " << fieldID;
            } else {
                PregIdx regIdx = regFieldPair.GetPregIdx();
                const MIRPreg *mirPreg = mirFunc->GetPregItem(static_cast<PregIdx>(regIdx));
                DEBUG_ASSERT(mirPreg != nullptr, "mirPreg is null");
                LogInfo::MapleLogger() << "regassign"
                                       << " " << GetPrimTypeName(mirPreg->GetPrimType());
                LogInfo::MapleLogger() << " %" << mirPreg->GetPregNo();
            }
            if (i != numOutputs - 1) {
                LogInfo::MapleLogger() << ',';
            }
            LogInfo::MapleLogger() << '\n';
        }
    }
}

void AsmNode::DumpInputOperands(int32 indent, std::string &uStr) const
{
    PrintIndentation(indent + 1);
    LogInfo::MapleLogger() << " :";
    if (numOpnds == 0) {
        LogInfo::MapleLogger() << '\n';
    } else {
        for (size_t i = 0; i < numOpnds; i++) {
            if (i != 0) {
                PrintIndentation(indent + 2);  // Increase the indent by 2 bytes.
            }
            uStr = GlobalTables::GetUStrTable().GetStringFromStrIdx(inputConstraints[i]);
            PrintString(uStr);
            LogInfo::MapleLogger() << " (";
            GetNopndAt(i)->Dump(indent + 4);  // Increase the indent by 4 bytes.
            LogInfo::MapleLogger() << ")";
            if (i != static_cast<size_t>(static_cast<int64>(numOpnds - 1))) {
                LogInfo::MapleLogger() << ',';
            }
            LogInfo::MapleLogger() << "\n";
        }
    }
}

void AsmNode::Dump(int32 indent) const
{
    srcPosition.DumpLoc(lastPrintedLineNum, lastPrintedColumnNum);
    PrintIndentation(indent);
    LogInfo::MapleLogger() << kOpcodeInfo.GetName(op);
    if (GetQualifier(kASMvolatile)) {
        LogInfo::MapleLogger() << " volatile";
    }
    if (GetQualifier(kASMinline)) {
        LogInfo::MapleLogger() << " inline";
    }
    if (GetQualifier(kASMgoto)) {
        LogInfo::MapleLogger() << " goto";
    }
    LogInfo::MapleLogger() << " { ";
    EmitStr(asmString);
    // print outputs
    std::string uStr;
    DumpOutputs(indent, uStr);
    // print input operands
    DumpInputOperands(indent, uStr);
    // print clobber list
    PrintIndentation(indent + 1);
    LogInfo::MapleLogger() << " :";
    for (size_t i = 0; i < clobberList.size(); i++) {
        uStr = GlobalTables::GetUStrTable().GetStringFromStrIdx(clobberList[i]);
        PrintString(uStr);
        DEBUG_ASSERT(clobberList.size() > 0, "must not be zero");
        if (i != clobberList.size() - 1) {
            LogInfo::MapleLogger() << ',';
        }
    }
    LogInfo::MapleLogger() << '\n';
    // print labels
    PrintIndentation(indent + 1);
    LogInfo::MapleLogger() << " :";
    size_t labelSize = gotoLabels.size();
    DEBUG_ASSERT(theMIRModule->CurFunction() != nullptr, "theMIRModule->CurFunction() should not be nullptr");
    for (size_t i = 0; i < labelSize; i++) {
        LabelIdx offset = gotoLabels[i];
        LogInfo::MapleLogger() << " @" << theMIRModule->CurFunction()->GetLabelName(offset);
        if (i != labelSize - 1) {
            LogInfo::MapleLogger() << ',';
        }
    }
    LogInfo::MapleLogger() << " }\n";
}
#endif

enum PTYGroup {
    kPTYGi32u32a32,
    kPTYGi32u32a32PtrRef,
    kPTYGi64u64a64,
    kPTYGPtrRef,
    kPTYGDynall,
    kPTYGu1,
    kPTYGSimpleObj,
    kPTYGSimpleStr,
    kPTYGOthers
};

uint8 GetPTYGroup(PrimType primType)
{
    switch (primType) {
        case PTY_i32:
        case PTY_u32:
        case PTY_a32:
            return kPTYGi32u32a32;
        case PTY_i64:
        case PTY_u64:
        case PTY_a64:
            return kPTYGi64u64a64;
        case PTY_ref:
        case PTY_ptr:
            return kPTYGPtrRef;
        case PTY_u1:
            return kPTYGu1;
        default:
            return kPTYGOthers;
    }
}

uint8 GetCompGroupID(const BaseNode &opnd)
{
    return GetPTYGroup(opnd.GetPrimType());
}

inline MIRTypeKind GetTypeKind(StIdx stIdx)
{
    const MIRSymbol *var = theMIRModule->CurFunction()->GetLocalOrGlobalSymbol(stIdx);
    DEBUG_ASSERT(var != nullptr, "null ptr check");
    MIRType *type = var->GetType();
    DEBUG_ASSERT(type != nullptr, "null ptr check");
    return type->GetKind();
}

inline MIRTypeKind GetTypeKind(TyIdx tyIdx)
{
    MIRType *type = GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx);
    DEBUG_ASSERT(type != nullptr, "null ptr check");
    return type->GetKind();
}

inline MIRType *GetPointedMIRType(TyIdx tyIdx)
{
    MIRType *type = GlobalTables::GetTypeTable().GetTypeFromTyIdx(tyIdx);
    CHECK_FATAL(type->GetKind() == kTypePointer, "TyIdx: %d is not pointer type", static_cast<uint32>(tyIdx));
    auto *ptrType = static_cast<MIRPtrType *>(type);
    return ptrType->GetPointedType();
}

inline MIRTypeKind GetPointedTypeKind(TyIdx tyIdx)
{
    MIRType *pointedType = GetPointedMIRType(tyIdx);
    DEBUG_ASSERT(pointedType != nullptr, "null ptr check");
    return pointedType->GetKind();
}

MIRTypeKind GetFieldTypeKind(MIRStructType *structType, FieldID fieldId)
{
    TyIdx fieldTyIdx;
    if (fieldId > 0) {
        MIRType *mirType = structType->GetFieldType(fieldId);
        fieldTyIdx = mirType->GetTypeIndex();
    } else {
        DEBUG_ASSERT(static_cast<unsigned>(-fieldId) < structType->GetParentFieldsSize() + 1,
                     "array index out of range");
        fieldTyIdx = structType->GetParentFieldsElemt(-fieldId - 1).second.first;
    }
    return GetTypeKind(fieldTyIdx);
}

inline bool IsStructureTypeKind(MIRTypeKind kind)
{
    return kind == kTypeStruct || kind == kTypeStructIncomplete || kind == kTypeUnion || kind == kTypeClass ||
           kind == kTypeClassIncomplete || kind == kTypeInterface || kind == kTypeInterfaceIncomplete;
}

bool IsSignedType(const BaseNode *opnd)
{
    switch (opnd->GetPrimType()) {
        case PTY_i32:
        case PTY_i64:
        case PTY_f32:
        case PTY_f64:
            return true;
        default:
            break;
    }
    return false;
}

std::string SafetyCheckStmtNode::GetFuncName() const
{
    return GlobalTables::GetStrTable().GetStringFromStrIdx(funcNameIdx);
}

bool UnaryNode::IsSameContent(const BaseNode *node) const
{
    auto *unaryNode = dynamic_cast<const UnaryNode *>(node);
    if ((this == unaryNode) || (unaryNode != nullptr && (GetOpCode() == unaryNode->GetOpCode()) &&
                                (GetPrimType() == unaryNode->GetPrimType()) &&
                                (uOpnd && unaryNode->Opnd(0) && uOpnd->IsSameContent(unaryNode->Opnd(0))))) {
        return true;
    } else {
        return false;
    }
}

bool TypeCvtNode::IsSameContent(const BaseNode *node) const
{
    auto *tyCvtNode = dynamic_cast<const TypeCvtNode *>(node);
    if ((this == tyCvtNode) ||
        (tyCvtNode != nullptr && (fromPrimType == tyCvtNode->FromType()) && UnaryNode::IsSameContent(tyCvtNode))) {
        return true;
    } else {
        return false;
    }
}

bool IreadNode::IsSameContent(const BaseNode *node) const
{
    auto *ireadNode = dynamic_cast<const IreadNode *>(node);
    if ((this == ireadNode) || (ireadNode != nullptr && (tyIdx == ireadNode->GetTyIdx()) &&
                                (fieldID == ireadNode->GetFieldID()) && UnaryNode::IsSameContent(ireadNode))) {
        return true;
    } else {
        return false;
    }
}

bool IreadoffNode::IsSameContent(const BaseNode *node) const
{
    auto *ireadoffNode = dynamic_cast<const IreadoffNode *>(node);
    if ((this == ireadoffNode) || (ireadoffNode != nullptr && (GetOffset() == ireadoffNode->GetOffset()) &&
                                   UnaryNode::IsSameContent(ireadoffNode))) {
        return true;
    } else {
        return false;
    }
}

bool BinaryOpnds::IsSameContent(const BaseNode *node) const
{
    auto *binaryOpnds = dynamic_cast<const BinaryOpnds *>(node);
    if ((this == binaryOpnds) || (binaryOpnds != nullptr && GetBOpnd(0)->IsSameContent(binaryOpnds->GetBOpnd(0)) &&
                                  GetBOpnd(1)->IsSameContent(binaryOpnds->GetBOpnd(1)))) {
        return true;
    } else {
        return false;
    }
}

bool BinaryNode::IsSameContent(const BaseNode *node) const
{
    auto *binaryNode = dynamic_cast<const BinaryNode *>(node);
    if ((this == binaryNode) ||
        (binaryNode != nullptr && (GetOpCode() == binaryNode->GetOpCode()) &&
         (GetPrimType() == binaryNode->GetPrimType()) && BinaryOpnds::IsSameContent(binaryNode))) {
        return true;
    } else {
        return false;
    }
}

bool ConstvalNode::IsSameContent(const BaseNode *node) const
{
    auto *constvalNode = dynamic_cast<const ConstvalNode *>(node);
    if (this == constvalNode) {
        return true;
    }
    if (constvalNode == nullptr) {
        return false;
    }
    const MIRConst *mirConst = constvalNode->GetConstVal();
    if (constVal == mirConst) {
        return true;
    }
    if (constVal->GetKind() != mirConst->GetKind()) {
        return false;
    }
    if (constVal->GetKind() == kConstInt) {
        // integer may differ in primtype, and they may be different MIRIntConst Node
        return static_cast<MIRIntConst *>(constVal)->GetValue() ==
               static_cast<const MIRIntConst *>(mirConst)->GetValue();
    } else {
        return false;
    }
}

bool ConststrNode::IsSameContent(const BaseNode *node) const
{
    if (node->GetOpCode() != OP_conststr) {
        return false;
    }
    auto *cstrNode = static_cast<const ConststrNode *>(node);
    return strIdx == cstrNode->strIdx;
}

bool Conststr16Node::IsSameContent(const BaseNode *node) const
{
    if (node->GetOpCode() != OP_conststr16) {
        return false;
    }
    auto *cstr16Node = static_cast<const Conststr16Node *>(node);
    return strIdx == cstr16Node->strIdx;
}

bool AddrofNode::IsSameContent(const BaseNode *node) const
{
    auto *addrofNode = dynamic_cast<const AddrofNode *>(node);
    if ((this == addrofNode) ||
        (addrofNode != nullptr && (GetOpCode() == addrofNode->GetOpCode()) &&
         (GetPrimType() == addrofNode->GetPrimType()) && (GetNumOpnds() == addrofNode->GetNumOpnds()) &&
         (stIdx.FullIdx() == addrofNode->GetStIdx().FullIdx()) && (fieldID == addrofNode->GetFieldID()))) {
        return true;
    } else {
        return false;
    }
}

bool DreadoffNode::IsSameContent(const BaseNode *node) const
{
    auto *dreaddoffNode = dynamic_cast<const DreadoffNode *>(node);
    if ((this == dreaddoffNode) || (dreaddoffNode != nullptr && (GetOpCode() == dreaddoffNode->GetOpCode()) &&
                                    (GetPrimType() == dreaddoffNode->GetPrimType()) &&
                                    (stIdx == dreaddoffNode->stIdx) && (offset == dreaddoffNode->offset))) {
        return true;
    } else {
        return false;
    }
}

bool RegreadNode::IsSameContent(const BaseNode *node) const
{
    auto *regreadNode = dynamic_cast<const RegreadNode *>(node);
    if ((this == regreadNode) ||
        (regreadNode != nullptr && (GetOpCode() == regreadNode->GetOpCode()) &&
         (GetPrimType() == regreadNode->GetPrimType()) && (regIdx == regreadNode->GetRegIdx()))) {
        return true;
    } else {
        return false;
    }
}

bool AddroffuncNode::IsSameContent(const BaseNode *node) const
{
    auto *addroffuncNode = dynamic_cast<const AddroffuncNode *>(node);
    if ((this == addroffuncNode) ||
        (addroffuncNode != nullptr && (GetOpCode() == addroffuncNode->GetOpCode()) &&
         (GetPrimType() == addroffuncNode->GetPrimType()) && (puIdx == addroffuncNode->GetPUIdx()))) {
        return true;
    } else {
        return false;
    }
}

bool AddroflabelNode::IsSameContent(const BaseNode *node) const
{
    auto *addroflabelNode = dynamic_cast<const AddroflabelNode *>(node);
    if ((this == addroflabelNode) ||
        (addroflabelNode != nullptr && (GetOpCode() == addroflabelNode->GetOpCode()) &&
         (GetPrimType() == addroflabelNode->GetPrimType()) && (offset == addroflabelNode->GetOffset()))) {
        return true;
    } else {
        return false;
    }
}
}  // namespace maple
