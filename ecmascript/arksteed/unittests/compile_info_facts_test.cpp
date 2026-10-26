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

// Unit domain closing matrix row G2.5 (contract C4 of
// `arksteed-cpp-unit-test-domains`): local invariants of `NodeInfo` /
// `CompileInfoFacts` — the `NodeType` lattice, HClass recording and narrowing,
// merge/compatibility of node facts, CSE of expressions and cached
// environment slots.
//
// Fixture A (H4): allocator-backed `Chunk`, a `Graph` over that chunk and real
// vertices built through `Vertex::New`/`Graph::Get*Constant`; no VM.
// Fixture B (H4): the VM-light fixture of `panda::test::TestHelper` used only
// where two real `JSHClass` objects are required. A failed VM creation makes
// the case red (`ASSERT_TRUE(scope.IsValid())`), never skipped.
//
// Expectations come from independent sources (D2): the lattice algebra over
// the 12 declared atomic members, the subset definition of `NodeTypeIs`, the
// tagged-value layout of `JSType`/`JSTaggedValue`, the conjunction/union rules
// of `MergeWith`, and the documented CSE/env-slot contracts. No expected value
// is copied from the production expression under test.

#include "ecmascript/arksteed/arksteed_compile_info_facts.h"

#include "ecmascript/arksteed/arksteed_bb.h"
#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_loop_effects.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/ecma_string.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_object.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/tests/test_helper.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <sstream>
#include <string>

namespace panda::ecmascript::arksteed {

namespace {

using NodeType = NodeInfo::NodeType;

// Bit view of the lattice element, so that the laws can be checked against a
// second, independent representation (plain bit sets) instead of only against
// the production helpers.
uint32_t Bits(NodeType type)
{
    return NodeInfo::ToNodeTypeBits(type);
}

uint32_t BitCount(uint16_t value)
{
    uint32_t count = 0;
    while (value != 0) {
        value &= static_cast<uint16_t>(value - 1);
        ++count;
    }
    return count;
}

// Collects law violations without paying the cost of a GTest assertion in a
// loop that makes ~10^8 checks; the failure text of the first violation is
// kept for the final assertion.
struct LawReport {
    size_t checked {0};
    size_t failed {0};
    std::string firstFailure;

    void Check(bool ok, const char *law, uint16_t a = 0, uint16_t b = 0, uint16_t c = 0)
    {
        ++checked;
        if (ok) {
            return;
        }
        ++failed;
        if (firstFailure.empty()) {
            std::ostringstream out;
            out << law << " violated: a=0x" << std::hex << a << " b=0x" << b << " c=0x" << c;
            firstFailure = out.str();
        }
    }
};

// Independent oracle of the positive lattice/HClass assertions: the pair
// (union, intersection) of two members and, for the HClass part, the narrowed
// known class and type. The mutation control feeds it a mutated expectation.
bool LatticePairIsExpected(NodeType lhs, NodeType rhs, uint32_t expectedUnion, uint32_t expectedIntersection)
{
    return Bits(NodeInfo::UnionNodeType(lhs, rhs)) == expectedUnion &&
           Bits(NodeInfo::IntersectNodeType(lhs, rhs)) == expectedIntersection;
}

bool NarrowedHClassIsExpected(const NodeInfo &info, JSHClass *expectedHClass, NodeType expectedType)
{
    return info.HasFreshKnownHClass() && info.GetFreshKnownHClass() == expectedHClass &&
           Bits(info.GetType()) == Bits(expectedType);
}

struct GraphFixture {
    NativeAreaAllocator native;
    Chunk chunk {&native};
    Graph graph {&chunk};

    ValueVertex *Int32(int32_t value)
    {
        return graph.GetInt32Constant(value);
    }

    ValueVertex *Tagged(JSTaggedValue value)
    {
        return graph.GetTaggedConstant(static_cast<uint64_t>(value.GetRawData()));
    }

    ValueVertex *NewPhi()
    {
        return Vertex::New<PhiVertex>(&chunk, 1, VirtualRegister(0));
    }

    ValueVertex *NewInitialValue()
    {
        return Vertex::New<InitialValueVertex>(&chunk, 0, 0);
    }

    ValueVertex *NewAdd(ValueVertex *lhs, ValueVertex *rhs)
    {
        auto *vertex = Vertex::New<I32AddVertex>(&chunk, 2);
        vertex->SetInput(0, lhs);
        vertex->SetInput(1, rhs);
        return vertex;
    }

    CompileInfoFacts::ExpressionInputs Inputs(std::initializer_list<ValueVertex *> values)
    {
        CompileInfoFacts::ExpressionInputs inputs(&chunk);
        for (ValueVertex *value : values) {
            inputs.push_back(value);
        }
        return inputs;
    }

    CompileInfoFacts::ExpressionOptions Options(uint64_t option)
    {
        CompileInfoFacts::ExpressionOptions options(&chunk);
        options.push_back(option);
        return options;
    }
};

constexpr uint32_t kCseHash = 0x1234ABCDU;
constexpr uint32_t kPureCseHash = 0x0F0F0F0FU;

}  // namespace

// VM-light fixture B (H4) over `panda::test::TestHelper`
// (`ecmascript/tests/test_helper.h:107-137`). The scope is created and
// destroyed with the case; an invalid scope fails the case assertion instead of
// silently skipping it (H5), and the destructor releases the VM even when an
// assertion returns early.
class ArkSteedCompileInfoFactsTest : public testing::Test {
public:
    class ScopedEcmaVm {
    public:
        ScopedEcmaVm()
        {
            panda::test::TestHelper::CreateEcmaVMWithScope(vm_, thread_, scope_);
        }

        ~ScopedEcmaVm()
        {
            if (vm_ != nullptr) {
                panda::test::TestHelper::DestroyEcmaVMWithScope(vm_, scope_);
            }
        }

        NO_COPY_SEMANTIC(ScopedEcmaVm);
        NO_MOVE_SEMANTIC(ScopedEcmaVm);

        bool IsValid() const
        {
            return vm_ != nullptr && thread_ != nullptr;
        }

        EcmaVM *GetVm() const
        {
            return vm_;
        }

        JSThread *GetThread() const
        {
            return thread_;
        }

