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

#ifndef ECMASCRIPT_PGO_PROFILER_TYPES_PGO_PROFILER_TYPE_H
#define ECMASCRIPT_PGO_PROFILER_TYPES_PGO_PROFILER_TYPE_H

#include <stdint.h>
#include <string>
#include <variant>
#include <vector>

#include "ecmascript/elements.h"
#include "ecmascript/mem/region.h"
#include "ecmascript/log_wrapper.h"
#include "ecmascript/pgo_profiler/pgo_utils.h"
#include "ecmascript/pgo_profiler/types/pgo_profile_type.h"
#include "libpandabase/utils/bit_field.h"
#include "macros.h"

namespace panda::ecmascript::pgo {
class PGOContext;
class PGOType {
public:
    enum class TypeKind : uint8_t {
        SCALAR_OP_TYPE,
        RW_OP_TYPE,
        DEFINE_OP_TYPE,
    };
    PGOType() = default;
    explicit PGOType(TypeKind kind) : kind_(kind) {}

    bool IsScalarOpType() const
    {
        return kind_ == TypeKind::SCALAR_OP_TYPE;
    }

    bool IsRwOpType() const
    {
        return kind_ == TypeKind::RW_OP_TYPE;
    }

    bool IsDefineOpType() const
    {
        return kind_ == TypeKind::DEFINE_OP_TYPE;
    }

private:
    TypeKind kind_ {TypeKind::SCALAR_OP_TYPE};
};

/**
 * | INT    \          -> INT_OVERFLOW \
 * |           NUMBER                    NUMBER_HETEROE1
 * | DOUBLE /                          /
 */
template <typename PGOProfileType>
class PGOSampleTemplate : public PGOType {
public:
    static constexpr int WEIGHT_BITS = 11;
    static constexpr int WEIGHT_START_BIT = 10;
    static constexpr int WEIGHT_TRUE_START_BIT = WEIGHT_START_BIT + WEIGHT_BITS;
    static constexpr int WEIGHT_MASK = (1 << WEIGHT_BITS) - 1;

    enum class Type : uint32_t {
        NONE = 0x0ULL,
        INT = 0x1ULL,                        // 00000001
        INT_OVERFLOW = (0x1ULL << 1) | INT,  // 00000011
        DOUBLE = 0x1ULL << 2,                // 00000100
        NUMBER = INT | DOUBLE,               // 00000101
        NUMBER1 = INT_OVERFLOW | DOUBLE,     // 00000111
        BOOLEAN = 0x1ULL << 3,
        UNDEFINED_OR_NULL = 0x1ULL << 4,
        SPECIAL = 0x1ULL << 5,
        BOOLEAN_OR_SPECIAL = BOOLEAN | SPECIAL,
        STRING = 0x1ULL << 6,
        NUMBER_OR_STRING = NUMBER | STRING,
        BIG_INT = 0x1ULL << 7,
        HEAP_OBJECT = 0x1ULL << 8,
        HEAP_OR_UNDEFINED_OR_NULL = HEAP_OBJECT | UNDEFINED_OR_NULL,
        ANY = (0x1ULL << WEIGHT_START_BIT) - 1,
    };

    PGOSampleTemplate() : type_(Type::NONE) {};

    explicit PGOSampleTemplate(Type type) : type_(type) {};
    explicit PGOSampleTemplate(uint32_t type) : type_(Type(type)) {};
    explicit PGOSampleTemplate(PGOProfileType type) : type_(type) {}

    template <typename FromType>
    static PGOSampleTemplate ConvertFrom(PGOContext &context, const FromType &from)
    {
        if (from.IsProfileType()) {
            return PGOSampleTemplate(PGOProfileType(context, from.GetProfileType()));
        }
        return PGOSampleTemplate(static_cast<PGOSampleTemplate::Type>(from.GetType()));
    }

    static PGOSampleTemplate CreateProfileType(ApEntityId recordId, int32_t profileType,
                                               typename ProfileType::Kind kind = ProfileType::Kind::ClassId,
                                               bool isRoot = false)
    {
        return PGOSampleTemplate(PGOProfileType(recordId, profileType, kind, isRoot));
    }

    static PGOSampleTemplate NoneType()
    {
        return PGOSampleTemplate(Type::NONE);
    }

