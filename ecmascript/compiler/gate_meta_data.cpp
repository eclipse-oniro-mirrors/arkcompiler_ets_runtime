/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "ecmascript/compiler/gate.h"
#include "ecmascript/compiler/gate_meta_data.h"

namespace panda::ecmascript::kungfu {
std::string MachineTypeToStr(MachineType machineType)
{
    switch (machineType) {
        case NOVALUE:
            return "NOVALUE";
        case ANYVALUE:
            return "ANYVALUE";
        case I1:
            return "I1";
        case I8:
            return "I8";
        case I16:
            return "I16";
        case I32:
            return "I32";
        case I64:
            return "I64";
        case F32:
            return "F32";
        case F64:
            return "F64";
        default:
            return "???";
    }
}

std::string GateMetaData::Str(OpCode opcode)
{
    const std::map<OpCode, const char *> strMap = {
#define GATE_NAME_MAP(NAME, OP, R, S, D, V)  { OpCode::OP, #OP },
    IMMUTABLE_META_DATA_CACHE_LIST(GATE_NAME_MAP)
    GATE_META_DATA_LIST_WITH_SIZE(GATE_NAME_MAP)
    GATE_META_DATA_LIST_WITH_ONE_VALUE(GATE_NAME_MAP)
#undef GATE_NAME_MAP
#define GATE_NAME_MAP(OP) { OpCode::OP, #OP },
        GATE_OPCODE_LIST(GATE_NAME_MAP)
#undef GATE_NAME_MAP
    };
    if (strMap.count(opcode) > 0) {
        return strMap.at(opcode);
    }
    return "OP-" + std::to_string(static_cast<uint8_t>(opcode));
}

bool GateMetaData::IsRoot() const
{
    return (opcode_ == OpCode::CIRCUIT_ROOT) || (opcode_ == OpCode::STATE_ENTRY) ||
        (opcode_ == OpCode::DEPEND_ENTRY) || (opcode_ == OpCode::RETURN_LIST) ||
        (opcode_ == OpCode::ARG_LIST);
}

bool GateMetaData::IsProlog() const
{
    return (opcode_ == OpCode::ARG);
}

bool GateMetaData::IsFixed() const
{
    return (opcode_ == OpCode::VALUE_SELECTOR) || (opcode_ == OpCode::DEPEND_SELECTOR)
        || (opcode_ == OpCode::DEPEND_RELAY);
}

bool GateMetaData::IsSchedulable() const
{
    return (opcode_ != OpCode::NOP) && (!IsProlog()) && (!IsRoot())
        && (!IsFixed()) && (GetStateCount() == 0);
}

bool GateMetaData::IsState() const
{
    return (opcode_ != OpCode::NOP) && (!IsProlog()) && (!IsRoot())
        && (!IsFixed()) && (GetStateCount() > 0);
}

bool GateMetaData::IsGeneralState() const
{
    return ((opcode_ == OpCode::IF_TRUE) || (opcode_ == OpCode::IF_FALSE)
        || (opcode_ == OpCode::JS_BYTECODE) || (opcode_ == OpCode::IF_SUCCESS)
        || (opcode_ == OpCode::IF_EXCEPTION) || (opcode_ == OpCode::SWITCH_CASE)
        || (opcode_ == OpCode::DEFAULT_CASE) || (opcode_ == OpCode::MERGE)
        || (opcode_ == OpCode::LOOP_BEGIN) || (opcode_ == OpCode::ORDINARY_BLOCK)
        || (opcode_ == OpCode::STATE_ENTRY) || (opcode_ == OpCode::TYPED_BINARY_OP)
        || (opcode_ == OpCode::TYPE_CONVERT) || (opcode_ == OpCode::TYPED_UNARY_OP)
        || (opcode_ == OpCode::TO_LENGTH) || (opcode_ == OpCode::HEAP_ALLOC)
        || (opcode_ == OpCode::LOAD_ELEMENT) || (opcode_ == OpCode::LOAD_PROPERTY)
        || (opcode_ == OpCode::STORE_ELEMENT) || (opcode_ == OpCode::STORE_PROPERTY)
        || (opcode_ == OpCode::TYPED_CALL));
}

bool GateMetaData::IsTerminalState() const
{
    return ((opcode_ == OpCode::RETURN) || (opcode_ == OpCode::THROW)
        || (opcode_ == OpCode::RETURN_VOID));
}

bool GateMetaData::IsCFGMerge() const
{
    return (opcode_ == OpCode::MERGE) || (opcode_ == OpCode::LOOP_BEGIN);
}

bool GateMetaData::IsControlCase() const
{
    return (opcode_ == OpCode::IF_BRANCH) || (opcode_ == OpCode::SWITCH_BRANCH)
        || (opcode_ == OpCode::IF_TRUE) || (opcode_ == OpCode::IF_FALSE)
        || (opcode_ == OpCode::IF_SUCCESS) || (opcode_ == OpCode::IF_EXCEPTION) ||
           (opcode_ == OpCode::SWITCH_CASE) || (opcode_ == OpCode::DEFAULT_CASE);
}

bool GateMetaData::IsLoopHead() const
{
    return (opcode_ == OpCode::LOOP_BEGIN);
}

bool GateMetaData::IsNop() const
{
    return (opcode_ == OpCode::NOP);
}

bool GateMetaData::IsConstant() const
{
    return (opcode_ == OpCode::CONSTANT || opcode_ == OpCode::CONST_DATA);
}

bool GateMetaData::IsTypedOperator() const
{
    return (opcode_ == OpCode::TYPED_BINARY_OP) || (opcode_ == OpCode::TYPE_CONVERT)
        || (opcode_ == OpCode::TYPED_UNARY_OP);
}

#define CACHED_VALUE_LIST(V) \
    V(1)                     \
    V(2)                     \
    V(3)                     \
    V(4)                     \
    V(5)                     \

struct GateMetaDataChache {
static constexpr size_t ONE_VALUE = 1;
static constexpr size_t TWO_VALUE = 2;
static constexpr size_t THREE_VALUE = 3;
static constexpr size_t FOUR_VALUE = 4;
static constexpr size_t FIVE_VALUE = 5;

#define DECLARE_CACHED_GATE_META(NAME, OP, R, S, D, V)      \
    GateMetaData cached##NAME##_ { OpCode::OP, R, S, D, V };
    IMMUTABLE_META_DATA_CACHE_LIST(DECLARE_CACHED_GATE_META)
#undef DECLARE_CACHED_GATE_META

#define DECLARE_CACHED_VALUE_META(VALUE)                                           \
GateMetaData cachedMerge##VALUE##_ { OpCode::MERGE, false, VALUE, 0, 0 };          \
GateMetaData cachedDependSelector##VALUE##_ { OpCode::DEPEND_SELECTOR, false, 1, VALUE, 0 };
CACHED_VALUE_LIST(DECLARE_CACHED_VALUE_META)
#undef DECLARE_CACHED_VALUE_META

#define DECLARE_CACHED_GATE_META(NAME, OP, R, S, D, V)                 \
    GateMetaData cached##NAME##1_{ OpCode::OP, R, S, D, ONE_VALUE };   \
    GateMetaData cached##NAME##2_{ OpCode::OP, R, S, D, TWO_VALUE };   \
    GateMetaData cached##NAME##3_{ OpCode::OP, R, S, D, THREE_VALUE }; \
    GateMetaData cached##NAME##4_{ OpCode::OP, R, S, D, FOUR_VALUE };  \
    GateMetaData cached##NAME##5_{ OpCode::OP, R, S, D, FIVE_VALUE };
GATE_META_DATA_LIST_WITH_VALUE_IN(DECLARE_CACHED_GATE_META)
#undef DECLARE_CACHED_GATE_META

#define DECLARE_CACHED_GATE_META(NAME, OP, R, S, D, V)                        \
    OneValueMetaData cached##NAME##1_{ OpCode::OP, R, S, D, V, ONE_VALUE };   \
    OneValueMetaData cached##NAME##2_{ OpCode::OP, R, S, D, V, TWO_VALUE };   \
    OneValueMetaData cached##NAME##3_{ OpCode::OP, R, S, D, V, THREE_VALUE }; \
    OneValueMetaData cached##NAME##4_{ OpCode::OP, R, S, D, V, FOUR_VALUE };  \
    OneValueMetaData cached##NAME##5_{ OpCode::OP, R, S, D, V, FIVE_VALUE };
GATE_META_DATA_LIST_WITH_ONE_VALUE(DECLARE_CACHED_GATE_META)
#undef DECLARE_CACHED_GATE_META
};

namespace {
GateMetaDataChache globalGateMetaDataChache;
}

GateMetaBuilder::GateMetaBuilder(Chunk* chunk) :
    cache_(globalGateMetaDataChache), chunk_(chunk) {}

#define DECLARE_GATE_META(NAME, OP, R, S, D, V) \
const GateMetaData* GateMetaBuilder::NAME()     \
{                                               \
    return &cache_.cached##NAME##_;             \
}
IMMUTABLE_META_DATA_CACHE_LIST(DECLARE_GATE_META)
#undef DECLARE_GATE_META

#define DECLARE_GATE_META(NAME, OP, R, S, D, V)                    \
const GateMetaData* GateMetaBuilder::NAME(size_t value)            \
{                                                                  \
    switch (value) {                                               \
        case GateMetaDataChache::ONE_VALUE:                        \
            return &cache_.cached##NAME##1_;                       \
        case GateMetaDataChache::TWO_VALUE:                        \
            return &cache_.cached##NAME##2_;                       \
        case GateMetaDataChache::THREE_VALUE:                      \
            return &cache_.cached##NAME##3_;                       \
        case GateMetaDataChache::FOUR_VALUE:                       \
            return &cache_.cached##NAME##4_;                       \
        case GateMetaDataChache::FIVE_VALUE:                       \
            return &cache_.cached##NAME##5_;                       \
        default:                                                   \
            break;                                                 \
    }                                                              \
    auto meta = new (chunk_) GateMetaData(OpCode::OP, R, S, D, V); \
    meta->SetType(GateMetaData::MUTABLE_WITH_SIZE);                \
    return meta;                                                   \
}
GATE_META_DATA_LIST_WITH_SIZE(DECLARE_GATE_META)
#undef DECLARE_GATE_META

#define DECLARE_GATE_META(NAME, OP, R, S, D, V)                               \
const GateMetaData* GateMetaBuilder::NAME(uint64_t value)                     \
{                                                                             \
    switch (value) {                                                          \
        case GateMetaDataChache::ONE_VALUE:                                   \
            return &cache_.cached##NAME##1_;                                  \
        case GateMetaDataChache::TWO_VALUE:                                   \
            return &cache_.cached##NAME##2_;                                  \
        case GateMetaDataChache::THREE_VALUE:                                 \
            return &cache_.cached##NAME##3_;                                  \
        case GateMetaDataChache::FOUR_VALUE:                                  \
            return &cache_.cached##NAME##4_;                                  \
        case GateMetaDataChache::FIVE_VALUE:                                  \
            return &cache_.cached##NAME##5_;                                  \
        default:                                                              \
            break;                                                            \
    }                                                                         \
    auto meta = new (chunk_) OneValueMetaData(OpCode::OP, R, S, D, V, value); \
    meta->SetType(GateMetaData::MUTABLE_ONE_VALUE);                           \
    return meta;                                                              \
}
GATE_META_DATA_LIST_WITH_ONE_VALUE(DECLARE_GATE_META)
#undef DECLARE_GATE_META

}  // namespace panda::ecmascript::kungfu