    private:
        EcmaVM *vm_ {nullptr};
        EcmaHandleScope *scope_ {nullptr};
        JSThread *thread_ {nullptr};
    };
};

// The 12 atomic (single-bit) members declared by `NodeInfo::NodeType`. Every
// other declared member is the union of some of them, so their union is the
// value universe of the lattice.
static const std::array<NodeType, 12> g_atomicTypes = {
    NodeType::INT, NodeType::DOUBLE, NodeType::NULL_TYPE, NodeType::UNDEFINED, NodeType::BOOLEAN, NodeType::STRING,
    NodeType::SYMBOL, NodeType::BIGINT, NodeType::JS_ARRAY, NodeType::JS_FUNCTION, NodeType::JS_TYPED_ARRAY,
    NodeType::OTHER_JS_RECEIVER,
};

// Every value declared by the `NodeInfo::NodeType` enumeration.
static const std::array<NodeType, 19> g_enumerationValues = {
    NodeType::NONE, NodeType::INT, NodeType::DOUBLE, NodeType::NULL_TYPE, NodeType::UNDEFINED, NodeType::BOOLEAN,
    NodeType::STRING, NodeType::SYMBOL, NodeType::BIGINT, NodeType::JS_ARRAY, NodeType::JS_FUNCTION,
    NodeType::JS_TYPED_ARRAY, NodeType::OTHER_JS_RECEIVER, NodeType::NUMBER, NodeType::NULL_OR_UNDEFINED,
    NodeType::ODDBALL, NodeType::JS_RECEIVER, NodeType::ANY_HEAP_OBJECT, NodeType::UNKNOWN,
};

// --- NodeType lattice ------------------------------------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, NodeTypeLatticeLawsHoldForAllValuePairs)
{
    uint32_t universe = 0;
    for (NodeType atom : g_atomicTypes) {
        ASSERT_EQ(BitCount(static_cast<uint16_t>(Bits(atom))), 1U) << "atomic member must occupy exactly one bit";
        ASSERT_EQ(Bits(atom) & universe, 0U) << "atomic members must use distinct bits";
        universe |= Bits(atom);
    }
    // The atoms occupy the low 12 bits, so the value domain is exactly [0, 0x0FFF].
    ASSERT_EQ(universe, 0x0FFFU);
    ASSERT_EQ(Bits(NodeType::UNKNOWN), universe);
    ASSERT_EQ(Bits(NodeType::NONE), 0U);
    ASSERT_TRUE(NodeInfo::IsEmptyNodeType(NodeType::NONE));

    // Named composite members, recomputed independently from the atoms.
    ASSERT_EQ(Bits(NodeType::NUMBER), Bits(NodeType::INT) | Bits(NodeType::DOUBLE));
    ASSERT_EQ(Bits(NodeType::NULL_OR_UNDEFINED), Bits(NodeType::NULL_TYPE) | Bits(NodeType::UNDEFINED));
    ASSERT_EQ(Bits(NodeType::ODDBALL),
              Bits(NodeType::NULL_TYPE) | Bits(NodeType::UNDEFINED) | Bits(NodeType::BOOLEAN));
    ASSERT_EQ(Bits(NodeType::JS_RECEIVER),
              Bits(NodeType::JS_ARRAY) | Bits(NodeType::JS_FUNCTION) | Bits(NodeType::JS_TYPED_ARRAY) |
                  Bits(NodeType::OTHER_JS_RECEIVER));
    ASSERT_EQ(Bits(NodeType::ANY_HEAP_OBJECT),
              Bits(NodeType::STRING) | Bits(NodeType::SYMBOL) | Bits(NodeType::BIGINT) | Bits(NodeType::JS_RECEIVER));

    LawReport report;
    const auto element = [](uint32_t bits) { return NodeInfo::ToNodeType(static_cast<uint16_t>(bits)); };

    // Unary laws over the whole value domain.
    for (uint32_t a = 0; a <= universe; ++a) {
        const NodeType ta = element(a);
        report.Check(Bits(ta) == a, "ToNodeType/ToNodeTypeBits round trip", static_cast<uint16_t>(a));
        report.Check(NodeInfo::IsEmptyNodeType(ta) == (a == 0), "IsEmptyNodeType == (bits == 0)",
                     static_cast<uint16_t>(a));
        report.Check(NodeInfo::NodeTypeIs(ta, ta), "reflexive subset (NodeTypeIs(t, t))", static_cast<uint16_t>(a));
        report.Check(Bits(NodeInfo::UnionNodeType(ta, NodeType::NONE)) == a, "Union(t, NONE) == t",
                     static_cast<uint16_t>(a));
        report.Check(Bits(NodeInfo::IntersectNodeType(ta, NodeType::NONE)) == 0, "Intersect(t, NONE) == NONE",
                     static_cast<uint16_t>(a));
        report.Check(Bits(NodeInfo::IntersectNodeType(ta, NodeType::UNKNOWN)) == a,
                     "Intersect(t, UNKNOWN) == t", static_cast<uint16_t>(a));
        report.Check(Bits(NodeInfo::UnionNodeType(ta, NodeType::UNKNOWN)) == universe,
                     "Union(t, UNKNOWN) == UNKNOWN", static_cast<uint16_t>(a));
        report.Check(Bits(NodeInfo::RemoveNodeType(NodeType::UNKNOWN, ta)) == (universe & ~a),
                     "RemoveNodeType(UNKNOWN, t) == complement(t)", static_cast<uint16_t>(a));
        report.Check(Bits(NodeInfo::RemoveNodeType(ta, ta)) == 0, "RemoveNodeType(t, t) == NONE",
                     static_cast<uint16_t>(a));
        report.Check(Bits(NodeInfo::RemoveNodeType(ta, NodeType::NONE)) == a, "RemoveNodeType(t, NONE) == t",
                     static_cast<uint16_t>(a));
    }

    // Binary laws over every pair of lattice values.
    for (uint32_t a = 0; a <= universe; ++a) {
        for (uint32_t b = 0; b <= universe; ++b) {
            const NodeType ta = element(a);
            const NodeType tb = element(b);
            report.Check(Bits(NodeInfo::UnionNodeType(ta, tb)) == (a | b), "Union == bitwise or",
                         static_cast<uint16_t>(a), static_cast<uint16_t>(b));
            report.Check(Bits(NodeInfo::IntersectNodeType(ta, tb)) == (a & b), "Intersect == bitwise and",
                         static_cast<uint16_t>(a), static_cast<uint16_t>(b));
            report.Check(NodeInfo::UnionNodeType(ta, tb) == NodeInfo::UnionNodeType(tb, ta),
                         "Union commutative", static_cast<uint16_t>(a), static_cast<uint16_t>(b));
            report.Check(NodeInfo::IntersectNodeType(ta, tb) == NodeInfo::IntersectNodeType(tb, ta),
                         "Intersect commutative", static_cast<uint16_t>(a), static_cast<uint16_t>(b));
            report.Check(Bits(NodeInfo::IntersectNodeType(ta, NodeInfo::UnionNodeType(ta, tb))) == a,
                         "absorption: intersect(a, union(a, b)) == a", static_cast<uint16_t>(a),
                         static_cast<uint16_t>(b));
            report.Check(Bits(NodeInfo::UnionNodeType(ta, NodeInfo::IntersectNodeType(ta, tb))) == a,
                         "absorption: union(a, intersect(a, b)) == a", static_cast<uint16_t>(a),
                         static_cast<uint16_t>(b));
            report.Check(NodeInfo::NodeTypeIs(ta, tb) == ((a & ~b) == 0),
                         "subset definition of NodeTypeIs", static_cast<uint16_t>(a), static_cast<uint16_t>(b));
            report.Check(NodeInfo::NodeTypeCanBe(ta, tb) == ((a & b) != 0),
                         "NodeTypeCanBe == non-empty intersection", static_cast<uint16_t>(a),
                         static_cast<uint16_t>(b));
        }
    }

    // Ternary laws over every triple of declared enumeration values.
    for (NodeType ta : g_enumerationValues) {
        for (NodeType tb : g_enumerationValues) {
            for (NodeType tc : g_enumerationValues) {
                const uint32_t a = Bits(ta);
                const uint32_t b = Bits(tb);
                const uint32_t c = Bits(tc);
                report.Check(NodeInfo::UnionNodeType(NodeInfo::UnionNodeType(ta, tb), tc) ==
                                 NodeInfo::UnionNodeType(ta, NodeInfo::UnionNodeType(tb, tc)),
                             "Union associative", static_cast<uint16_t>(a), static_cast<uint16_t>(b),
                             static_cast<uint16_t>(c));
                report.Check(NodeInfo::IntersectNodeType(NodeInfo::IntersectNodeType(ta, tb), tc) ==
                                 NodeInfo::IntersectNodeType(ta, NodeInfo::IntersectNodeType(tb, tc)),
                             "Intersect associative", static_cast<uint16_t>(a), static_cast<uint16_t>(b),
                             static_cast<uint16_t>(c));
                report.Check(NodeInfo::UnionNodeType(ta, NodeInfo::IntersectNodeType(tb, tc)) ==
                                 NodeInfo::IntersectNodeType(NodeInfo::UnionNodeType(ta, tb),
                                                             NodeInfo::UnionNodeType(ta, tc)),
                             "distributivity: union over intersect", static_cast<uint16_t>(a),
                             static_cast<uint16_t>(b), static_cast<uint16_t>(c));
                report.Check(NodeInfo::IntersectNodeType(ta, NodeInfo::UnionNodeType(tb, tc)) ==
                                 NodeInfo::UnionNodeType(NodeInfo::IntersectNodeType(ta, tb),
                                                         NodeInfo::IntersectNodeType(ta, tc)),
                             "distributivity: intersect over union", static_cast<uint16_t>(a),
                             static_cast<uint16_t>(b), static_cast<uint16_t>(c));
                report.Check(Bits(NodeInfo::RemoveNodeType(NodeType::UNKNOWN, NodeInfo::UnionNodeType(ta, tb))) ==
                                 (universe & ~(a | b)),
                             "complement of a union (De Morgan)", static_cast<uint16_t>(a),
                             static_cast<uint16_t>(b), static_cast<uint16_t>(c));
                report.Check(Bits(NodeInfo::RemoveNodeType(NodeType::UNKNOWN, NodeInfo::IntersectNodeType(ta, tb))) ==
                                 (universe & ~(a & b)),
                             "complement of an intersection (De Morgan)", static_cast<uint16_t>(a),
                             static_cast<uint16_t>(b), static_cast<uint16_t>(c));
            }
        }
    }

    // Exact number of law checks the enumeration above must make: 10 laws for
    // each of the 4096 values, 8 laws for each of the 4096 x 4096 pairs and
    // 6 laws for each of the 19^3 triples of declared enumeration values. A
    // shortened loop or a dropped law makes this count mismatch, so the
    // exhaustiveness of the enumeration is itself asserted.
    constexpr size_t kExpectedChecks = 4096U * 10U + 4096U * 4096U * 8U + 19U * 19U * 19U * 6U;
    EXPECT_EQ(report.checked, kExpectedChecks)
        << "the enumeration must cover every value, pair and triple of the lattice";
    EXPECT_EQ(report.failed, 0U) << report.firstFailure;
}

// --- NodeType conversion ---------------------------------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, NodeTypeFromJSTaggedValueMapsPrimitiveAndHeapValues)
{
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue(42)), NodeType::INT);
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue(-1)), NodeType::INT);
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue(1.5)), NodeType::DOUBLE);
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue(-0.5)), NodeType::DOUBLE);
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue::True()), NodeType::BOOLEAN);
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue::False()), NodeType::BOOLEAN);
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue::Undefined()), NodeType::UNDEFINED);
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue::Null()), NodeType::NULL_TYPE);
    // The hole is the tagged special value 0x05: it carries neither the int tag
    // (the high 16-bit marker) nor the object tag, and its special bits are set,
    // so it is not a heap object either. The lattice has no hole member, hence
    // the mapping has to report UNKNOWN (`js_tagged_value_internals.h`).
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue::Hole()), NodeType::UNKNOWN);
    EXPECT_EQ(NodeTypeFromHClass(nullptr), NodeType::UNKNOWN);

    ArkSteedCompileInfoFactsTest::ScopedEcmaVm scope;
    ASSERT_TRUE(scope.IsValid()) << "cannot create EcmaVM";

    ObjectFactory *factory = scope.GetVm()->GetFactory();
    JSHandle<JSHClass> plainClass = factory->NewEcmaHClass(static_cast<uint32_t>(JSObject::SIZE), JSType::JS_OBJECT);
    JSHandle<JSObject> plainObject = factory->NewJSObject(plainClass);
    JSHandle<JSArray> array = factory->NewJSArray();
    JSHandle<EcmaString> text = factory->NewFromASCII("arksteed");
    ASSERT_NE(*plainClass, nullptr);
    ASSERT_NE(*plainObject, nullptr);
    ASSERT_NE(*array, nullptr);
    ASSERT_NE(*text, nullptr);

    // Derived from the documented JSType grouping: a plain JS object is an
    // "other JS receiver", a JSArray is its own member, a string is STRING.
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue(static_cast<const TaggedObject *>(*plainObject))),
              NodeType::OTHER_JS_RECEIVER);
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue(static_cast<const TaggedObject *>(*array))), NodeType::JS_ARRAY);
    EXPECT_EQ(NodeTypeFromJSTaggedValue(JSTaggedValue(static_cast<const TaggedObject *>(*text))), NodeType::STRING);
    EXPECT_EQ(NodeTypeFromHClass((*plainObject)->GetClass()), NodeType::OTHER_JS_RECEIVER);
    EXPECT_EQ(NodeTypeFromHClass((*array)->GetClass()), NodeType::JS_ARRAY);
}

