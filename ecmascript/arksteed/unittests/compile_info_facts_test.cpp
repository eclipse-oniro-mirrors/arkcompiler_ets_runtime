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
#include "ecmascript/arksteed/arksteed_cse.h"
#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_heap_broker.h"
#include "ecmascript/arksteed/arksteed_loop_effects.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/ecma_string.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_object.h"
#include "ecmascript/jit/jit_task.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/tests/test_helper.h"

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

bool NarrowedHClassIsExpected(const NodeInfo &info, ArkSteedHClassRef expectedHClass, NodeType expectedType)
{
    return info.HasFreshKnownHClass() && info.GetFreshKnownHClass().Equals(expectedHClass) &&
           Bits(info.GetType()) == Bits(expectedType);
}

// Ref creation uses the same task-owned handles and heap-access scope as compilation.
void WithHClassRefs(EcmaVM *vm, JSThread *thread, const std::function<void(ArkSteedHClassRef, ArkSteedHClassRef)> &body)
{
    vm->SetEnableForceGC(false);
    vm->GetJSOptions().SetCompilerEnableLiteCG(true);
    Jit::GetInstance()->PreFork();
    auto *factory = vm->GetFactory();
    auto function = factory->NewJSFunction(vm->GetGlobalEnv());
    auto lexicalEnv = factory->NewLexicalEnv(0);
    lexicalEnv->SetGlobalEnv(thread, vm->GetGlobalEnv().GetTaggedValue());
    function->SetLexicalEnv(thread, lexicalEnv.GetTaggedValue());
    auto plainClass = factory->NewEcmaHClass(static_cast<uint32_t>(JSObject::SIZE), JSType::JS_OBJECT);
    auto array = factory->NewJSArray();
    std::thread worker([vm, thread, &function, plainClass, array, &body] {
        auto *jitVM = JitVM::Create();
        jitVM->SetHostVM(thread);
        auto *jitThread = static_cast<JitThread *>(jitVM->GetJSThreadNoCheck());
        CString name("compile-info-facts-test");
        {
            JitTask task(thread, jitThread, Jit::GetInstance(), function, CompilerTier(CompilerTier::Tier::FAST),
                         name, 0, JitCompileMode(JitCompileMode::Mode::SYNC));
            jitThread->SetCurrentTask(&task);
            {
                JitHeapAccessScope heapAccess(jitThread);
                JitCompilationEnv env(jitVM, vm, function, task.GetDependencies());
                ArkSteedHeapBroker broker(jitThread, &env);
                auto plainHClass = broker.MakeObjectRef(plainClass);
                auto arrayHClass = broker.MakeHClassRef(array->GetClass());
                body(plainHClass, arrayHClass);
            }
            jitThread->SetCurrentTask(nullptr);
        }
        jitVM->ReSetHostVM();
        JitVM::Destroy(jitVM);
    });
    ThreadNativeScope native(thread);
    worker.join();
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

    std::vector<ValueVertex *> Inputs(std::initializer_list<ValueVertex *> values)
    {
        return std::vector<ValueVertex *>(values);
    }

    ValueVertex *NewPhi()
    {
        return Vertex::New<PhiVertex>(&chunk, 1, VirtualRegister(0));
    }

    ValueVertex *NewInitialValue()
    {
        return Vertex::New<InitialValueVertex>(&chunk, 0, 0);
    }

    I32AddVertex *NewAdd(ValueVertex *lhs, ValueVertex *rhs)
    {
        auto *vertex = Vertex::New<I32AddVertex>(&chunk, 2);
        vertex->SetInput(0, lhs);
        vertex->SetInput(1, rhs);
        return vertex;
    }

    LoadTaggedFieldVertex *NewFieldLoad(ValueVertex *receiver, int32_t offset)
    {
        auto *vertex = Vertex::New<LoadTaggedFieldVertex>(&chunk, 1, offset);
        vertex->SetInput(0, receiver);
        return vertex;
    }
};

struct HamtTestKey {
    uint64_t hash;
    uint32_t id;
};

struct HamtTestHash {
    uint64_t operator()(const HamtTestKey &key) const
    {
        return key.hash;
    }
};

struct HamtTestEqual {
    bool operator()(const HamtTestKey &left, const HamtTestKey &right) const
    {
        return left.hash == right.hash && left.id == right.id;
    }
};

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

    WithHClassRefs(scope.GetVm(), scope.GetThread(), [&](ArkSteedHClassRef plainHClass, ArkSteedHClassRef arrayHClass) {
        ASSERT_TRUE(plainHClass.IsJSHClass());
        ASSERT_TRUE(arrayHClass.IsJSHClass());
        ASSERT_FALSE(plainHClass.Equals(arrayHClass));
        ASSERT_EQ(NodeTypeFromHClass(plainHClass), NodeType::OTHER_JS_RECEIVER);
        ASSERT_EQ(NodeTypeFromHClass(arrayHClass), NodeType::JS_ARRAY);

        NativeAreaAllocator nodeInfoAllocator;
        Chunk nodeInfoChunk {&nodeInfoAllocator};

        NodeInfo info(&nodeInfoChunk);
        ASSERT_TRUE(info.NoInfoAvailable());
        info.RecordPossibleHClasses({plainHClass, arrayHClass}, true);
        ASSERT_EQ(info.PossibleHClassCount(), 2U);
        ASSERT_TRUE(info.HasFreshPossibleHClasses());
        ASSERT_FALSE(info.HasFreshKnownHClass());
        ASSERT_FALSE(info.PossibleHClassesAreStale());
        ASSERT_FALSE(info.HasUnstablePossibleHClass());
        // The recorded receiver classes narrow the unknown type to the union
        // of their atom types (ANY_HEAP_OBJECT is not part of that union).
        ASSERT_EQ(Bits(info.GetType()), Bits(NodeType::OTHER_JS_RECEIVER) | Bits(NodeType::JS_ARRAY));

        // Narrowing to one class keeps exactly one fresh known class.
        ASSERT_TRUE(info.NarrowPossibleHClasses({plainHClass}));
        ASSERT_EQ(info.PossibleHClassCount(), 1U);
        ASSERT_TRUE(info.HasFreshKnownHClass());
        ASSERT_TRUE(info.GetFreshKnownHClass().Equals(plainHClass));
        ASSERT_EQ(Bits(info.GetType()), Bits(NodeType::OTHER_JS_RECEIVER));
        ASSERT_TRUE(NarrowedHClassIsExpected(info, plainHClass, NodeType::OTHER_JS_RECEIVER));
        ASSERT_TRUE(info.NarrowPossibleHClasses({plainHClass})) << "narrowing to the same set is stable";
        // A disjoint set empties the possible classes and reports failure.
        ASSERT_FALSE(info.NarrowPossibleHClasses({arrayHClass}));
        ASSERT_EQ(info.PossibleHClassCount(), 0U);
        ASSERT_FALSE(info.HasFreshKnownHClass());
        ASSERT_FALSE(info.HasFreshPossibleHClasses());

        // An unstable single class is a fresh known class but not a stable
        // one.
        NodeInfo unstable(&nodeInfoChunk);
        unstable.RecordPossibleHClasses({plainHClass}, false);
        ASSERT_FALSE(unstable.HClassIsStable());
        ASSERT_TRUE(unstable.HasUnstablePossibleHClass());
        ASSERT_TRUE(unstable.HasFreshKnownHClass());
        unstable.MarkUnstableHClassesStale();
        ASSERT_TRUE(unstable.PossibleHClassesAreStale());
        ASSERT_FALSE(unstable.HasFreshKnownHClass());

        // A stable class is never invalidated by MarkUnstableHClassesStale.
        NodeInfo stable(&nodeInfoChunk);
        stable.RecordPossibleHClasses({plainHClass}, true);
        stable.MarkUnstableHClassesStale();
        ASSERT_FALSE(stable.PossibleHClassesAreStale());
        ASSERT_TRUE(stable.HasFreshKnownHClass());

        // Merging two different single-class facts unions the receiver sets.
        NodeInfo left(&nodeInfoChunk);
        left.RecordPossibleHClasses({plainHClass}, true);
        NodeInfo right(&nodeInfoChunk);
        right.RecordPossibleHClasses({arrayHClass}, true);
        ASSERT_TRUE(left.MergeWith(right));
        ASSERT_EQ(left.PossibleHClassCount(), 2U);
        ASSERT_TRUE(left.ContainsPossibleHClass(plainHClass));
        ASSERT_TRUE(left.ContainsPossibleHClass(arrayHClass));
        ASSERT_EQ(Bits(left.GetType()), Bits(NodeType::OTHER_JS_RECEIVER) | Bits(NodeType::JS_ARRAY));
        // A side without any possible class drops the merged set entirely.
        NodeInfo withoutHClasses(&nodeInfoChunk);
        left.MergeWith(withoutHClasses);
        ASSERT_EQ(left.PossibleHClassCount(), 0U);
        ASSERT_FALSE(left.HasFreshPossibleHClasses());

        // IsCompatibleWith: an expected set with unstable entries requires
        // the actual set to be at least as stable and to be a subset of it.
        NodeInfo expectedUnstable(&nodeInfoChunk);
        expectedUnstable.RecordPossibleHClasses({plainHClass}, false);
        NodeInfo actualUnstable(&nodeInfoChunk);
        actualUnstable.RecordPossibleHClasses({plainHClass}, false);
        ASSERT_TRUE(actualUnstable.IsCompatibleWith(expectedUnstable)) << "unstable expected entries are compatible";
        NodeInfo actualOtherClass(&nodeInfoChunk);
        actualOtherClass.RecordPossibleHClasses({arrayHClass}, false);
        ASSERT_FALSE(actualOtherClass.IsCompatibleWith(expectedUnstable)) << "class outside the expected set";

        // In a mixed expected set a stable expected class cannot be covered
        // by an unstable actual class, while a stable actual class is always
        // accepted.
        NodeInfo expectedMixed(&nodeInfoChunk);
        expectedMixed.RecordPossibleHClasses(NodeInfo::PossibleHClassInfos {
            NodeInfo::PossibleHClassInfo {plainHClass, true}, NodeInfo::PossibleHClassInfo {arrayHClass, false}});
        ASSERT_TRUE(expectedMixed.HasUnstablePossibleHClass());
        NodeInfo actualStable(&nodeInfoChunk);
        actualStable.RecordPossibleHClasses({plainHClass}, true);
        ASSERT_TRUE(actualStable.IsCompatibleWith(expectedMixed));
        NodeInfo actualNotAsStable(&nodeInfoChunk);
        actualNotAsStable.RecordPossibleHClasses({plainHClass}, false);
        ASSERT_FALSE(actualNotAsStable.IsCompatibleWith(expectedMixed));

        // A stale expected set is accepted without comparing classes.
        NodeInfo expectedStale(&nodeInfoChunk);
        expectedStale.RecordPossibleHClasses({plainHClass}, false);
        expectedStale.MarkUnstableHClassesStale();
        ASSERT_TRUE(expectedStale.PossibleHClassesAreStale());
        NodeInfo actualTyped(&nodeInfoChunk);
        actualTyped.SetType(NodeType::OTHER_JS_RECEIVER);
        ASSERT_TRUE(actualTyped.IsCompatibleWith(expectedStale));
    });
}