    static int32_t None()
    {
        return static_cast<int32_t>(Type::NONE);
    }

    static int32_t AnyType()
    {
        return static_cast<int32_t>(Type::ANY);
    }

    static int32_t IntType()
    {
        return static_cast<int32_t>(Type::INT);
    }

    static int32_t IntOverFlowType()
    {
        return static_cast<int32_t>(Type::INT_OVERFLOW);
    }

    static int32_t DoubleType()
    {
        return static_cast<int32_t>(Type::DOUBLE);
    }

    static int32_t NumberType()
    {
        return static_cast<int32_t>(Type::NUMBER);
    }

    static int32_t HeapObjectType()
    {
        return static_cast<int32_t>(Type::HEAP_OBJECT);
    }

    static int32_t UndefineOrNullType()
    {
        return static_cast<int32_t>(Type::UNDEFINED_OR_NULL);
    }

    static int32_t BooleanType()
    {
        return static_cast<int32_t>(Type::BOOLEAN);
    }

    static int32_t StringType()
    {
        return static_cast<int32_t>(Type::STRING);
    }

    static int32_t NumberOrStringType()
    {
        return static_cast<int32_t>(Type::NUMBER_OR_STRING);
    }

    static int32_t BigIntType()
    {
        return static_cast<int32_t>(Type::BIG_INT);
    }

    static int32_t SpecialType()
    {
        return static_cast<int32_t>(Type::SPECIAL);
    }

    static int32_t CombineType(int32_t curType, int32_t newType)
    {
        return static_cast<int32_t>(static_cast<uint32_t>(curType) | static_cast<uint32_t>(newType));
    }

    static PGOSampleTemplate NoneProfileType()
    {
        return PGOSampleTemplate(PGOProfileType());
    }

    PGOSampleTemplate CombineType(PGOSampleTemplate type)
    {
        if (IsPrimitiveType()) {
            auto oldType = static_cast<uint32_t>(std::get<Type>(type_));
            oldType = oldType & static_cast<uint32_t>(AnyType());
            type_ =
                Type(oldType | static_cast<uint32_t>(std::get<Type>(type.type_)));
        } else {
            this->SetType(type);
        }
        return *this;
    }

    PGOSampleTemplate CombineCallTargetType(PGOSampleTemplate type)
    {
        ASSERT(type_.index() == 1);
        ProfileType::Kind oldKind = GetProfileType().GetKind();
        ProfileType::Kind newKind = type.GetProfileType().GetKind();
        uint32_t oldMethodId = GetProfileType().GetId();
        uint32_t newMethodId = type.GetProfileType().GetId();
        // If we have recorded a valid method if before, invalidate it.
        if ((oldMethodId != 0) &&
            ((oldKind != newKind) || (oldMethodId != newMethodId))) {
            type_ = ProfileType::PROFILE_TYPE_NONE;
        }
        return *this;
    }

    void SetType(PGOSampleTemplate type)
    {
        type_ = type.type_;
    }

    std::string GetTypeString() const
    {
        if (IsPrimitiveType()) {
            return std::to_string(static_cast<uint32_t>(std::get<Type>(type_)));
        } else {
            return std::get<PGOProfileType>(type_).GetTypeString();
        }
    }

    std::string ToString() const
    {
        if (IsPrimitiveType()) {
            auto type = std::get<Type>(type_);
            switch (type) {
                case Type::NONE:
                    return "none";
                case Type::INT:
                    return "int";
                case Type::INT_OVERFLOW:
                    return "int_overflow";
                case Type::DOUBLE:
                    return "double";
                case Type::NUMBER:
                    return "number";
                case Type::NUMBER1:
                    return "number1";
                case Type::BOOLEAN:
                    return "boolean";
                case Type::UNDEFINED_OR_NULL:
                    return "undefined_or_null";
                case Type::SPECIAL:
                    return "special";
                case Type::BOOLEAN_OR_SPECIAL:
                    return "boolean_or_special";
                case Type::STRING:
                    return "string";
                case Type::BIG_INT:
                    return "big_int";
                case Type::HEAP_OBJECT:
                    return "heap_object";
                case Type::HEAP_OR_UNDEFINED_OR_NULL:
                    return "heap_or_undefined_or_null";
                case Type::ANY:
                    return "any";
                default:
                    LOG_ECMA(FATAL) << "this branch is unreachable";
                    UNREACHABLE();
            }
        }
        LOG_ECMA(FATAL) << "this branch is unreachable";
        UNREACHABLE();
    }