// --- NodeInfo: HClasses, narrowing, merge ----------------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, NodeInfoHClassFactsOnRealHClasses)
{
    ArkSteedCompileInfoFactsTest::ScopedEcmaVm scope;
    ASSERT_TRUE(scope.IsValid()) << "cannot create EcmaVM";
    ASSERT_NE(scope.GetThread(), nullptr);

    ObjectFactory *factory = scope.GetVm()->GetFactory();
    JSHandle<JSHClass> plainClass = factory->NewEcmaHClass(static_cast<uint32_t>(JSObject::SIZE), JSType::JS_OBJECT);
    JSHandle<JSObject> plainObject = factory->NewJSObject(plainClass);
    JSHandle<JSArray> array = factory->NewJSArray();
    ASSERT_NE(*plainObject, nullptr);
    ASSERT_NE(*array, nullptr);

    // Both classes are real, non-movable heap objects; their object types are
    // independent of the recorded facts.
    JSHClass *plainHClass = (*plainObject)->GetClass();
    JSHClass *arrayHClass = (*array)->GetClass();
    ASSERT_NE(plainHClass, nullptr);
    ASSERT_NE(arrayHClass, nullptr);
    ASSERT_NE(plainHClass, arrayHClass);
    ASSERT_EQ(NodeTypeFromHClass(plainHClass), NodeType::OTHER_JS_RECEIVER);
    ASSERT_EQ(NodeTypeFromHClass(arrayHClass), NodeType::JS_ARRAY);

    NodeInfo info;
    ASSERT_TRUE(info.NoInfoAvailable());
    info.RecordPossibleHClasses({plainHClass, arrayHClass}, true);
    ASSERT_EQ(info.PossibleHClassCount(), 2U);
    ASSERT_TRUE(info.HasFreshPossibleHClasses());
    ASSERT_FALSE(info.HasFreshKnownHClass());
    ASSERT_FALSE(info.PossibleHClassesAreStale());
    ASSERT_FALSE(info.HasUnstablePossibleHClass());
    // The recorded receiver classes narrow the unknown type to the union of
    // their atom types (ANY_HEAP_OBJECT is not part of that union).
    ASSERT_EQ(Bits(info.GetType()), Bits(NodeType::OTHER_JS_RECEIVER) | Bits(NodeType::JS_ARRAY));

    // Narrowing to one class keeps exactly one fresh known class.
    ASSERT_TRUE(info.NarrowPossibleHClasses({plainHClass}));
    ASSERT_EQ(info.PossibleHClassCount(), 1U);
    ASSERT_TRUE(info.HasFreshKnownHClass());
    ASSERT_EQ(info.GetFreshKnownHClass(), plainHClass);
    ASSERT_EQ(Bits(info.GetType()), Bits(NodeType::OTHER_JS_RECEIVER));
    ASSERT_TRUE(NarrowedHClassIsExpected(info, plainHClass, NodeType::OTHER_JS_RECEIVER));
    ASSERT_TRUE(info.NarrowPossibleHClasses({plainHClass})) << "narrowing to the same set is stable";
    // A disjoint set empties the possible classes and reports failure.
    ASSERT_FALSE(info.NarrowPossibleHClasses({arrayHClass}));
    ASSERT_EQ(info.PossibleHClassCount(), 0U);
    ASSERT_FALSE(info.HasFreshKnownHClass());
    ASSERT_FALSE(info.HasFreshPossibleHClasses());

    // An unstable single class is a fresh known class but not a stable one.
    NodeInfo unstable;
    unstable.RecordPossibleHClasses({plainHClass}, false);
    ASSERT_FALSE(unstable.HClassIsStable());
    ASSERT_TRUE(unstable.HasUnstablePossibleHClass());
    ASSERT_TRUE(unstable.HasFreshKnownHClass());
    unstable.MarkUnstableHClassesStale();
    ASSERT_TRUE(unstable.PossibleHClassesAreStale());
    ASSERT_FALSE(unstable.HasFreshKnownHClass());

    // A stable class is never invalidated by MarkUnstableHClassesStale.
    NodeInfo stable;
    stable.RecordPossibleHClasses({plainHClass}, true);
    stable.MarkUnstableHClassesStale();
    ASSERT_FALSE(stable.PossibleHClassesAreStale());
    ASSERT_TRUE(stable.HasFreshKnownHClass());

    // Merging two different single-class facts unions the receiver sets.
    NodeInfo left;
    left.RecordPossibleHClasses({plainHClass}, true);
    NodeInfo right;
    right.RecordPossibleHClasses({arrayHClass}, true);
    ASSERT_TRUE(left.MergeWith(right));
    ASSERT_EQ(left.PossibleHClassCount(), 2U);
    ASSERT_TRUE(left.ContainsPossibleHClass(plainHClass));
    ASSERT_TRUE(left.ContainsPossibleHClass(arrayHClass));
    ASSERT_EQ(Bits(left.GetType()), Bits(NodeType::OTHER_JS_RECEIVER) | Bits(NodeType::JS_ARRAY));
    // A side without any possible class drops the merged set entirely.
    NodeInfo withoutHClasses;
    left.MergeWith(withoutHClasses);
    ASSERT_EQ(left.PossibleHClassCount(), 0U);
    ASSERT_FALSE(left.HasFreshPossibleHClasses());

    // IsCompatibleWith: an expected set with unstable entries requires the
    // actual set to be at least as stable and to be a subset of it.
    NodeInfo expectedUnstable;
    expectedUnstable.RecordPossibleHClasses({plainHClass}, false);
    NodeInfo actualUnstable;
    actualUnstable.RecordPossibleHClasses({plainHClass}, false);
    ASSERT_TRUE(actualUnstable.IsCompatibleWith(expectedUnstable)) << "unstable expected entries are compatible";
    NodeInfo actualOtherClass;
    actualOtherClass.RecordPossibleHClasses({arrayHClass}, false);
    ASSERT_FALSE(actualOtherClass.IsCompatibleWith(expectedUnstable)) << "class outside the expected set";

    // In a mixed expected set a stable expected class cannot be covered by an
    // unstable actual class, while a stable actual class is always accepted.
    NodeInfo expectedMixed;
    expectedMixed.RecordPossibleHClasses(
        NodeInfo::PossibleHClassInfos {NodeInfo::PossibleHClassInfo {plainHClass, true},
                                       NodeInfo::PossibleHClassInfo {arrayHClass, false}});
    ASSERT_TRUE(expectedMixed.HasUnstablePossibleHClass());
    NodeInfo actualStable;
    actualStable.RecordPossibleHClasses({plainHClass}, true);
    ASSERT_TRUE(actualStable.IsCompatibleWith(expectedMixed));
    NodeInfo actualNotAsStable;
    actualNotAsStable.RecordPossibleHClasses({plainHClass}, false);
    ASSERT_FALSE(actualNotAsStable.IsCompatibleWith(expectedMixed));

    // A stale expected set is accepted without comparing classes.
    NodeInfo expectedStale;
    expectedStale.RecordPossibleHClasses({plainHClass}, false);
    expectedStale.MarkUnstableHClassesStale();
    ASSERT_TRUE(expectedStale.PossibleHClassesAreStale());
    NodeInfo actualTyped;
    actualTyped.SetType(NodeType::OTHER_JS_RECEIVER);
    ASSERT_TRUE(actualTyped.IsCompatibleWith(expectedStale));
}