// Copy/move members must keep the scalar facts and the chunk-backed HClass set
// together: a copy re-creates the set independently, and a same-chunk move hands
// the buffer over and empties the donor.
HWTEST_F_L0(ArkSteedCompileInfoFactsTest, NodeInfoCopyAndMoveAssignmentTransferChunkFacts)
{
    ArkSteedCompileInfoFactsTest::ScopedEcmaVm scope;
    ASSERT_TRUE(scope.IsValid()) << "cannot create EcmaVM";
    ASSERT_NE(scope.GetThread(), nullptr);

    WithHClassRefs(scope.GetVm(), scope.GetThread(), [&](ArkSteedHClassRef plainHClass, ArkSteedHClassRef arrayHClass) {
        NativeAreaAllocator allocator;
        Chunk chunk {&allocator};

        NodeInfo source(&chunk);
        source.SetType(NodeType::JS_RECEIVER);
        source.MarkNonHole();
        source.RecordPossibleHClasses({plainHClass, arrayHClass}, false);
        // Recording the classes narrows JS_RECEIVER to the union of the two
        // atom types (IntersectTypeWithPossibleHClasses), so this is the type
        // every copy/move below must preserve.
        const NodeType recordedType = NodeInfo::UnionNodeType(NodeType::OTHER_JS_RECEIVER, NodeType::JS_ARRAY);

        // Copy construction transfers every fact and owns an independent set.
        NodeInfo copied(source);
        ASSERT_EQ(Bits(copied.GetType()), Bits(recordedType));
        ASSERT_TRUE(copied.IsNonHole());
        ASSERT_EQ(copied.PossibleHClassCount(), 2U);
        ASSERT_TRUE(copied.ContainsPossibleHClass(plainHClass));
        ASSERT_TRUE(copied.ContainsPossibleHClass(arrayHClass));
        ASSERT_TRUE(copied.HasUnstablePossibleHClass());
        ASSERT_TRUE(copied.NarrowPossibleHClasses({plainHClass}));
        ASSERT_EQ(copied.PossibleHClassCount(), 1U);
        ASSERT_EQ(source.PossibleHClassCount(), 2U) << "narrowing the copy must not touch the source";

        // The stale flag travels with a copy of a stale set.
        source.MarkUnstableHClassesStale();
        ASSERT_TRUE(source.PossibleHClassesAreStale());
        NodeInfo copiedStale(source);
        ASSERT_TRUE(copiedStale.PossibleHClassesAreStale());

        // Copy assignment replaces the target's facts without consuming the source.
        NodeInfo assigned(&chunk);
        assigned.SetType(NodeType::INT);
        assigned.RecordPossibleHClasses({plainHClass}, true);
        assigned = source;
        ASSERT_EQ(Bits(assigned.GetType()), Bits(recordedType));
        ASSERT_TRUE(assigned.IsNonHole());
        ASSERT_EQ(assigned.PossibleHClassCount(), 2U);
        ASSERT_TRUE(assigned.HasUnstablePossibleHClass());
        ASSERT_EQ(source.PossibleHClassCount(), 2U) << "copy assignment must not consume the source";

        // Move construction hands the chunk-backed set over and empties the donor.
        NodeInfo moveConstructed(std::move(source));
        ASSERT_EQ(Bits(moveConstructed.GetType()), Bits(recordedType));
        ASSERT_TRUE(moveConstructed.IsNonHole());
        ASSERT_EQ(moveConstructed.PossibleHClassCount(), 2U);
        ASSERT_EQ(source.PossibleHClassCount(), 0U) << "a same-chunk move must empty the donor";

        // Move assignment (same chunk) behaves like move construction.
        NodeInfo moveAssigned(&chunk);
        moveAssigned.SetType(NodeType::STRING);
        moveAssigned = std::move(moveConstructed);
        ASSERT_EQ(Bits(moveAssigned.GetType()), Bits(recordedType));
        ASSERT_TRUE(moveAssigned.IsNonHole());
        ASSERT_EQ(moveAssigned.PossibleHClassCount(), 2U);
        ASSERT_EQ(moveConstructed.PossibleHClassCount(), 0U) << "a same-chunk move must empty the donor";
    });
}