    bool IsProfileType() const
    {
        return type_.index() == 1;
    }

    PGOProfileType GetProfileType() const
    {
        ASSERT(IsProfileType());
        return std::get<PGOProfileType>(type_);
    }

    bool IsPrimitiveType() const
    {
        return type_.index() == 0;
    }

    Type GetPrimitiveType() const
    {
        ASSERT(IsPrimitiveType());
        auto type = static_cast<uint32_t>(std::get<Type>(type_));
        return Type(type & static_cast<uint32_t>(AnyType()));
    }

    uint32_t GetWeight() const
    {
        ASSERT(IsPrimitiveType());
        auto type = static_cast<uint32_t>(std::get<Type>(type_));
        return type >> WEIGHT_START_BIT;
    }

    bool IsAny() const
    {
        return IsPrimitiveType() && GetPrimitiveType() == Type::ANY;
    }

    bool IsNone() const
    {
        return IsPrimitiveType() && GetPrimitiveType() == Type::NONE;
    }

    bool IsInt() const
    {
        return IsPrimitiveType() && GetPrimitiveType() == Type::INT;
    }

    bool IsIntOverFlow() const
    {
        return IsPrimitiveType() && GetPrimitiveType() == Type::INT_OVERFLOW;
    }

    bool IsDouble() const
    {
        return IsPrimitiveType() && GetPrimitiveType() == Type::DOUBLE;
    }

    bool IsString() const
    {
        return IsPrimitiveType() && GetPrimitiveType() == Type::STRING;
    }

    bool IsBigInt() const
    {
        return type_.index() == 0 && GetPrimitiveType() == Type::BIG_INT;
    }

    bool IsBoolean() const
    {
        return type_.index() == 0 && GetPrimitiveType() == Type::BOOLEAN;
    }

    bool IsHeapObject() const
    {
        return type_.index() == 0 && GetPrimitiveType() == Type::HEAP_OBJECT;
    }

    bool IsNumber() const
    {
        if (type_.index() != 0) {
            return false;
        }
        auto primType = GetPrimitiveType();
        return primType == Type::NUMBER || primType == Type::NUMBER1;
    }

    bool IsNumberOrString() const
    {
        if (type_.index() != 0) {
            return false;
        }
        auto primType = GetPrimitiveType();
        return primType == Type::NUMBER_OR_STRING;
    }

    bool HasNumber() const
    {
        if (type_.index() != 0) {
            return false;
        }
        auto primType = GetPrimitiveType();
        switch (primType) {
            case Type::INT:
            case Type::INT_OVERFLOW:
            case Type::DOUBLE:
            case Type::NUMBER:
            case Type::NUMBER1:
                return true;
            default:
                return false;
        }
    }

    bool IsProfileTypeNone() const
    {
        return type_.index() == 1 && GetProfileType() == ProfileType::PROFILE_TYPE_NONE;
    }

    bool operator<(const PGOSampleTemplate &right) const
    {
        return type_ < right.type_;
    }

    bool operator!=(const PGOSampleTemplate &right) const
    {
        return type_ != right.type_;
    }

    bool operator==(const PGOSampleTemplate &right) const
    {
        return type_ == right.type_;
    }

    Type GetType() const
    {
        ASSERT(!IsProfileType());
        return std::get<Type>(type_);
    }

private:
    std::variant<Type, PGOProfileType> type_;
};

using PGOSampleType = PGOSampleTemplate<ProfileType>;
using PGOSampleTypeRef = PGOSampleTemplate<ProfileTypeRef>;

template <typename PGOProfileType>
class PGOProtoChainTemplate {
public:
    PGOProtoChainTemplate() = default;
    PGOProtoChainTemplate(int32_t size, int32_t count) : size_(size), count_(count) {};