// --- NodeInfo: merge and compatibility of plain facts ----------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, NodeInfoMergeWithAndCompatibility)
{
    GraphFixture fixture;
    ValueVertex *alternative = fixture.Int32(1);
    ValueVertex *otherAlternative = fixture.Int32(2);

    NodeInfo lhs;
    lhs.SetType(NodeType::INT);
    lhs.MarkNonHole();
    lhs.GetAlternatives().Set(AlternativeNodes::Kind::INT32, alternative);
    NodeInfo rhs;
    rhs.SetType(NodeType::DOUBLE);
    rhs.GetAlternatives().Set(AlternativeNodes::Kind::INT32, alternative);

    ASSERT_TRUE(lhs.MergeWith(rhs));
    ASSERT_EQ(Bits(lhs.GetType()), Bits(NodeType::INT) | Bits(NodeType::DOUBLE));
    // nonHole is a conjunction of both sides; alternatives survive only when
    // both sides agree on the alternative node.
    ASSERT_FALSE(lhs.IsNonHole());
    ASSERT_EQ(lhs.GetAlternatives().Get(AlternativeNodes::Kind::INT32), alternative);
    NodeInfo conflicting;
    conflicting.SetType(NodeType::INT);
    conflicting.GetAlternatives().Set(AlternativeNodes::Kind::INT32, otherAlternative);
    lhs.MergeWith(conflicting);
    ASSERT_EQ(lhs.GetAlternatives().Get(AlternativeNodes::Kind::INT32), nullptr);

    NodeInfo bothNonHole;
    bothNonHole.SetType(NodeType::INT);
    bothNonHole.MarkNonHole();
    NodeInfo alsoNonHole;
    alsoNonHole.SetType(NodeType::DOUBLE);
    alsoNonHole.MarkNonHole();
    bothNonHole.MergeWith(alsoNonHole);
    ASSERT_TRUE(bothNonHole.IsNonHole());

    // A merge of two disjoint, narrower inputs must widen the type: keeping
    // either of the input types after the merge would be a false merge.
    NodeInfo mergedInt;
    mergedInt.SetType(NodeType::INT);
    NodeInfo mergedString;
    mergedString.SetType(NodeType::STRING);
    ASSERT_TRUE(mergedInt.MergeWith(mergedString));
    ASSERT_EQ(Bits(mergedInt.GetType()), Bits(NodeType::INT) | Bits(NodeType::STRING));
    ASSERT_FALSE(mergedInt.CheckType(NodeType::INT)) << "the merged facts must not keep INT";
    ASSERT_FALSE(mergedInt.CheckType(NodeType::STRING)) << "the merged facts must not keep STRING";
    ASSERT_TRUE(mergedInt.CheckType(NodeInfo::UnionNodeType(NodeType::INT, NodeType::STRING)));

    // Two facts that carry no information at all stay without information, so
    // the merge reports that nothing usable was produced.
    NodeInfo emptyLeft;
    NodeInfo emptyRight;
    ASSERT_TRUE(emptyLeft.NoInfoAvailable());
    ASSERT_FALSE(emptyLeft.MergeWith(emptyRight));
    NodeInfo withType;
    withType.SetType(NodeType::INT);
    ASSERT_FALSE(withType.NoInfoAvailable());
    NodeInfo withNonHole;
    withNonHole.MarkNonHole();
    ASSERT_FALSE(withNonHole.NoInfoAvailable());
    NodeInfo withAlternative;
    withAlternative.GetAlternatives().Set(AlternativeNodes::Kind::TAGGED, alternative);
    ASSERT_FALSE(withAlternative.NoInfoAvailable());

    // Compatibility is the subset relation on types plus the HClass rules; the
    // pairs below are hand-derived from that definition.
    NodeInfo actualInt;
    actualInt.SetType(NodeType::INT);
    NodeInfo expectedInt;
    expectedInt.SetType(NodeType::INT);
    NodeInfo expectedNumber;
    expectedNumber.SetType(NodeType::NUMBER);
    NodeInfo expectedString;
    expectedString.SetType(NodeType::STRING);
    NodeInfo expectedUnknown;
    expectedUnknown.SetType(NodeType::UNKNOWN);
    NodeInfo actualNumber;
    actualNumber.SetType(NodeType::NUMBER);
    NodeInfo actualUnknown;
    actualUnknown.SetType(NodeType::UNKNOWN);
    NodeInfo actualEmpty;
    actualEmpty.SetType(NodeType::NONE);
    EXPECT_TRUE(actualInt.IsCompatibleWith(expectedInt));
    EXPECT_TRUE(actualInt.IsCompatibleWith(expectedNumber)) << "INT is a subset of NUMBER";
    EXPECT_TRUE(actualInt.IsCompatibleWith(expectedUnknown)) << "every type is a subset of the full domain";
    EXPECT_FALSE(actualNumber.IsCompatibleWith(expectedInt)) << "NUMBER is not a subset of INT";
    EXPECT_FALSE(actualUnknown.IsCompatibleWith(expectedInt)) << "the full domain is not a subset of INT";
    EXPECT_FALSE(actualInt.IsCompatibleWith(expectedString)) << "disjoint members are incompatible";
    EXPECT_TRUE(actualEmpty.IsCompatibleWith(expectedInt)) << "the empty type is a subset of everything";
}