// --- NodeInfo: merge and compatibility of plain facts ----------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, NodeInfoMergeWithAndCompatibility)
{
    GraphFixture fixture;
    ValueVertex *alternative = fixture.Int32(1);
    ValueVertex *otherAlternative = fixture.Int32(2);

    NodeInfo lhs(&fixture.chunk);
    lhs.SetType(NodeType::INT);
    lhs.MarkNonHole();
    lhs.GetAlternatives().Set(AlternativeNodes::Kind::INT32, alternative);
    NodeInfo rhs(&fixture.chunk);
    rhs.SetType(NodeType::DOUBLE);
    rhs.GetAlternatives().Set(AlternativeNodes::Kind::INT32, alternative);

    ASSERT_TRUE(lhs.MergeWith(rhs));
    ASSERT_EQ(Bits(lhs.GetType()), Bits(NodeType::INT) | Bits(NodeType::DOUBLE));
    // nonHole is a conjunction of both sides; alternatives survive only when
    // both sides agree on the alternative node.
    ASSERT_FALSE(lhs.IsNonHole());
    ASSERT_EQ(lhs.GetAlternatives().Get(AlternativeNodes::Kind::INT32), alternative);
    NodeInfo conflicting(&fixture.chunk);
    conflicting.SetType(NodeType::INT);
    conflicting.GetAlternatives().Set(AlternativeNodes::Kind::INT32, otherAlternative);
    lhs.MergeWith(conflicting);
    ASSERT_EQ(lhs.GetAlternatives().Get(AlternativeNodes::Kind::INT32), nullptr);

    NodeInfo bothNonHole(&fixture.chunk);
    bothNonHole.SetType(NodeType::INT);
    bothNonHole.MarkNonHole();
    NodeInfo alsoNonHole(&fixture.chunk);
    alsoNonHole.SetType(NodeType::DOUBLE);
    alsoNonHole.MarkNonHole();
    bothNonHole.MergeWith(alsoNonHole);
    ASSERT_TRUE(bothNonHole.IsNonHole());

    // A merge of two disjoint, narrower inputs must widen the type: keeping
    // either of the input types after the merge would be a false merge.
    NodeInfo mergedInt(&fixture.chunk);
    mergedInt.SetType(NodeType::INT);
    NodeInfo mergedString(&fixture.chunk);
    mergedString.SetType(NodeType::STRING);
    ASSERT_TRUE(mergedInt.MergeWith(mergedString));
    ASSERT_EQ(Bits(mergedInt.GetType()), Bits(NodeType::INT) | Bits(NodeType::STRING));
    ASSERT_FALSE(mergedInt.CheckType(NodeType::INT)) << "the merged facts must not keep INT";
    ASSERT_FALSE(mergedInt.CheckType(NodeType::STRING)) << "the merged facts must not keep STRING";
    ASSERT_TRUE(mergedInt.CheckType(NodeInfo::UnionNodeType(NodeType::INT, NodeType::STRING)));

    // Two facts that carry no information at all stay without information, so
    // the merge reports that nothing usable was produced.
    NodeInfo emptyLeft(&fixture.chunk);
    NodeInfo emptyRight(&fixture.chunk);
    ASSERT_TRUE(emptyLeft.NoInfoAvailable());
    ASSERT_FALSE(emptyLeft.MergeWith(emptyRight));
    NodeInfo withType(&fixture.chunk);
    withType.SetType(NodeType::INT);
    ASSERT_FALSE(withType.NoInfoAvailable());
    NodeInfo withNonHole(&fixture.chunk);
    withNonHole.MarkNonHole();
    ASSERT_FALSE(withNonHole.NoInfoAvailable());
    NodeInfo withAlternative(&fixture.chunk);
    withAlternative.GetAlternatives().Set(AlternativeNodes::Kind::TAGGED, alternative);
    ASSERT_FALSE(withAlternative.NoInfoAvailable());

    // Compatibility is the subset relation on types plus the HClass rules; the
    // pairs below are hand-derived from that definition.
    NodeInfo actualInt(&fixture.chunk);
    actualInt.SetType(NodeType::INT);
    NodeInfo expectedInt(&fixture.chunk);
    expectedInt.SetType(NodeType::INT);
    NodeInfo expectedNumber(&fixture.chunk);
    expectedNumber.SetType(NodeType::NUMBER);
    NodeInfo expectedString(&fixture.chunk);
    expectedString.SetType(NodeType::STRING);
    NodeInfo expectedUnknown(&fixture.chunk);
    expectedUnknown.SetType(NodeType::UNKNOWN);
    NodeInfo actualNumber(&fixture.chunk);
    actualNumber.SetType(NodeType::NUMBER);
    NodeInfo actualUnknown(&fixture.chunk);
    actualUnknown.SetType(NodeType::UNKNOWN);
    NodeInfo actualEmpty(&fixture.chunk);
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

    WithHClassRefs(scope.GetVm(), scope.GetThread(), [&](ArkSteedHClassRef plainHClass, ArkSteedHClassRef arrayHClass) {
        ASSERT_TRUE(plainHClass.IsJSHClass());
        ASSERT_TRUE(arrayHClass.IsJSHClass());
        ASSERT_FALSE(plainHClass.Equals(arrayHClass));

        NativeAreaAllocator nodeInfoAllocator;
        Chunk nodeInfoChunk {&nodeInfoAllocator};

        NodeInfo info(&nodeInfoChunk);
        info.RecordPossibleHClasses({plainHClass}, true);
        ASSERT_TRUE(NarrowedHClassIsExpected(info, plainHClass, NodeType::OTHER_JS_RECEIVER));
        EXPECT_FALSE(NarrowedHClassIsExpected(info, arrayHClass, NodeType::OTHER_JS_RECEIVER))
            << "a substituted expected HClass must be detected";
        EXPECT_FALSE(NarrowedHClassIsExpected(info, plainHClass, NodeType::JS_ARRAY))
            << "a substituted expected receiver type must be detected";
    });
}