    static PGOProtoChainTemplate *CreateProtoChain(std::vector<std::pair<PGOProfileType, PGOProfileType>> protoChain)
    {
        auto count = protoChain.size();
        size_t size = sizeof(PGOProtoChainTemplate);
        if (count != 0) {
            size += sizeof(PGOProfileType) * (count - 1) * 2;   // 2 means mul by 2
        }
        auto result = reinterpret_cast<PGOProtoChainTemplate *>(malloc(size));
        if (result == nullptr) {
            LOG_ECMA_MEM(FATAL) << "malloc failed";
            UNREACHABLE();
        }
        new (result) PGOProtoChainTemplate(size, count);
        PGOProfileType *curPt = &(result->rootType_);
        for (auto iter : protoChain) {
            *curPt = iter.first;
            curPt = curPt + 1;
            *curPt = iter.second;
            curPt = curPt + 1;
        }
        return result;
    }

    static void DeleteProtoChain(PGOProtoChainTemplate *protoChain)
    {
        free(protoChain);
    }

    template <typename FromType>
    static PGOProtoChainTemplate *ConvertFrom(PGOContext &context, FromType *from)
    {
        auto count = from->GetCount();
        size_t size = sizeof(PGOProtoChainTemplate);
        if (count != 0) {
            size += sizeof(PGOProfileType) * (static_cast<size_t>(count) - 1) * 2;   // 2 means mul by 2
        }
        auto result = reinterpret_cast<PGOProtoChainTemplate *>(malloc(size));
        if (result == nullptr) {
            LOG_ECMA_MEM(FATAL) << "malloc failed";
            UNREACHABLE();
        }
        new (result) PGOProtoChainTemplate(size, count);
        PGOProfileType *curPt = &(result->rootType_);
        from->IterateProtoChain([&context, &curPt] (auto rootType, auto childType) {
            *curPt = PGOProfileType(context, rootType);
            curPt = curPt + 1;
            *curPt = PGOProfileType(context, childType);
            curPt = curPt + 1;
        });
        return result;
    }

    template <typename Callback>
    void IterateProtoChain(Callback callback) const
    {
        for (int i = 0; i < count_; i++) {
            callback(*(&rootType_ + i), *(&rootType_ + i + 1));
        }
    }

    int32_t GetCount()
    {
        return count_;
    }

    int32_t Size()
    {
        return size_;
    }

private:
    int32_t size_;
    int32_t count_;
    PGOProfileType rootType_ {PGOProfileType()};
    PGOProfileType childType_ {PGOProfileType()};
};

using PGOProtoChain = PGOProtoChainTemplate<ProfileType>;
using PGOProtoChainRef = PGOProtoChainTemplate<ProfileTypeRef>;

enum class ProtoChainMarker : uint8_t {
    EXSIT,
    NOT_EXSIT,
};

template <typename PGOProfileType, typename PGOSampleType>
class PGOObjectTemplate {
public:
    PGOObjectTemplate() = default;
    PGOObjectTemplate(PGOProfileType type) : receiverType_(type) {}
    PGOObjectTemplate(PGOProfileType receiverRootType, PGOProfileType receiverType, PGOProfileType holdRootType,
        PGOProfileType holdType, PGOProfileType holdTraRootType,
        PGOProfileType holdTraType, PGOSampleType accessorMethod)
        : receiverRootType_(receiverRootType), receiverType_(receiverType), holdRootType_(holdRootType),
        holdType_(holdType), holdTraRootType_(holdTraRootType),
        holdTraType_(holdTraType), accessorMethod_(accessorMethod) {}

    void AddPrototypePt(std::vector<std::pair<PGOProfileType, PGOProfileType>> protoChain)
    {
        protoChainMarker_ = ProtoChainMarker::EXSIT;
        if (protoChain_ != nullptr) {
            PGOProtoChainTemplate<PGOProfileType>::DeleteProtoChain(protoChain_);
        }
        protoChain_ = PGOProtoChainTemplate<PGOProfileType>::CreateProtoChain(protoChain);
    }

    template <typename FromType>
    void ConvertFrom(PGOContext &context, const FromType &from)
    {
        receiverRootType_ = PGOProfileType(context, from.GetReceiverRootType());
        receiverType_ = PGOProfileType(context, from.GetReceiverType());
        holdRootType_ = PGOProfileType(context, from.GetHoldRootType());
        holdType_ = PGOProfileType(context, from.GetHoldType());
        holdTraRootType_ = PGOProfileType(context, from.GetHoldTraRootType());
        holdTraType_ = PGOProfileType(context, from.GetHoldTraType());
        accessorMethod_ = PGOSampleType::ConvertFrom(context, from.GetAccessorMethod());
        protoChainMarker_ = from.GetProtoChainMarker();
    }