// --- Negative control (H6, D5) ---------------------------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, MutationControlForTypePairAndHClassSetIsDetected)
{
    // Type pair: the union of two distinct atoms is the set of their bits and
    // the intersection is empty. Mutating either expected value has to be
    // detected by the same oracle the positive case uses.
    const uint32_t expectedUnion = Bits(NodeType::INT) | Bits(NodeType::DOUBLE);
    ASSERT_TRUE(LatticePairIsExpected(NodeType::INT, NodeType::DOUBLE, expectedUnion, 0));
    EXPECT_FALSE(LatticePairIsExpected(NodeType::INT, NodeType::DOUBLE, expectedUnion | Bits(NodeType::STRING), 0))
        << "an extra bit in the expected union must be detected";
    EXPECT_FALSE(LatticePairIsExpected(NodeType::INT, NodeType::DOUBLE, expectedUnion, Bits(NodeType::INT)))
        << "a non-empty expected intersection must be detected";
    EXPECT_FALSE(LatticePairIsExpected(NodeType::INT, NodeType::DOUBLE, 0, expectedUnion))
        << "swapped expected union/intersection must be detected";

    // HClass set: after recording one fresh class the known class and the
    // narrowed type are exact. Substituting the other real class or the other
    // receiver type must be detected.
    ArkSteedCompileInfoFactsTest::ScopedEcmaVm scope;
    ASSERT_TRUE(scope.IsValid()) << "cannot create EcmaVM";

    ObjectFactory *factory = scope.GetVm()->GetFactory();
    JSHandle<JSHClass> plainClass = factory->NewEcmaHClass(static_cast<uint32_t>(JSObject::SIZE), JSType::JS_OBJECT);
    JSHandle<JSObject> plainObject = factory->NewJSObject(plainClass);
    JSHandle<JSArray> array = factory->NewJSArray();
    ASSERT_NE(*plainObject, nullptr);
    ASSERT_NE(*array, nullptr);
    JSHClass *plainHClass = (*plainObject)->GetClass();
    JSHClass *arrayHClass = (*array)->GetClass();
    ASSERT_NE(plainHClass, arrayHClass);

    NodeInfo info;
    info.RecordPossibleHClasses({plainHClass}, true);
    ASSERT_TRUE(NarrowedHClassIsExpected(info, plainHClass, NodeType::OTHER_JS_RECEIVER));
    EXPECT_FALSE(NarrowedHClassIsExpected(info, arrayHClass, NodeType::OTHER_JS_RECEIVER))
        << "a substituted expected HClass must be detected";
    EXPECT_FALSE(NarrowedHClassIsExpected(info, plainHClass, NodeType::JS_ARRAY))
        << "a substituted expected receiver type must be detected";
}