// --- CompileInfoFacts: CSE -------------------------------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, FixedInputConstructionAndHashKeepTheirDynamicContracts)
{
    static_assert(HasFixedInputCount<Int32ConstantVertex>::value);
    static_assert(HasFixedInputCount<LoadTaggedFieldVertex>::value);
    static_assert(HasFixedInputCount<I32SubWithOverflowVertex>::value);
    static_assert(!HasFixedInputCount<PhiVertex>::value);

    GraphFixture fixture;
    ValueVertex *receiver = fixture.NewInitialValue();
    ValueVertex *lhs = fixture.Int32(3);
    ValueVertex *rhs = fixture.Int32(4);
    std::array<ValueVertex *, 0> noInputs {};
    std::array<ValueVertex *, 1> loadInputs {receiver};
    ChunkVector<ValueVertex *> subInputs(&fixture.chunk);
    subInputs.push_back(lhs);
    subInputs.push_back(rhs);

    auto *constant = Vertex::New<Int32ConstantVertex>(&fixture.chunk, noInputs, 17);
    auto *load = Vertex::New<LoadTaggedFieldVertex>(&fixture.chunk, loadInputs, 16);
    auto *sub = Vertex::New<I32SubWithOverflowVertex>(&fixture.chunk, subInputs, &fixture.chunk, 0);
    auto *phi = Vertex::New<PhiVertex>(&fixture.chunk, std::initializer_list<ValueVertex *> {lhs, rhs, receiver},
                                       VirtualRegister(0));
    ASSERT_NE(constant, nullptr);
    ASSERT_NE(load, nullptr);
    ASSERT_NE(sub, nullptr);
    ASSERT_NE(phi, nullptr);
    EXPECT_EQ(static_cast<Vertex *>(constant)->GetInputCount(), 0U);
    EXPECT_EQ(static_cast<Vertex *>(load)->GetInputCount(), 1U);
    EXPECT_EQ(static_cast<Vertex *>(sub)->GetInputCount(), 2U);
    EXPECT_EQ(phi->GetInputCount(), 3U);
    EXPECT_EQ(load->GetInput(0), receiver);
    EXPECT_EQ(sub->GetInput(0), lhs);
    EXPECT_EQ(sub->GetInput(1), rhs);
    EXPECT_EQ(phi->GetInput(0), lhs);
    EXPECT_EQ(phi->GetInput(1), rhs);
    EXPECT_EQ(phi->GetInput(2), receiver);

    // The unchanged dynamic hash remains the reference for all fixed loop bounds.
    EXPECT_EQ(CseHashInputs<0>(VertexOpcode::Int32Constant, MakeCseInputView(noInputs)),
              CseHashInputs(VertexOpcode::Int32Constant, MakeCseInputView(noInputs)));
    EXPECT_EQ(CseHashInputs<1>(VertexOpcode::LoadTaggedField, MakeCseInputView(loadInputs)),
              CseHashInputs(VertexOpcode::LoadTaggedField, MakeCseInputView(loadInputs)));
    EXPECT_EQ(CseHashInputs<2>(VertexOpcode::I32SubWithOverflow, MakeCseInputView(subInputs)),
              CseHashInputs(VertexOpcode::I32SubWithOverflow, MakeCseInputView(subInputs)));

    std::array<ValueVertex *, 2> ordered {lhs, rhs};
    std::array<ValueVertex *, 2> reversed {rhs, lhs};
    EXPECT_EQ(CseHashQuery(MakeCseQuery<I32AddVertex>(ordered)), CseHashQuery(MakeCseQuery<I32AddVertex>(reversed)));
    EXPECT_NE(CseHashQuery(MakeCseQuery<I32SubVertex>(ordered)), CseHashQuery(MakeCseQuery<I32SubVertex>(reversed)));
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, PrecomputedHashKeepsCollisionMatchingAndEpochChecks)
{
    GraphFixture fixture;
    ValueVertex *receiver = fixture.NewInitialValue();
    ValueVertex *otherReceiver = fixture.NewInitialValue();
    std::array<ValueVertex *, 1> inputs {receiver};
    std::array<ValueVertex *, 1> otherInputs {otherReceiver};
    auto firstQuery = MakeCseQuery<LoadTaggedFieldVertex>(inputs, 16);
    auto secondQuery = MakeCseQuery<LoadTaggedFieldVertex>(inputs, 24);
    auto absentQuery = MakeCseQuery<LoadTaggedFieldVertex>(otherInputs, 16);
    auto *first = fixture.NewFieldLoad(receiver, 16);
    auto *second = fixture.NewFieldLoad(receiver, 24);
    CompileInfoFacts facts(&fixture.chunk);

    // Put distinct valid queries in one bucket, independently of the hash distribution.
    constexpr uint64_t collidingHash = 42;
    facts.GetCse().AddExpression(firstQuery, first, collidingHash, facts.GetEffectEpoch());
    facts.GetCse().AddExpression(secondQuery, second, collidingHash, facts.GetEffectEpoch());
    EXPECT_EQ(facts.GetCse().Find(firstQuery, collidingHash, facts.GetEffectEpoch()), first);
    EXPECT_EQ(facts.GetCse().Find(secondQuery, collidingHash, facts.GetEffectEpoch()), second);
    EXPECT_EQ(facts.GetCse().Find(absentQuery, collidingHash, facts.GetEffectEpoch()), nullptr);
    facts.IncrementEffectEpoch();
    EXPECT_EQ(facts.GetCse().Find(firstQuery, collidingHash, facts.GetEffectEpoch()), nullptr);
    EXPECT_EQ(facts.GetCse().Find(secondQuery, collidingHash, facts.GetEffectEpoch()), nullptr);
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, CompileInfoFactsCseLifecycle)
{
    GraphFixture fixture;
    ValueVertex *receiver = fixture.NewInitialValue();
    ValueVertex *otherReceiver = fixture.NewInitialValue();
    ValueVertex *lhs = fixture.Int32(3);
    ValueVertex *rhs = fixture.Int32(4);
    std::array<ValueVertex *, 1> loadInputs {receiver};
    std::array<ValueVertex *, 1> otherInputs {otherReceiver};
    std::array<ValueVertex *, 2> addInputs {lhs, rhs};
    auto loadQuery = MakeCseQuery<LoadTaggedFieldVertex>(loadInputs, 16);
    auto otherOffset = MakeCseQuery<LoadTaggedFieldVertex>(loadInputs, 24);
    auto otherObject = MakeCseQuery<LoadTaggedFieldVertex>(otherInputs, 16);
    auto pureQuery = MakeCseQuery<I32AddVertex>(addInputs);
    auto *first = fixture.NewFieldLoad(receiver, 16);
    auto *second = fixture.NewFieldLoad(receiver, 16);
    auto *otherField = fixture.NewFieldLoad(receiver, 24);
    auto *pure = fixture.NewAdd(lhs, rhs);

    CompileInfoFacts facts(&fixture.chunk);
    ASSERT_EQ(facts.GetCse().Find(loadQuery, CseHashQuery(loadQuery), facts.GetEffectEpoch()), nullptr);
    facts.GetCse().AddExpression(loadQuery, first, CseHashQuery(loadQuery), facts.GetEffectEpoch());
    facts.GetCse().AddExpression(otherOffset, otherField, CseHashQuery(otherOffset), facts.GetEffectEpoch());
    ASSERT_EQ(facts.GetCse().Find(loadQuery, CseHashQuery(loadQuery), facts.GetEffectEpoch()), first);
    ASSERT_EQ(facts.GetCse().Find(otherOffset, CseHashQuery(otherOffset), facts.GetEffectEpoch()), otherField);
    ASSERT_EQ(facts.GetCse().Find(otherObject, CseHashQuery(otherObject), facts.GetEffectEpoch()), nullptr);

    SideEffectDescriptor safeCall {SideEffectKind::SAFE_CALL};
    safeCall.advancesEffectEpoch = false;
    const uint32_t epochBeforeCall = facts.GetEffectEpoch();
    facts.MarkPossibleSideEffect(safeCall);
    ASSERT_EQ(facts.GetEffectEpoch(), epochBeforeCall);
    ASSERT_EQ(facts.GetCse().Find(loadQuery, CseHashQuery(loadQuery), facts.GetEffectEpoch()), first);

    safeCall.advancesEffectEpoch = true;
    facts.MarkPossibleSideEffect(safeCall);
    ASSERT_EQ(facts.GetCse().Find(loadQuery, CseHashQuery(loadQuery), facts.GetEffectEpoch()), nullptr);
    ASSERT_EQ(facts.GetCse().Find(otherOffset, CseHashQuery(otherOffset), facts.GetEffectEpoch()), nullptr);
    facts.GetCse().AddExpression(loadQuery, second, CseHashQuery(loadQuery), facts.GetEffectEpoch());
    ASSERT_EQ(facts.GetCse().Find(loadQuery, CseHashQuery(loadQuery), facts.GetEffectEpoch()), second);

    facts.GetCse().AddExpression(pureQuery, pure, CseHashQuery(pureQuery), facts.GetEffectEpoch());
    facts.IncrementEffectEpoch();
    ASSERT_EQ(facts.GetCse().Find(pureQuery, CseHashQuery(pureQuery), facts.GetEffectEpoch()), pure);
    ASSERT_EQ(facts.GetCse().Find(loadQuery, CseHashQuery(loadQuery), facts.GetEffectEpoch()), nullptr);

    facts.GetCse().Clear();
    ASSERT_EQ(facts.GetCse().Find(pureQuery, CseHashQuery(pureQuery), facts.GetEffectEpoch()), nullptr);
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, CompileInfoFactsCseMergeIntersectsNodeIdentity)
{
    GraphFixture fixture;
    ValueVertex *receiver = fixture.NewInitialValue();
    ValueVertex *lhs = fixture.Int32(5);
    ValueVertex *rhs = fixture.Int32(6);
    std::array<ValueVertex *, 1> loadInputs {receiver};
    std::array<ValueVertex *, 2> addInputs {lhs, rhs};
    auto loadQuery = MakeCseQuery<LoadTaggedFieldVertex>(loadInputs, 16);
    auto pureQuery = MakeCseQuery<I32AddVertex>(addInputs);
    auto *pure = fixture.NewAdd(lhs, rhs);

    CompileInfoFacts facts(&fixture.chunk);
    facts.GetCse().AddExpression(pureQuery, pure, CseHashQuery(pureQuery), facts.GetEffectEpoch());
    CompileInfoFacts *left = facts.Clone();
    CompileInfoFacts *right = facts.Clone();
    left->GetCse().AddExpression(loadQuery, fixture.NewFieldLoad(receiver, 16), CseHashQuery(loadQuery),
                                 left->GetEffectEpoch());
    right->GetCse().AddExpression(loadQuery, fixture.NewFieldLoad(receiver, 16), CseHashQuery(loadQuery),
                                  right->GetEffectEpoch());
    left->Merge(*right);
    ASSERT_EQ(left->GetCse().Find(loadQuery, CseHashQuery(loadQuery), left->GetEffectEpoch()), nullptr);
    ASSERT_EQ(left->GetCse().Find(pureQuery, CseHashQuery(pureQuery), left->GetEffectEpoch()), pure);

    CompileInfoFacts *withWrite = facts.Clone();
    withWrite->IncrementEffectEpoch();
    facts.Merge(*withWrite);
    ASSERT_EQ(facts.GetCse().Find(pureQuery, CseHashQuery(pureQuery), facts.GetEffectEpoch()), pure);
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

// --- CompileInfoFacts: external module locations ---------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, ModuleLocationKeysSurviveCall)
{
    GraphFixture fixture;
    ValueVertex *function = fixture.NewInitialValue();
    ValueVertex *otherFunction = fixture.NewInitialValue();
    ValueVertex *environment = fixture.NewInitialValue();
    ValueVertex *dictionary = fixture.NewInitialValue();
    ValueVertex *otherDictionary = fixture.NewInitialValue();
    ValueVertex *index = fixture.Int32(3);
    CompileInfoFacts facts(&fixture.chunk);
    facts.RecordLoadedConstantProperty(function, PropertyKey::ModuleEnvironment(), environment);
    facts.RecordLoadedConstantProperty(function, PropertyKey::ExternalModuleDictionary(0), dictionary);
    facts.RecordLoadedConstantProperty(function, PropertyKey::ExternalModuleIndex(0), index);
    facts.RecordLoadedConstantProperty(function, PropertyKey::ExternalModuleDictionary(1), otherDictionary);
    facts.RecordLoadedProperty(dictionary, PropertyKey::Index(3), fixture.Tagged(JSTaggedValue(7)));

    SideEffectDescriptor call;
    call.kind = SideEffectKind::UNKNOWN_CALL;
    facts.MarkPossibleSideEffect(call);

    EXPECT_EQ(facts.LookupLoadedConstantProperty(function, PropertyKey::ModuleEnvironment()), environment);
    EXPECT_EQ(facts.LookupLoadedConstantProperty(function, PropertyKey::ExternalModuleDictionary(0)), dictionary);
    EXPECT_EQ(facts.LookupLoadedConstantProperty(function, PropertyKey::ExternalModuleIndex(0)), index);
    EXPECT_EQ(facts.LookupLoadedConstantProperty(function, PropertyKey::ExternalModuleDictionary(1)), otherDictionary);
    EXPECT_EQ(facts.LookupLoadedConstantProperty(function, PropertyKey::ExternalModuleIndex(1)), nullptr);
    EXPECT_EQ(facts.LookupLoadedConstantProperty(otherFunction, PropertyKey::ModuleEnvironment()), nullptr);
    EXPECT_EQ(facts.LookupLoadedConstantProperty(otherFunction, PropertyKey::ExternalModuleDictionary(0)), nullptr);
    EXPECT_EQ(facts.LookupLoadedConstantProperty(otherFunction, PropertyKey::ExternalModuleIndex(0)), nullptr);
    EXPECT_EQ(facts.LookupLoadedProperty(dictionary, PropertyKey::Index(3)), nullptr);
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, ModuleLocationMergeRequiresBothPaths)
{
    GraphFixture fixture;
    ValueVertex *function = fixture.NewInitialValue();
    ValueVertex *environment = fixture.NewInitialValue();
    ValueVertex *dictionary = fixture.NewInitialValue();
    ValueVertex *index = fixture.Int32(3);
    CompileInfoFacts entry(&fixture.chunk);
    entry.RecordLoadedConstantProperty(function, PropertyKey::ModuleEnvironment(), environment);
    CompileInfoFacts *taken = entry.Clone();
    CompileInfoFacts *notTaken = entry.Clone();
    taken->RecordLoadedConstantProperty(function, PropertyKey::ExternalModuleDictionary(0), dictionary);
    taken->RecordLoadedConstantProperty(function, PropertyKey::ExternalModuleIndex(0), index);

    taken->Merge(*notTaken);

    EXPECT_EQ(taken->LookupLoadedConstantProperty(function, PropertyKey::ModuleEnvironment()), environment);
    EXPECT_EQ(taken->LookupLoadedConstantProperty(function, PropertyKey::ExternalModuleDictionary(0)), nullptr);
    EXPECT_EQ(taken->LookupLoadedConstantProperty(function, PropertyKey::ExternalModuleIndex(0)), nullptr);
    EXPECT_EQ(entry.LookupLoadedConstantProperty(function, PropertyKey::ExternalModuleDictionary(0)), nullptr);
}

// Fixed result types are available without builder-side facts. Querying them
// must not create NodeInfo entries or infer a raw representation alternative.
HWTEST_F_L0(ArkSteedCompileInfoFactsTest, FixedResultTypesWithoutDynamicFacts)
{
    GraphFixture fixture;
    ValueVertex *i32 = fixture.Int32(65);
    ValueVertex *f64 = fixture.graph.GetFloat64Constant(1.25);
    ValueVertex *tagged = fixture.NewInitialValue();
    auto i32Inputs = fixture.Inputs({i32});
    auto f64Inputs = fixture.Inputs({f64});
    auto taggedInputs = fixture.Inputs({tagged});
    ValueVertex *checkedString =
        Vertex::New<CheckedTaggedStringVertex>(&fixture.chunk, taggedInputs, &fixture.chunk, 0);
    ValueVertex *actualArgc = Vertex::New<ActualArgcVertex>(&fixture.chunk, 0);

    struct TypedResult {
        ValueVertex *node;
        NodeType type;
    };
    const std::array<TypedResult, 14> results {{
        {Vertex::New<I32ToTaggedIntVertex>(&fixture.chunk, i32Inputs), NodeType::INT},
        {Vertex::New<CheckedNonNegativeI32ToTaggedIntVertex>(&fixture.chunk, i32Inputs, &fixture.chunk, 0),
         NodeType::INT},
        {Vertex::New<F64ToTaggedDoubleVertex>(&fixture.chunk, f64Inputs), NodeType::DOUBLE},
        {Vertex::New<I32ConditionCheckVertex>(&fixture.chunk, fixture.Inputs({i32, i32}), Condition::EQUAL),
         NodeType::BOOLEAN},
        {Vertex::New<F64ConditionCheckVertex>(&fixture.chunk, fixture.Inputs({f64, f64}), Condition::EQUAL),
         NodeType::BOOLEAN},
        {Vertex::New<TaggedEqualVertex>(&fixture.chunk, fixture.Inputs({tagged, tagged})), NodeType::BOOLEAN},
        {Vertex::New<TaggedNotEqualVertex>(&fixture.chunk, fixture.Inputs({tagged, tagged})), NodeType::BOOLEAN},
        {Vertex::New<StringEqualVertex>(&fixture.chunk, fixture.Inputs({checkedString, checkedString, tagged})),
         NodeType::BOOLEAN},
        {Vertex::New<Float64ToBooleanVertex>(&fixture.chunk, f64Inputs, true), NodeType::BOOLEAN},
        {Vertex::New<TaggedNumberToBooleanVertex>(&fixture.chunk, taggedInputs, &fixture.chunk, 0, true),
         NodeType::BOOLEAN},
        {Vertex::New<TaggedDoubleToBooleanVertex>(&fixture.chunk, taggedInputs, &fixture.chunk, 0, false),
         NodeType::BOOLEAN},
        {checkedString, NodeType::STRING},
        {Vertex::New<LoadSingleCharTableElementVertex>(&fixture.chunk, i32Inputs), NodeType::STRING},
        {Vertex::New<I32ToTaggedIntVertex>(&fixture.chunk, fixture.Inputs({actualArgc})), NodeType::INT},
    }};

    CompileInfoFacts facts(&fixture.chunk);
    const CompileInfoFacts &readOnlyFacts = facts;
    for (const auto &result : results) {
        EXPECT_TRUE(readOnlyFacts.TryGetInfoFor(result.node) == nullptr);
        EXPECT_EQ(readOnlyFacts.GetKnownType(result.node), result.type);
        EXPECT_TRUE(readOnlyFacts.CheckType(result.node, result.type));
        EXPECT_EQ(readOnlyFacts.TryGetAlternative(result.node, AlternativeNodes::Kind::INT32), nullptr);
        EXPECT_EQ(readOnlyFacts.TryGetAlternative(result.node, AlternativeNodes::Kind::HOLEY_FLOAT64), nullptr);
        EXPECT_TRUE(readOnlyFacts.TryGetInfoFor(result.node) == nullptr);
    }
    EXPECT_TRUE(facts.Empty());

    // A broad dynamic NUMBER fact retains the more precise encoding type.
    // Conflicting dynamic facts produce an empty type, never a usable proof.
    for (const auto &result : results) {
        NodeType dynamicType =
            result.type == NodeType::INT || result.type == NodeType::DOUBLE ? NodeType::NUMBER : NodeType::UNKNOWN;
        facts.EnsureType(result.node, dynamicType);
        EXPECT_EQ(facts.GetKnownType(result.node), result.type);
        EXPECT_TRUE(facts.CheckType(result.node, dynamicType));
        EXPECT_TRUE(facts.TryGetInfoFor(result.node)->GetAlternatives().Empty());
        NodeType conflictingType = result.type == NodeType::STRING ? NodeType::BOOLEAN : NodeType::STRING;
        facts.EnsureType(result.node, conflictingType);
        EXPECT_EQ(facts.GetKnownType(result.node), NodeType::NONE);
        EXPECT_FALSE(facts.CheckType(result.node, result.type));
    }
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, GenericTaggedResultsDoNotProveInt32)
{
    GraphFixture fixture;
    ValueVertex *u32 = Vertex::New<U32ToTaggedNumberVertex>(&fixture.chunk, fixture.Inputs({fixture.Int32(-1)}));
    ValueVertex *raw =
        Vertex::New<I64ToTaggedIntVertex>(&fixture.chunk, fixture.Inputs({fixture.graph.GetInt64Constant(0)}));
    CompileInfoFacts facts(&fixture.chunk);

    EXPECT_EQ(facts.GetKnownType(u32), NodeType::NUMBER);
    EXPECT_TRUE(facts.CheckType(u32, NodeType::NUMBER));
    EXPECT_FALSE(facts.CheckType(u32, NodeType::INT));
    EXPECT_FALSE(facts.CheckType(u32, NodeType::DOUBLE));
    EXPECT_EQ(facts.GetKnownType(raw), NodeType::UNKNOWN);
    EXPECT_FALSE(facts.CheckType(raw, NodeType::INT));
    EXPECT_FALSE(facts.CheckType(raw, NodeType::NUMBER));
    EXPECT_EQ(facts.TryGetAlternative(u32, AlternativeNodes::Kind::INT32), nullptr);
    EXPECT_EQ(facts.TryGetAlternative(raw, AlternativeNodes::Kind::INT32), nullptr);
    EXPECT_TRUE(facts.Empty());
}

// --- CompileInfoFacts: clone variants --------------------------------------

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, CompileInfoFactsCloneVariants)
{
    GraphFixture fixture;
    ValueVertex *node = fixture.Int32(7);
    ValueVertex *withoutInfo = fixture.Int32(8);
    ValueVertex *receiver = fixture.Int32(9);
    ValueVertex *value = fixture.Int32(10);
    ValueVertex *env = fixture.NewInitialValue();
    std::array<ValueVertex *, 1> readInputs {env};
    std::array<ValueVertex *, 2> pureInputs {receiver, value};
    auto readQuery = MakeCseQuery<LoadTaggedFieldVertex>(readInputs, 16);
    auto pureQuery = MakeCseQuery<I32AddVertex>(pureInputs);
    auto *read = fixture.NewFieldLoad(env, 16);
    auto *pure = fixture.NewAdd(receiver, value);

    CompileInfoFacts facts(&fixture.chunk);
    facts.UpdateInfoFor(node, [](NodeInfo &info) { info.SetType(NodeType::INT); });
    facts.UpdateInfoFor(withoutInfo, [](NodeInfo &) {});
    facts.RecordNonHole(node);
    facts.RecordLoadedProperty(receiver, PropertyKey::Named(1), value);
    facts.RecordLoadedConstantProperty(receiver, PropertyKey::Named(2), value);
    facts.RecordEnvSlot(env, 5, value);
    facts.RecordEnvConstant(env, 6, value);
    facts.GetCse().AddExpression(readQuery, read, CseHashQuery(readQuery), facts.GetEffectEpoch());
    facts.GetCse().AddExpression(pureQuery, pure, CseHashQuery(pureQuery), facts.GetEffectEpoch());
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
    ASSERT_TRUE(clone->TryGetInfoFor(withoutInfo) != nullptr);
    ASSERT_EQ(clone->GetCse().Find(readQuery, CseHashQuery(readQuery), clone->GetEffectEpoch()), read);
    clone->UpdateInfoFor(node, [](NodeInfo &info) { info.SetType(NodeType::NONE); });
    ASSERT_EQ(facts.GetKnownType(node), NodeType::INT) << "the copy must be independent of the original";
    ASSERT_TRUE(NodeInfo::IsEmptyNodeType(clone->GetKnownType(node)));

    // Loop-header clone: information-free entries and epoch-dependent facts are
    // dropped, constants are kept, the epoch advances and aliasing is reset.
    CompileInfoFacts *header = facts.CloneForLoopHeader();
    ASSERT_NE(header, nullptr);
    ASSERT_EQ(header->GetKnownType(node), NodeType::INT);
    ASSERT_TRUE(header->IsKnownNonHole(node));
    ASSERT_TRUE(header->TryGetInfoFor(node) != nullptr);
    ASSERT_TRUE(header->TryGetInfoFor(withoutInfo) == nullptr) << "entries without information are dropped";
    ASSERT_TRUE(facts.TryGetInfoFor(withoutInfo) != nullptr);
    ASSERT_EQ(header->LookupLoadedProperty(receiver, PropertyKey::Named(1)), nullptr);
    ASSERT_EQ(header->LookupLoadedConstantProperty(receiver, PropertyKey::Named(2)), value);
    ASSERT_EQ(header->LookupEnvSlot(env, 5), nullptr);
    ASSERT_EQ(header->LookupEnvConstant(env, 6), value);
    ASSERT_GT(header->GetEffectEpoch(), facts.GetEffectEpoch());
    ASSERT_EQ(header->GetEnvSlotAliasMode(), EnvSlotAliasMode::NONE);
    ASSERT_EQ(header->GetCse().Find(readQuery, CseHashQuery(readQuery), header->GetEffectEpoch()), nullptr);
    ASSERT_EQ(header->GetCse().Find(pureQuery, CseHashQuery(pureQuery), header->GetEffectEpoch()), pure);

    // Optimistic loop-header clone without effects: ordinary loaded facts stay.
    LoopEffects noEffects(&fixture.chunk);
    CompileInfoFacts *optimistic = facts.CloneForOptimisticLoopHeader(noEffects);
    ASSERT_NE(optimistic, nullptr);
    ASSERT_EQ(optimistic->GetKnownType(node), NodeType::INT);
    ASSERT_TRUE(optimistic->TryGetInfoFor(withoutInfo) != nullptr);
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
    ASSERT_TRUE(clearedClone->TryGetInfoFor(node) != nullptr);
    ASSERT_TRUE(clearedClone->TryGetInfoFor(withoutInfo) == nullptr);
    ASSERT_EQ(clearedClone->LookupLoadedProperty(receiver, PropertyKey::Named(1)), nullptr);
    ASSERT_EQ(clearedClone->LookupEnvSlot(env, 5), nullptr);
    ASSERT_EQ(clearedClone->LookupLoadedConstantProperty(receiver, PropertyKey::Named(2)), value);
    ASSERT_EQ(clearedClone->LookupEnvConstant(env, 6), value);
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, InlinedPropertyCachesDistinguishConstantPoolScopes)
{
    GraphFixture fixture;
    ValueVertex *receiver = fixture.NewInitialValue();
    ValueVertex *callerValue = fixture.Int32(7);
    ValueVertex *calleeValue = fixture.Int32(9);
    PropertyKey callerKey = PropertyKey::ConstDataId(7, 100);
    PropertyKey calleeKey = PropertyKey::ConstDataId(7, 200);
    CompileInfoFacts caller(&fixture.chunk);
    caller.RecordLoadedProperty(receiver, callerKey, callerValue);
    caller.RecordLoadedConstantProperty(receiver, callerKey, callerValue);

    // An inlined callee receives the same receiver and a clone of the caller facts.
    CompileInfoFacts *callee = caller.Clone();
    EXPECT_EQ(callee->LookupLoadedProperty(receiver, calleeKey), nullptr);
    EXPECT_EQ(callee->LookupLoadedConstantProperty(receiver, calleeKey), nullptr);
    callee->RecordLoadedProperty(receiver, calleeKey, calleeValue);
    callee->RecordLoadedConstantProperty(receiver, calleeKey, calleeValue);

    CompileInfoFacts *returned = callee->Clone();
    EXPECT_EQ(returned->LookupLoadedProperty(receiver, callerKey), callerValue);
    EXPECT_EQ(returned->LookupLoadedProperty(receiver, calleeKey), calleeValue);
    EXPECT_EQ(returned->LookupLoadedConstantProperty(receiver, callerKey), callerValue);
    EXPECT_EQ(returned->LookupLoadedConstantProperty(receiver, calleeKey), calleeValue);
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, InlinedPropertyWritesInvalidateOtherConstantPoolScopes)
{
    GraphFixture fixture;
    ValueVertex *receiver = fixture.NewInitialValue();
    ValueVertex *value = fixture.Int32(7);
    PropertyKey callerKey = PropertyKey::ConstDataId(7, 100);
    PropertyKey writtenKey = PropertyKey::ConstDataId(9, 200);
    PropertyKey otherCalleeKey = PropertyKey::ConstDataId(8, 200);
    CompileInfoFacts facts(&fixture.chunk);
    facts.RecordLoadedProperty(receiver, callerKey, value);
    facts.RecordLoadedProperty(receiver, writtenKey, value);
    facts.RecordLoadedProperty(receiver, otherCalleeKey, value);

    // Different indices in different pools can still identify the same property name.
    facts.ClearLoadedPropertiesForKey(writtenKey);
    EXPECT_EQ(facts.LookupLoadedProperty(receiver, callerKey), nullptr);
    EXPECT_EQ(facts.LookupLoadedProperty(receiver, writtenKey), nullptr);
    EXPECT_EQ(facts.LookupLoadedProperty(receiver, otherCalleeKey), value);

    facts.RecordLoadedProperty(receiver, callerKey, value);
    facts.ClearLoadedPropertiesForKey(callerKey);
    EXPECT_EQ(facts.LookupLoadedProperty(receiver, otherCalleeKey), nullptr);
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, LoopPropertyWritesInvalidateOtherConstantPoolScopes)
{
    GraphFixture fixture;
    ValueVertex *receiver = fixture.NewInitialValue();
    ValueVertex *value = fixture.Int32(7);
    PropertyKey callerKey = PropertyKey::ConstDataId(7, 100);
    PropertyKey writtenKey = PropertyKey::ConstDataId(9, 200);
    PropertyKey otherCalleeKey = PropertyKey::ConstDataId(8, 200);
    LoopEffects effects(&fixture.chunk);
    effects.RecordPropertyWrite(writtenKey);
    EXPECT_TRUE(effects.InvalidatesPropertyKey(callerKey));
    EXPECT_TRUE(effects.InvalidatesPropertyKey(writtenKey));
    EXPECT_FALSE(effects.InvalidatesPropertyKey(otherCalleeKey));

    // The caller's cached read need not have existed while the loop summary was recorded.
    CompileInfoFacts facts(&fixture.chunk);
    facts.RecordLoadedProperty(receiver, callerKey, value);
    facts.RecordLoadedProperty(receiver, otherCalleeKey, value);
    CompileInfoFacts *header = facts.CloneForOptimisticLoopHeader(effects);
    EXPECT_EQ(header->LookupLoadedProperty(receiver, callerKey), nullptr);
    EXPECT_EQ(header->LookupLoadedProperty(receiver, otherCalleeKey), value);
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
    facts.UpdateInfoFor(intNode, [](NodeInfo &info) { info.SetType(NodeType::INT); });
    facts.UpdateInfoFor(doubleNode, [](NodeInfo &info) { info.SetType(NodeType::DOUBLE); });
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
    narrowedHeader->UpdateInfoFor(doubleNode, [](NodeInfo &info) { info.SetType(NodeType::INT); });
    ASSERT_FALSE(body->IsCompatibleWithLoopHeader(*narrowedHeader, headerBlock));

    // A header entry the body does not have is incompatible.
    CompileInfoFacts *extendedHeader = facts.CloneForLoopHeader();
    ValueVertex *extra = fixture.Int32(6);
    extendedHeader->UpdateInfoFor(extra, [](NodeInfo &info) { info.SetType(NodeType::INT); });
    ASSERT_FALSE(body->IsCompatibleWithLoopHeader(*extendedHeader, headerBlock));

    // Phi vertices owned by the loop header are skipped: their value is
    // reconstructed at the back edge and cannot be compared there.
    CompileInfoFacts *headerWithPhi = facts.CloneForLoopHeader();
    PhiVertex *phi = Vertex::New<PhiVertex>(&fixture.chunk, 1, VirtualRegister(0));
    phi->SetInput(0, intNode);
    phi->SetOwner(headerBlock);
    headerWithPhi->UpdateInfoFor(phi, [](NodeInfo &info) { info.SetType(NodeType::INT); });
    ASSERT_TRUE(body->IsCompatibleWithLoopHeader(*headerWithPhi, headerBlock));
    BB *otherBlock = BB::New(&fixture.chunk);
    otherBlock->SetId(8);
    ASSERT_FALSE(body->IsCompatibleWithLoopHeader(*headerWithPhi, otherBlock));

    // Equal epochs compare types only (subset relation), never HClasses.
    CompileInfoFacts *sameEpochHeader = facts.Clone();
    CompileInfoFacts *sameEpochBody = facts.Clone();
    sameEpochHeader->UpdateInfoFor(doubleNode, [](NodeInfo &info) { info.SetType(NodeType::INT); });
    ASSERT_FALSE(sameEpochBody->IsCompatibleWithLoopHeader(*sameEpochHeader, headerBlock));
    sameEpochBody->UpdateInfoFor(doubleNode, [](NodeInfo &info) { info.SetType(NodeType::INT); });
    sameEpochHeader->UpdateInfoFor(doubleNode, [](NodeInfo &info) { info.SetType(NodeType::NUMBER); });
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

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, HamtSnapshotsCollisionsAndIntersection)
{
    GraphFixture fixture;
    using Map = FactsHamt<HamtTestKey, int *, HamtTestHash, HamtTestEqual>;
    Map original(&fixture.chunk);
    Map left(&fixture.chunk);
    Map right(&fixture.chunk);
    std::array<int, 66> values {};
    auto key = [](uint32_t id) { return HamtTestKey {static_cast<uint64_t>(id % 32U) << 40U, id}; };
    for (uint32_t i = 0; i < 64U; ++i) {
        values[i] = static_cast<int>(i);
        original.InsertOrAssign(key(i), &values[i]);
    }
    left.CopyFrom(original);
    right.CopyFrom(original);

    // Mutating the original and both clones must not affect any other version.
    original.InsertOrAssign(key(0), &values[64]);
    left.Erase(key(1));
    right.InsertOrAssign(key(2), &values[65]);
    left.InsertOrAssign(key(64), &values[64]);
    right.InsertOrAssign(key(65), &values[65]);
    EXPECT_EQ(left.Find(key(0)), &values[0]);
    EXPECT_EQ(right.Find(key(0)), &values[0]);
    EXPECT_EQ(original.Find(key(1)), &values[1]);
    EXPECT_EQ(left.Find(key(2)), &values[2]);
    EXPECT_EQ(original.Find(key(64)), nullptr);

    left.IntersectWith(
        right, [](const HamtTestKey &, int *a, int *b) { return a == b ? a : nullptr; },
        [](const auto &) { return true; });
    for (uint32_t i = 0; i < 64U; ++i) {
        EXPECT_EQ(left.Find(key(i)), (i == 1U || i == 2U) ? nullptr : &values[i]) << "key " << i;
        EXPECT_EQ(original.Find(key(i)), i == 0U ? &values[64] : &values[i]) << "original key " << i;
    }
    EXPECT_EQ(left.Find(key(64)), nullptr);
    EXPECT_EQ(left.Find(key(65)), nullptr);
    EXPECT_EQ(right.Find(key(2)), &values[65]);

    left.Transform([](const HamtTestKey &entry, int *value) { return entry.id % 2U == 0 ? nullptr : value; });
    EXPECT_EQ(left.Find(key(4)), nullptr);
    EXPECT_EQ(left.Find(key(5)), &values[5]);
    EXPECT_EQ(right.Find(key(4)), &values[4]);
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, HamtMatchesReferenceMapAcrossEditsAndForks)
{
    GraphFixture fixture;
    using Map = FactsHamt<HamtTestKey, int *, HamtTestHash, HamtTestEqual>;
    Map actual(&fixture.chunk);
    std::map<uint32_t, int *> expected;
    std::array<int, 256> values {};
    auto key = [](uint32_t id) { return HamtTestKey {MixFactsHash(id % 17U), id}; };
    uint32_t state = 0x12345678U;
    for (uint32_t step = 0; step < 1024U; ++step) {
        state = state * 1664525U + 1013904223U;
        uint32_t id = (state >> 8U) % 256U;
        if ((state & 3U) == 0) {
            actual.Erase(key(id));
            expected.erase(id);
        } else {
            actual.InsertOrAssign(key(id), &values[id]);
            expected[id] = &values[id];
        }
        if (step % 41U != 0) {
            continue;
        }
        Map snapshot(&fixture.chunk);
        snapshot.CopyFrom(actual);
        snapshot.Erase(key(id));
        for (uint32_t candidate = 0; candidate < values.size(); ++candidate) {
            auto it = expected.find(candidate);
            int *expectedValue = it == expected.end() ? nullptr : it->second;
            EXPECT_EQ(actual.Find(key(candidate)), expectedValue) << "step " << step << " key " << candidate;
            EXPECT_EQ(snapshot.Find(key(candidate)), candidate == id ? nullptr : expectedValue)
                << "snapshot step " << step << " key " << candidate;
        }
    }
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, HamtUpdateUsesCurrentValueAndKeepsSnapshotsIsolated)
{
    GraphFixture fixture;
    using Map = FactsHamt<HamtTestKey, int *, HamtTestHash, HamtTestEqual>;
    Map original(&fixture.chunk);
    Map snapshot(&fixture.chunk);
    std::array<int, 4> values {};
    const HamtTestKey first {42, 1};
    const HamtTestKey collision {42, 2};
    const HamtTestKey absent {42, 3};
    original.InsertOrAssign(first, &values[0]);
    original.InsertOrAssign(collision, &values[1]);
    snapshot.CopyFrom(original);

    uint32_t calls = 0;
    original.Update(first, [&](int *previous) {
        EXPECT_EQ(previous, &values[0]);
        ++calls;
        return &values[2];
    });
    original.Update(first, [&](int *previous) {
        EXPECT_EQ(previous, &values[2]);
        ++calls;
        return previous;
    });
    original.Update(absent, [&](int *previous) {
        EXPECT_EQ(previous, nullptr);
        ++calls;
        return &values[3];
    });
    EXPECT_EQ(calls, 3U);
    EXPECT_EQ(original.Find(first), &values[2]);
    EXPECT_EQ(original.Find(collision), &values[1]);
    EXPECT_EQ(original.Find(absent), &values[3]);
    EXPECT_EQ(snapshot.Find(first), &values[0]);
    EXPECT_EQ(snapshot.Find(collision), &values[1]);
    EXPECT_EQ(snapshot.Find(absent), nullptr);

    // Missing keys at the leaf, hash, and branch levels leave both versions intact.
    original.Erase(HamtTestKey {42, 4});
    original.Erase(HamtTestKey {43, 4});
    snapshot.Erase(HamtTestKey {44, 4});
    original.Erase(first);
    EXPECT_EQ(original.Find(first), nullptr);
    EXPECT_EQ(original.Find(collision), &values[1]);
    EXPECT_EQ(snapshot.Find(first), &values[0]);
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, HamtMetadataStaysExactAcrossInPlaceAndSharedEdits)
{
    struct Value {
        uint32_t epoch;
        bool empty;
    };
    struct Traits {
        static uint32_t MinEpoch(Value *value)
        {
            return value->epoch;
        }
        static bool HasEmptyInfo(Value *value)
        {
            return value->empty;
        }
    };
    GraphFixture fixture;
    using Map = FactsHamt<HamtTestKey, Value *, HamtTestHash, HamtTestEqual, Traits>;
    Map original(&fixture.chunk);
    Map snapshot(&fixture.chunk);
    Value fresh {20, false};
    Value stale {1, true};
    auto key = [](uint32_t id) { return HamtTestKey {static_cast<uint64_t>(id % 8U) << 20U, id}; };
    for (uint32_t id = 0; id < 16U; ++id) {
        original.InsertOrAssign(key(id), &fresh);
    }
    original.InsertOrAssign(key(0), &stale);
    snapshot.CopyFrom(original);
    original.InsertOrAssign(key(0), &fresh);
    // Replacing a child can mutate the same pointer; metadata must be captured first.
    original.InsertOrAssign(key(1), &stale);
    original.InsertOrAssign(key(1), &fresh);

    uint32_t visited = 0;
    original.TransformWithReuse(
        [&](const HamtTestKey &, Value *value) {
            ++visited;
            return value->epoch < 10U || value->empty ? nullptr : value;
        },
        [](const auto &metadata) { return metadata.minEpoch >= 10U && !metadata.hasEmptyInfo; });
    EXPECT_EQ(visited, 0U);
    snapshot.TransformWithReuse(
        [](const HamtTestKey &, Value *value) { return value->epoch < 10U || value->empty ? nullptr : value; },
        [](const auto &metadata) { return metadata.minEpoch >= 10U && !metadata.hasEmptyInfo; });
    EXPECT_EQ(snapshot.Find(key(0)), nullptr);
    for (uint32_t id = 0; id < 16U; ++id) {
        EXPECT_EQ(original.Find(key(id)), &fresh);
        if (id != 0) {
            EXPECT_EQ(snapshot.Find(key(id)), &fresh);
        }
    }
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, CseSnapshotMergeRetainsCandidateOrderAfterEpochFilter)
{
    GraphFixture fixture;
    ValueVertex *receiver = fixture.NewInitialValue();
    ValueVertex *lhs = fixture.Int32(3);
    ValueVertex *rhs = fixture.Int32(4);
    std::array<ValueVertex *, 1> loadInputs {receiver};
    std::array<ValueVertex *, 2> addInputs {lhs, rhs};
    auto loadQuery = MakeCseQuery<LoadTaggedFieldVertex>(loadInputs, 16);
    auto addQuery = MakeCseQuery<I32AddVertex>(addInputs);
    auto *oldAdd = fixture.NewAdd(lhs, rhs);
    auto *load = fixture.NewFieldLoad(receiver, 16);
    auto *newAdd = fixture.NewAdd(lhs, rhs);
    constexpr uint64_t hash = 42;

    CompileInfoFacts facts(&fixture.chunk);
    facts.GetCse().AddExpression(addQuery, oldAdd, hash, facts.GetEffectEpoch());
    facts.GetCse().AddExpression(loadQuery, load, hash, facts.GetEffectEpoch());
    facts.GetCse().AddExpression(addQuery, newAdd, hash, facts.GetEffectEpoch());
    CompileInfoFacts *fast = facts.Clone();
    CompileInfoFacts *slow = facts.Clone();
    slow->OnSideEffect();
    slow->Merge(*fast);

    EXPECT_EQ(slow->GetEffectEpoch(), 2U);
    EXPECT_EQ(slow->GetCse().Find(loadQuery, hash, slow->GetEffectEpoch()), nullptr);
    // Advancing the memory epoch preserves the independent pure candidate order.
    EXPECT_EQ(slow->GetCse().Find(addQuery, hash, slow->GetEffectEpoch()), newAdd);
    EXPECT_EQ(fast->GetCse().Find(addQuery, hash, fast->GetEffectEpoch()), newAdd);
    EXPECT_EQ(facts.GetCse().Find(addQuery, hash, facts.GetEffectEpoch()), newAdd);
}