    std::string GetInfoString() const
    {
        std::string result = "(receiverRoot";
        result += receiverRootType_.GetTypeString();
        result += ", receiver";
        result += receiverType_.GetTypeString();
        result += ", holdRoot";
        result += holdRootType_.GetTypeString();
        result += ", hold";
        result += holdType_.GetTypeString();
        result += ", holdTraRoot";
        result += holdTraRootType_.GetTypeString();
        result += ", holdTra";
        result += holdTraType_.GetTypeString();
        result += ", accessorMethod";
        result += accessorMethod_.GetTypeString();
        result += ")";
        return result;
    }

    PGOProfileType GetProfileType() const
    {
        return receiverType_;
    }

    PGOProfileType GetReceiverRootType() const
    {
        return receiverRootType_;
    }

    PGOProfileType GetReceiverType() const
    {
        return receiverType_;
    }

    PGOProfileType GetHoldRootType() const
    {
        return holdRootType_;
    }

    PGOProfileType GetHoldType() const
    {
        return holdType_;
    }

    PGOProfileType GetHoldTraRootType() const
    {
        return holdTraRootType_;
    }

    PGOProfileType GetHoldTraType() const
    {
        return holdTraType_;
    }

    PGOSampleType GetAccessorMethod() const
    {
        return accessorMethod_;
    }

    ProtoChainMarker GetProtoChainMarker() const
    {
        return protoChainMarker_;
    }

    PGOProtoChainTemplate<PGOProfileType> *GetProtoChain() const
    {
        return protoChain_;
    }

    void SetProtoChain(PGOProtoChainTemplate<PGOProfileType> *protoChain)
    {
        protoChain_ = protoChain;
    }

    bool IsNone() const
    {
        return receiverType_.IsNone();
    }

    bool IsMegaStateType() const
    {
        return receiverType_.IsMegaStateType();
    }

    bool InConstructor() const
    {
        return receiverType_.IsConstructor();
    }

    bool InElement() const
    {
        return receiverType_.IsBuiltinsArray();
    }

    bool operator<(const PGOObjectTemplate &right) const
    {
        return receiverRootType_ < right.receiverRootType_ || receiverType_ < right.receiverType_ ||
            holdRootType_ < right.holdRootType_ || holdType_ < right.holdType_ ||
            holdTraRootType_ < right.holdTraRootType_ || holdTraType_ < right.holdTraType_;
    }

    bool operator==(const PGOObjectTemplate &right) const
    {
        return receiverRootType_ == right.receiverRootType_ && receiverType_ == right.receiverType_ &&
            holdRootType_ == right.holdRootType_ && holdType_ == right.holdType_ &&
            holdTraRootType_ == right.holdTraRootType_ && holdTraType_ == right.holdTraType_;
    }

private:
    PGOProfileType receiverRootType_ { PGOProfileType() };
    PGOProfileType receiverType_ { PGOProfileType() };
    PGOProfileType holdRootType_ { PGOProfileType() };
    PGOProfileType holdType_ { PGOProfileType() };
    PGOProfileType holdTraRootType_ { PGOProfileType() };
    PGOProfileType holdTraType_ { PGOProfileType() };
    PGOSampleType accessorMethod_ { PGOSampleType() };
    ProtoChainMarker protoChainMarker_ {ProtoChainMarker::NOT_EXSIT};
    PGOProtoChainTemplate<PGOProfileType> *protoChain_ { nullptr };
};
using PGOObjectInfo = PGOObjectTemplate<ProfileType, PGOSampleType>;
using PGOObjectInfoRef = PGOObjectTemplate<ProfileTypeRef, PGOSampleTypeRef>;

template <typename PGOObjectInfoType>
class PGORWOpTemplate : public PGOType {
public:
    PGORWOpTemplate() : PGOType(TypeKind::RW_OP_TYPE) {};