// --- CompileInfoFacts: CSE -------------------------------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, CompileInfoFactsCseLifecycle)
{
    GraphFixture fixture;
    ValueVertex *lhs = fixture.Int32(3);
    ValueVertex *rhs = fixture.Int32(4);
    ValueVertex *first = fixture.NewAdd(lhs, rhs);
    ValueVertex *second = fixture.NewAdd(lhs, rhs);
    ValueVertex *tagged = fixture.Tagged(JSTaggedValue(1));
    CompileInfoFacts::ExpressionInputs inputs = fixture.Inputs({lhs, rhs});
    CompileInfoFacts::ExpressionOptions options = fixture.Options(7);
    CompileInfoFacts::ExpressionInputs otherInputs = fixture.Inputs({lhs, first});
    CompileInfoFacts::ExpressionOptions otherOptions = fixture.Options(8);

    CompileInfoFacts facts(&fixture.chunk);
    ASSERT_EQ(facts.FindExpression(kCseHash, first->GetOpcode(), inputs, options, true), nullptr);
    facts.AddExpression(kCseHash, first, inputs, options, true);
    ASSERT_EQ(facts.FindExpression(kCseHash, first->GetOpcode(), inputs, options, true), first);
    // A different hash, opcode, input list or option list must not match.
    ASSERT_EQ(facts.FindExpression(kCseHash + 1U, first->GetOpcode(), inputs, options, true), nullptr);
    ASSERT_EQ(facts.FindExpression(kCseHash, tagged->GetOpcode(), inputs, options, true), nullptr);
    ASSERT_EQ(facts.FindExpression(kCseHash, first->GetOpcode(), otherInputs, options, true), nullptr);
    ASSERT_EQ(facts.FindExpression(kCseHash, first->GetOpcode(), inputs, otherOptions, true), nullptr);

    // An effect advances the epoch: the epoch-dependent expression is no longer
    // available and the erase is observable through a re-recorded node.
    const uint32_t epochBeforeEffect = facts.GetEffectEpoch();
    facts.IncrementEffectEpoch();
    ASSERT_EQ(facts.GetEffectEpoch(), epochBeforeEffect + 1U);
    ASSERT_EQ(facts.FindExpression(kCseHash, first->GetOpcode(), inputs, options, true), nullptr);
    facts.AddExpression(kCseHash, second, inputs, options, true);
    ASSERT_EQ(facts.FindExpression(kCseHash, second->GetOpcode(), inputs, options, true), second);

    // A pure expression does not depend on the effect epoch and survives it.
    facts.AddExpression(kPureCseHash, first, inputs, options, false);
    facts.IncrementEffectEpoch();
    ASSERT_EQ(facts.FindExpression(kPureCseHash, first->GetOpcode(), inputs, options, false), first);
    ASSERT_EQ(facts.FindExpression(kCseHash, second->GetOpcode(), inputs, options, true), nullptr)
        << "the epoch-dependent expression must not survive a second effect";

    // Clearing the CSE map drops both kinds of expression.
    CompileInfoFacts::ExpressionOptions unusedOptions = fixture.Options(11);
    facts.AddExpression(kPureCseHash, first, inputs, unusedOptions, false);
    facts.ClearAvailableExpressions();
    ASSERT_EQ(facts.FindExpression(kPureCseHash, first->GetOpcode(), inputs, unusedOptions, false), nullptr);
    ASSERT_EQ(facts.FindExpression(kCseHash, first->GetOpcode(), inputs, options, true), nullptr);
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, CompileInfoFactsCseMutationControlIsDetected)
{
    GraphFixture fixture;
    ValueVertex *lhs = fixture.Int32(5);
    ValueVertex *rhs = fixture.Int32(6);
    ValueVertex *first = fixture.NewAdd(lhs, rhs);
    ValueVertex *second = fixture.NewAdd(lhs, rhs);
    CompileInfoFacts::ExpressionInputs inputs = fixture.Inputs({lhs, rhs});
    CompileInfoFacts::ExpressionOptions options = fixture.Options(9);

    CompileInfoFacts facts(&fixture.chunk);
    facts.AddExpression(kCseHash, first, inputs, options, true);
    const auto lookupIs = [&](ValueVertex *expected) {
        return facts.FindExpression(kCseHash, first->GetOpcode(), inputs, options, true) == expected;
    };
    ASSERT_TRUE(lookupIs(first));
    EXPECT_FALSE(lookupIs(second)) << "a wrong expected node must make the lookup red";
    // After a write the epoch-checked expression must be gone: expecting the
    // old node is the wrong-invalidation mutation and must be detected.
    facts.IncrementEffectEpoch();
    EXPECT_FALSE(lookupIs(first)) << "a stale expectation after a write must be detected";
    EXPECT_TRUE(lookupIs(nullptr));
}

// --- CompileInfoFacts: cached env slots ------------------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, CompileInfoFactsEnvSlotFactsAndAliasing)
{
    GraphFixture fixture;
    ValueVertex *aliasingEnv = fixture.NewPhi();
    ValueVertex *otherAliasingEnv = fixture.NewPhi();
    ValueVertex *slotValue = fixture.Int32(1);
    ValueVertex *otherValue = fixture.Int32(2);

    CompileInfoFacts facts(&fixture.chunk);
    ASSERT_EQ(facts.LookupEnvSlot(aliasingEnv, 5), nullptr);
    facts.RecordEnvSlot(aliasingEnv, 5, slotValue);
    EXPECT_EQ(facts.LookupEnvSlot(aliasingEnv, 5), slotValue);
    EXPECT_EQ(facts.LookupEnvSlot(otherAliasingEnv, 5), nullptr) << "slots are keyed by env and slot";
    EXPECT_EQ(facts.LookupEnvSlot(aliasingEnv, 6), nullptr);
    // A phi env is neither the current env nor a constant env, so distinct env
    // vertices may alias (EnvSlotAliasMode contract).
    EXPECT_EQ(facts.GetEnvSlotAliasMode(), EnvSlotAliasMode::MAY_ALIAS);

    // Writing another slot of the same env does not invalidate the cached slot.
    EXPECT_TRUE(facts.ClearAliasedEnvSlotsFor(aliasingEnv, 6, otherValue).empty());
    EXPECT_EQ(facts.LookupEnvSlot(aliasingEnv, 5), slotValue);

    // Writing the same slot of an aliasing env with a new value clears it.
    CompileInfoFacts::ClearedEnvSlotKeys cleared = facts.ClearAliasedEnvSlotsFor(otherAliasingEnv, 5, otherValue);
    ASSERT_EQ(cleared.size(), 1U);
    EXPECT_EQ(cleared[0].GetEnv(), aliasingEnv);
    EXPECT_EQ(cleared[0].GetSlot(), 5);
    EXPECT_EQ(facts.LookupEnvSlot(aliasingEnv, 5), nullptr);

    // Writing the same value back is not an invalidation.
    facts.RecordEnvSlot(aliasingEnv, 5, slotValue);
    EXPECT_TRUE(facts.ClearAliasedEnvSlotsFor(otherAliasingEnv, 5, slotValue).empty());
    EXPECT_EQ(facts.LookupEnvSlot(aliasingEnv, 5), slotValue);

    // Global-env and parent-env fields are tracked as constants and must not
    // invalidate cached user slots (`BaseEnv`/`LexicalEnv` field layout).
    const int32_t taggedSize = static_cast<int32_t>(JSTaggedValue::TaggedTypeSize());
    const int32_t globalEnvOffset =
        static_cast<int32_t>(TaggedArray::DATA_OFFSET) + static_cast<int32_t>(BaseEnv::GLOBAL_ENV_INDEX) * taggedSize;
    const int32_t parentEnvOffset =
        static_cast<int32_t>(TaggedArray::DATA_OFFSET) +
        static_cast<int32_t>(LexicalEnv::PARENT_ENV_INDEX) * taggedSize;
    EXPECT_TRUE(facts.ClearAliasedEnvSlotsFor(aliasingEnv, globalEnvOffset, otherValue).empty());
    EXPECT_EQ(facts.LookupEnvSlot(aliasingEnv, 5), slotValue);
    EXPECT_TRUE(facts.ClearAliasedEnvSlotsFor(aliasingEnv, parentEnvOffset, otherValue).empty());
    EXPECT_EQ(facts.LookupEnvSlot(aliasingEnv, 5), slotValue);

    // An unknown env or slot is a full side effect: every cached slot is
    // dropped, the alias mode is reset and the epoch advances.
    const uint32_t epochBefore = facts.GetEffectEpoch();
    EXPECT_TRUE(facts.ClearAliasedEnvSlotsFor(nullptr, 5, otherValue).empty());
    EXPECT_EQ(facts.GetEffectEpoch(), epochBefore + 1U);
    EXPECT_EQ(facts.LookupEnvSlot(aliasingEnv, 5), nullptr);
    EXPECT_EQ(facts.GetEnvSlotAliasMode(), EnvSlotAliasMode::NONE);
    facts.RecordEnvSlot(aliasingEnv, 5, slotValue);
    EXPECT_TRUE(facts.ClearAliasedEnvSlotsFor(aliasingEnv, -1, otherValue).empty());
    EXPECT_EQ(facts.LookupEnvSlot(aliasingEnv, 5), nullptr);
    EXPECT_EQ(facts.GetEnvSlotAliasMode(), EnvSlotAliasMode::NONE);

    // The current env and a constant env cannot alias another env vertex, so
    // their cached slots survive an aliasing write.
    GraphFixture secondFixture;
    ValueVertex *currentEnv = secondFixture.NewInitialValue();
    ValueVertex *constantEnv = secondFixture.Tagged(JSTaggedValue(1));
    ValueVertex *currentValue = secondFixture.Int32(1);
    ValueVertex *otherCurrentValue = secondFixture.Int32(2);

    CompileInfoFacts currentOnlyFacts(&secondFixture.chunk);
    currentOnlyFacts.RecordEnvSlot(currentEnv, 5, currentValue);
    EXPECT_EQ(currentOnlyFacts.GetEnvSlotAliasMode(), EnvSlotAliasMode::CURRENT_ENV_ONLY);
    EXPECT_TRUE(currentOnlyFacts.ClearAliasedEnvSlotsFor(currentEnv, 5, otherCurrentValue).empty());
    EXPECT_EQ(currentOnlyFacts.LookupEnvSlot(currentEnv, 5), currentValue);

    CompileInfoFacts constantOnlyFacts(&secondFixture.chunk);
    constantOnlyFacts.RecordEnvSlot(constantEnv, 5, currentValue);
    EXPECT_EQ(constantOnlyFacts.GetEnvSlotAliasMode(), EnvSlotAliasMode::CONSTANT_ENV_ONLY);
    EXPECT_TRUE(constantOnlyFacts.ClearAliasedEnvSlotsFor(constantEnv, 5, otherCurrentValue).empty());
    EXPECT_EQ(constantOnlyFacts.LookupEnvSlot(constantEnv, 5), currentValue);
}