HWTEST_F_L0(ArkSteedCompileInfoFactsTest, HClassGenerationStaysLocalAndMergesEffectiveFacts)
{
    ScopedEcmaVm scope;
    ASSERT_TRUE(scope.IsValid());
    WithHClassRefs(scope.GetVm(), scope.GetThread(), [](ArkSteedHClassRef hclass, ArkSteedHClassRef) {
        ASSERT_TRUE(hclass.IsJSHClass());
        GraphFixture fixture;
        ValueVertex *unstable = fixture.NewInitialValue();
        ValueVertex *stable = fixture.NewInitialValue();
        CompileInfoFacts facts(&fixture.chunk);
        facts.RecordHClass(unstable, hclass, false);
        facts.RecordHClass(stable, hclass, true);

        CompileInfoFacts *fast = facts.Clone();
        CompileInfoFacts *slow = facts.Clone();
        slow->OnSideEffect();
        EXPECT_TRUE(fast->TryGetHClass(unstable).Equals(hclass));
        EXPECT_FALSE(slow->TryGetHClass(unstable).IsJSHClass());
        EXPECT_TRUE(slow->TryGetHClass(stable).Equals(hclass));
        CompileInfoFacts *sharedSlow = slow->Clone();
        sharedSlow->Merge(*fast);
        EXPECT_TRUE(sharedSlow->PossibleHClassesAreStale(unstable));
        EXPECT_TRUE(sharedSlow->TryGetHClass(stable).Equals(hclass));
        // Updating a different field must preserve the lazily stale HClass.
        slow->EnsureType(unstable, NodeType::OTHER_JS_RECEIVER);
        EXPECT_TRUE(slow->PossibleHClassesAreStale(unstable));
        slow->RecordNonHole(unstable);
        EXPECT_TRUE(slow->PossibleHClassesAreStale(unstable));
        slow->Merge(*fast);
        EXPECT_TRUE(slow->PossibleHClassesAreStale(unstable));
        EXPECT_TRUE(slow->TryGetHClass(stable).Equals(hclass));
        EXPECT_TRUE(facts.TryGetHClass(unstable).Equals(hclass));

        // Independent calls get distinct generations; fresh facts on both paths
        // remain fresh after each path explicitly revalidates its HClass.
        CompileInfoFacts *left = facts.Clone();
        CompileInfoFacts *right = facts.Clone();
        left->OnSideEffect();
        right->OnSideEffect();
        left->MarkPossibleHClassesFresh(unstable);
        right->MarkPossibleHClassesFresh(unstable);
        left->Merge(*right);
        EXPECT_TRUE(left->TryGetHClass(unstable).Equals(hclass));
        EXPECT_FALSE(left->PossibleHClassesAreStale(unstable));
        EXPECT_TRUE(facts.TryGetHClass(unstable).Equals(hclass));
    });
}

}  // namespace panda::ecmascript::arksteed