    template <typename FromType>
    void ConvertFrom(PGOContext &context, const FromType &from)
    {
        count_ = std::min(from.GetCount(), static_cast<uint32_t>(POLY_CASE_NUM));
        for (uint32_t index = 0; index < count_; index++) {
            infos_[index].ConvertFrom(context, from.GetObjectInfo(index));
        }
    }

    void Merge(const PGORWOpTemplate &type)
    {
        for (uint32_t i = 0; i < type.count_; i++) {
            AddObjectInfo(type.infos_[i]);
        }
    }

    void AddObjectInfo(const PGOObjectInfoType &info)
    {
        if (info.IsNone()) {
            return;
        }
        if (info.IsMegaStateType()) {
            count_ = 0;
            return;
        }
        uint32_t count = 0;
        for (; count < count_; count++) {
            if (infos_[count] == info) {
                return;
            }
        }
        if (count < static_cast<uint32_t>(POLY_CASE_NUM)) {
            infos_[count] = info;
            count_++;
        } else {
            LOG_ECMA(DEBUG) << "Class type exceeds 4, discard";
        }
    }

    const PGOObjectInfoType &GetObjectInfo(uint32_t index) const
    {
        ASSERT(index < count_);
        return infos_[index];
    }

    uint32_t GetCount() const
    {
        return count_;
    }

private:
    static constexpr int POLY_CASE_NUM = 4;
    uint32_t count_ = 0;
    PGOObjectInfoType infos_[POLY_CASE_NUM];
};
using PGORWOpType = PGORWOpTemplate<PGOObjectInfo>;
using PGORWOpTypeRef = PGORWOpTemplate<PGOObjectInfoRef>;

template <typename PGOProfileType>
class PGODefineOpTemplate : public PGOType {
public:
    PGODefineOpTemplate() : PGOType(TypeKind::DEFINE_OP_TYPE), type_(PGOProfileType()) {};
    explicit PGODefineOpTemplate(PGOProfileType type) : PGOType(TypeKind::DEFINE_OP_TYPE), type_(type) {};

    template <typename FromType>
    void ConvertFrom(PGOContext &context, const FromType &from)
    {
        type_ = PGOProfileType(context, from.GetProfileType());
        ctorPt_ = PGOProfileType(context, from.GetCtorPt());
        protoPt_ = PGOProfileType(context, from.GetProtoTypePt());
        kind_ = from.GetElementsKind();
        elementsLength_ = from.GetElementsLength();
        spaceFlag_ = from.GetSpaceFlag();
    }

    std::string GetTypeString() const
    {
        std::string result = "(local";
        result += type_.GetTypeString();
        result += ", ctor";
        result += ctorPt_.GetTypeString();
        result += ", proto";
        result += protoPt_.GetTypeString();
        result += ", elementsKind:";
        result += std::to_string(static_cast<int32_t>(kind_));
        if (elementsLength_ > 0 && spaceFlag_ != RegionSpaceFlag::UNINITIALIZED) {
            result += ", size: ";
            result += std::to_string(elementsLength_);
            result += ", space; ";
            result += ToSpaceTypeName(spaceFlag_);
        }
        return result;
    }

    bool IsNone() const
    {
        return type_.IsNone();
    }

    PGOProfileType GetProfileType() const
    {
        return type_;
    }

    void SetCtorPt(PGOProfileType type)
    {
        ctorPt_ = type;
    }

    PGOProfileType GetCtorPt() const
    {
        return ctorPt_;
    }

    void SetProtoTypePt(PGOProfileType type)
    {
        protoPt_ = type;
    }

    PGOProfileType GetProtoTypePt() const
    {
        return protoPt_;
    }

    void SetElementsKind(ElementsKind kind)
    {
        kind_ = kind;
    }

    ElementsKind GetElementsKind() const
    {
        return kind_;
    }

    void SetElementsLength(uint32_t length)
    {
        elementsLength_ = length;
    }

    uint32_t GetElementsLength() const
    {
        return elementsLength_;
    }

    void SetSpaceFlag(RegionSpaceFlag flag)
    {
        spaceFlag_ = flag;
    }

    RegionSpaceFlag GetSpaceFlag() const
    {
        return spaceFlag_;
    }