// --- CompileInfoFacts: clone variants --------------------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, CompileInfoFactsCloneVariants)
{
    GraphFixture fixture;
    ValueVertex *node = fixture.Int32(7);
    ValueVertex *withoutInfo = fixture.Int32(8);
    ValueVertex *receiver = fixture.Int32(9);
    ValueVertex *value = fixture.Int32(10);
    ValueVertex *constant = fixture.Int32(11);
    ValueVertex *env = fixture.NewInitialValue();
    CompileInfoFacts::ExpressionInputs inputs = fixture.Inputs({receiver, value});
    CompileInfoFacts::ExpressionOptions options = fixture.Options(1);

    CompileInfoFacts facts(&fixture.chunk);
    facts.GetOrCreateInfoFor(node)->SetType(NodeType::INT);
    facts.GetOrCreateInfoFor(withoutInfo);
    facts.RecordNonHole(node);
    facts.RecordLoadedProperty(receiver, PropertyKey::Named(1), value);
    facts.RecordLoadedConstantProperty(receiver, PropertyKey::Named(2), value);
    facts.RecordEnvSlot(env, 5, value);
    facts.RecordEnvConstant(env, 6, value);
    facts.AddExpression(kCseHash, node, inputs, options, true);
    facts.AddExpression(kPureCseHash, node, inputs, options, false);
    ASSERT_TRUE(facts.TryGetInfoFor(withoutInfo)->NoInfoAvailable());

    // Plain clone: every kind of fact is copied and the copy is independent.
    CompileInfoFacts *clone = facts.Clone();
    ASSERT_NE(clone, nullptr);
    ASSERT_NE(clone, &facts);
    ASSERT_EQ(clone->GetKnownType(node), NodeType::INT);
    ASSERT_TRUE(clone->IsKnownNonHole(node));
    ASSERT_EQ(clone->LookupLoadedProperty(receiver, PropertyKey::Named(1)), value);
    ASSERT_EQ(clone->LookupLoadedConstantProperty(receiver, PropertyKey::Named(2)), value);
    ASSERT_EQ(clone->LookupEnvSlot(env, 5), value);
    ASSERT_EQ(clone->LookupEnvConstant(env, 6), value);
    ASSERT_NE(clone->TryGetInfoFor(withoutInfo), nullptr);
    ASSERT_EQ(clone->FindExpression(kCseHash, node->GetOpcode(), inputs, options, true), node);
    clone->TryGetInfoFor(node)->SetType(NodeType::NONE);
    ASSERT_EQ(facts.GetKnownType(node), NodeType::INT) << "the copy must be independent of the original";
    ASSERT_TRUE(NodeInfo::IsEmptyNodeType(clone->GetKnownType(node)));

    // Loop-header clone: information-free entries and epoch-dependent facts are
    // dropped, constants are kept, the epoch advances and aliasing is reset.
    CompileInfoFacts *header = facts.CloneForLoopHeader();
    ASSERT_NE(header, nullptr);
    ASSERT_EQ(header->GetKnownType(node), NodeType::INT);
    ASSERT_TRUE(header->IsKnownNonHole(node));
    ASSERT_NE(header->TryGetInfoFor(node), nullptr);
    ASSERT_EQ(header->TryGetInfoFor(withoutInfo), nullptr) << "entries without information are dropped";
    ASSERT_NE(facts.TryGetInfoFor(withoutInfo), nullptr);
    ASSERT_EQ(header->LookupLoadedProperty(receiver, PropertyKey::Named(1)), nullptr);
    ASSERT_EQ(header->LookupLoadedConstantProperty(receiver, PropertyKey::Named(2)), value);
    ASSERT_EQ(header->LookupEnvSlot(env, 5), nullptr);
    ASSERT_EQ(header->LookupEnvConstant(env, 6), value);
    ASSERT_GT(header->GetEffectEpoch(), facts.GetEffectEpoch());
    ASSERT_EQ(header->GetEnvSlotAliasMode(), EnvSlotAliasMode::NONE);
    ASSERT_EQ(header->FindExpression(kCseHash, node->GetOpcode(), inputs, options, true), nullptr);
    ASSERT_EQ(header->FindExpression(kPureCseHash, node->GetOpcode(), inputs, options, false), node);

    // Optimistic loop-header clone without effects: ordinary loaded facts stay.
    LoopEffects noEffects(&fixture.chunk);
    CompileInfoFacts *optimistic = facts.CloneForOptimisticLoopHeader(noEffects);
    ASSERT_NE(optimistic, nullptr);
    ASSERT_EQ(optimistic->GetKnownType(node), NodeType::INT);
    ASSERT_NE(optimistic->TryGetInfoFor(withoutInfo), nullptr);
    ASSERT_EQ(optimistic->LookupLoadedProperty(receiver, PropertyKey::Named(1)), value);
    ASSERT_EQ(optimistic->LookupEnvSlot(env, 5), value);
    ASSERT_EQ(optimistic->LookupLoadedConstantProperty(receiver, PropertyKey::Named(2)), value);
    ASSERT_EQ(optimistic->LookupEnvConstant(env, 6), value);
    ASSERT_GT(optimistic->GetEffectEpoch(), facts.GetEffectEpoch());

    // Optimistic loop-header clone with an unknown call: information-free
    // entries and loaded property/env-slot facts are dropped, constants stay.
    LoopEffects unknownCallEffects(&fixture.chunk);
    SideEffectDescriptor unknownCall;
    unknownCall.kind = SideEffectKind::UNKNOWN_CALL;
    unknownCallEffects.Record(unknownCall);
    ASSERT_TRUE(unknownCallEffects.ClearsUnstableNodeInfo());
    ASSERT_TRUE(unknownCallEffects.InvalidatesAllEnvSlots());
    CompileInfoFacts *clearedClone = facts.CloneForOptimisticLoopHeader(unknownCallEffects);
    ASSERT_NE(clearedClone, nullptr);
    ASSERT_NE(clearedClone->TryGetInfoFor(node), nullptr);
    ASSERT_EQ(clearedClone->TryGetInfoFor(withoutInfo), nullptr);
    ASSERT_EQ(clearedClone->LookupLoadedProperty(receiver, PropertyKey::Named(1)), nullptr);
    ASSERT_EQ(clearedClone->LookupEnvSlot(env, 5), nullptr);
    ASSERT_EQ(clearedClone->LookupLoadedConstantProperty(receiver, PropertyKey::Named(2)), value);
    ASSERT_EQ(clearedClone->LookupEnvConstant(env, 6), value);
}

// --- CompileInfoFacts: loop-header compatibility ---------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, CompileInfoFactsLoopHeaderCompatibility)
{
    GraphFixture fixture;
    ValueVertex *intNode = fixture.Int32(1);
    ValueVertex *doubleNode = fixture.Int32(2);
    ValueVertex *receiver = fixture.Int32(3);
    ValueVertex *loaded = fixture.Int32(4);
    ValueVertex *env = fixture.NewInitialValue();
    ValueVertex *envValue = fixture.Int32(5);

    CompileInfoFacts facts(&fixture.chunk);
    facts.GetOrCreateInfoFor(intNode)->SetType(NodeType::INT);
    facts.GetOrCreateInfoFor(doubleNode)->SetType(NodeType::DOUBLE);
    facts.RecordLoadedProperty(receiver, PropertyKey::Named(1), loaded);
    facts.RecordEnvSlot(env, 5, envValue);
    ASSERT_EQ(facts.GetEnvSlotAliasMode(), EnvSlotAliasMode::CURRENT_ENV_ONLY);

    BB *headerBlock = BB::New(&fixture.chunk);
    headerBlock->SetId(7);
    CompileInfoFacts *body = facts.Clone();
    CompileInfoFacts *header = facts.CloneForLoopHeader();
    // The body facts at the back edge carry every fact the loop header states,
    // so they are compatible with it.
    ASSERT_TRUE(body->IsCompatibleWithLoopHeader(*header, headerBlock));

    // A header entry that lost type information is incompatible.
    CompileInfoFacts *narrowedHeader = facts.CloneForLoopHeader();
    narrowedHeader->GetOrCreateInfoFor(doubleNode)->SetType(NodeType::INT);
    ASSERT_FALSE(body->IsCompatibleWithLoopHeader(*narrowedHeader, headerBlock));

    // A header entry the body does not have is incompatible.
    CompileInfoFacts *extendedHeader = facts.CloneForLoopHeader();
    ValueVertex *extra = fixture.Int32(6);
    extendedHeader->GetOrCreateInfoFor(extra)->SetType(NodeType::INT);
    ASSERT_FALSE(body->IsCompatibleWithLoopHeader(*extendedHeader, headerBlock));

    // Phi vertices owned by the loop header are skipped: their value is
    // reconstructed at the back edge and cannot be compared there.
    CompileInfoFacts *headerWithPhi = facts.CloneForLoopHeader();
    PhiVertex *phi = Vertex::New<PhiVertex>(&fixture.chunk, 1, VirtualRegister(0));
    phi->SetInput(0, intNode);
    phi->SetOwner(headerBlock);
    headerWithPhi->GetOrCreateInfoFor(phi)->SetType(NodeType::INT);
    ASSERT_TRUE(body->IsCompatibleWithLoopHeader(*headerWithPhi, headerBlock));
    BB *otherBlock = BB::New(&fixture.chunk);
    otherBlock->SetId(8);
    ASSERT_FALSE(body->IsCompatibleWithLoopHeader(*headerWithPhi, otherBlock));

    // Equal epochs compare types only (subset relation), never HClasses.
    CompileInfoFacts *sameEpochHeader = facts.Clone();
    CompileInfoFacts *sameEpochBody = facts.Clone();
    sameEpochHeader->GetOrCreateInfoFor(doubleNode)->SetType(NodeType::INT);
    ASSERT_FALSE(sameEpochBody->IsCompatibleWithLoopHeader(*sameEpochHeader, headerBlock));
    sameEpochBody->GetOrCreateInfoFor(doubleNode)->SetType(NodeType::INT);
    sameEpochHeader->GetOrCreateInfoFor(doubleNode)->SetType(NodeType::NUMBER);
    ASSERT_TRUE(sameEpochBody->IsCompatibleWithLoopHeader(*sameEpochHeader, headerBlock));

    // Cached env slots of the header interact with the aliasing mode of the
    // body: an incompatible non-aliasing mode is rejected.
    LoopEffects noEffects(&fixture.chunk);
    CompileInfoFacts *optimisticHeader = facts.CloneForOptimisticLoopHeader(noEffects);
    ASSERT_EQ(optimisticHeader->LookupEnvSlot(env, 5), envValue);
    ASSERT_EQ(optimisticHeader->GetEnvSlotAliasMode(), EnvSlotAliasMode::CURRENT_ENV_ONLY);
    CompileInfoFacts *aliasingBody = facts.Clone();
    aliasingBody->SetEnvSlotAliasMode(EnvSlotAliasMode::NONE);
    ASSERT_TRUE(aliasingBody->IsCompatibleWithLoopHeader(*optimisticHeader, headerBlock));
    aliasingBody->SetEnvSlotAliasMode(EnvSlotAliasMode::CONSTANT_ENV_ONLY);
    ASSERT_FALSE(aliasingBody->IsCompatibleWithLoopHeader(*optimisticHeader, headerBlock));
}

}  // namespace panda::ecmascript::arksteed