    bool operator<(const PGODefineOpTemplate &right) const
    {
        return this->GetProfileType() < right.GetProfileType();
    }

private:
    PGOProfileType type_ { PGOProfileType() };
    PGOProfileType ctorPt_ { PGOProfileType() };
    PGOProfileType protoPt_ { PGOProfileType() };
    uint32_t elementsLength_ { 0 };
    ElementsKind kind_ { ElementsKind::NONE };
    RegionSpaceFlag spaceFlag_  { RegionSpaceFlag::UNINITIALIZED };
};

using PGODefineOpType = PGODefineOpTemplate<ProfileType>;
using PGODefineOpTypeRef = PGODefineOpTemplate<ProfileTypeRef>;

class PGOTypeRef {
public:
    PGOTypeRef() : type_(nullptr) {}

    explicit PGOTypeRef(PGOType *type) : type_(type) {}

    explicit PGOTypeRef(const PGOSampleType *type) : type_(static_cast<const PGOType*>(type)) {}

    explicit PGOTypeRef(const PGORWOpType *type) : type_(static_cast<const PGOType*>(type)) {}

    explicit PGOTypeRef(const PGODefineOpType *type) : type_(static_cast<const PGOType*>(type)) {}

    static PGOTypeRef NoneType()
    {
        return PGOTypeRef();
    }

    bool operator<(const PGOTypeRef &right) const
    {
        return type_ < right.type_;
    }

    bool operator!=(const PGOTypeRef &right) const
    {
        return type_ != right.type_;
    }

    bool operator==(const PGOTypeRef &right) const
    {
        return type_ == right.type_;
    }

    bool IsValid() const
    {
        return type_ != nullptr;
    }

    bool IsValidCallMethodId() const
    {
        if (type_ == nullptr) {
            return false;
        }
        if (!type_->IsScalarOpType()) {
            return false;
        }
        auto sampleType = static_cast<const PGOSampleType*>(type_);
        if (sampleType->IsProfileType()) {
            if (sampleType->GetProfileType().IsMethodId()) {
                return sampleType->GetProfileType().IsValidCallMethodId();
            }
            if (sampleType->GetProfileType().IsClassType()) {
                return sampleType->GetProfileType().IsValidClassConstructorMethodId();
            }
        }
        return false;
    }

    uint32_t GetCallMethodId() const
    {
        auto sampleType = static_cast<const PGOSampleType*>(type_);
        if (sampleType->GetProfileType().IsClassType()) {
            return sampleType->GetProfileType().GetClassConstructorMethodId();
        }
        return sampleType->GetProfileType().GetCallMethodId();
    }

    uint64_t GetValue() const
    {
        auto sampleType = static_cast<const PGOSampleType*>(type_);
        return sampleType->GetProfileType().GetRaw();
    }

    const PGOSampleType* GetPGOSampleType() const
    {
        if (type_ == nullptr) {
            static PGOSampleType noneType = PGOSampleType::NoneType();
            return &noneType;
        }
        ASSERT(type_->IsScalarOpType());
        return static_cast<const PGOSampleType*>(type_);
    }

    bool IsPGOSampleType() const
    {
        if (type_ == nullptr) {
            return false;
        }
        return type_->IsScalarOpType();
    }

    bool HasNumber() const
    {
        return GetPGOSampleType()->HasNumber();
    }

    bool IsBoolean() const
    {
        return GetPGOSampleType()->IsBoolean();
    }

    bool IsString() const
    {
        return GetPGOSampleType()->IsString();
    }

    bool IsHeapObject() const
    {
        return GetPGOSampleType()->IsHeapObject();
    }

    bool IsNumberOrString() const
    {
        return GetPGOSampleType()->IsNumberOrString();
    }

    const PGORWOpType* GetPGORWOpType()
    {
        if (type_ == nullptr) {
            static PGORWOpType noneType;
            return &noneType;
        }
        ASSERT(type_->IsRwOpType());
        return static_cast<const PGORWOpType*>(type_);
    }

    const PGODefineOpType* GetPGODefineOpType()
    {
        if (type_ == nullptr) {
            static PGODefineOpType noneType;
            return &noneType;
        }
        ASSERT(type_->IsDefineOpType());
        return static_cast<const PGODefineOpType*>(type_);
    }

private:
    const PGOType *type_;
};
} // namespace panda::ecmascript::pgo
#endif  // ECMASCRIPT_PGO_PROFILER_TYPES_PGO_PROFILER_TYPE_H
