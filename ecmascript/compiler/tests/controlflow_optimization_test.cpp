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

#include "ecmascript/compiler/circuit_builder_helper.h"
#include "ecmascript/compiler/combined_pass_visitor.h"
#include "ecmascript/compiler/controlflow_optimization.h"
#include "ecmascript/compiler/verifier.h"
#include "ecmascript/tests/test_helper.h"
#include "ecmascript/compiler/circuit_builder-inl.h"

namespace panda::test {
class ControlflowOptimizationTests : public testing::Test {
};
using ecmascript::kungfu::Chunk;
using ecmascript::kungfu::Circuit;
using ecmascript::kungfu::CircuitBuilder;
using ecmascript::kungfu::ControlFlowOptimization;
using ecmascript::kungfu::Environment;
using ecmascript::kungfu::OpCode;
using ecmascript::kungfu::GateType;
using ecmascript::kungfu::MachineType;
using ecmascript::kungfu::GateAccessor;
using ecmascript::kungfu::GateRef;
using ecmascript::kungfu::Verifier;
using ecmascript::NativeAreaAllocator;
using ecmascript::kungfu::Label;
using ecmascript::kungfu::Variable;
using ecmascript::kungfu::VariableType;

HWTEST_F_L0(ControlflowOptimizationTests, ValueCheckNegOverflowElimination)
{
    NativeAreaAllocator allocator;
    Circuit circuit(&allocator);
    CircuitBuilder builder(&circuit);
    GateAccessor acc(&circuit);
    auto depend = acc.GetDependRoot();
    auto state = acc.GetStateRoot();
    auto arg0 = builder.Arguments(0);
    auto root = acc.GetCircuitRoot();
    auto constant = circuit.NewGate(circuit.Constant(0), {});
    auto negOverflow = circuit.NewGate(circuit.ValueCheckNegOverflow(), {state, depend, arg0, root});
    auto endReturn = circuit.NewGate(circuit.Return(), {negOverflow, negOverflow, constant, circuit.GetReturnRoot()});
    Chunk chunk(&allocator);
    ControlFlowOptimization cfo(&circuit, &chunk, false, "controlflow optimization");
    cfo.Run();
    EXPECT_TRUE(acc.IsNop(negOverflow));
    EXPECT_TRUE(acc.GetOpCode(state) == OpCode::STATE_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(depend) == OpCode::DEPEND_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(arg0) == OpCode::ARG);
    EXPECT_TRUE(acc.GetOpCode(endReturn) == OpCode::RETURN);
}

HWTEST_F_L0(ControlflowOptimizationTests, Int32UnsignedUpperBoundCheckElimination)
{
    NativeAreaAllocator allocator;
    Circuit circuit(&allocator);
    CircuitBuilder builder(&circuit);
    GateAccessor acc(&circuit);
    auto depend = acc.GetDependRoot();
    auto state = acc.GetStateRoot();
    auto arg0 = builder.Arguments(0);
    auto constant = circuit.NewGate(circuit.Constant(1), {});
    auto lsr = circuit.NewGate(circuit.Lsr(), {arg0, constant});
    auto root = acc.GetCircuitRoot();
    auto check = circuit.NewGate(circuit.Int32UnsignedUpperBoundCheck(), {state, depend, lsr, constant, root});
    auto endReturn = circuit.NewGate(circuit.Return(), {check, check, constant, circuit.GetReturnRoot()});
    Chunk chunk(&allocator);
    ControlFlowOptimization cfo(&circuit, &chunk, false, "controlflow optimization");
    cfo.Run();
    EXPECT_TRUE(acc.IsNop(check));
    EXPECT_TRUE(acc.IsNop(lsr));
    EXPECT_TRUE(acc.GetOpCode(state) == OpCode::STATE_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(depend) == OpCode::DEPEND_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(constant) == OpCode::CONSTANT);
    EXPECT_TRUE(acc.GetOpCode(arg0) == OpCode::ARG);
    EXPECT_TRUE(acc.GetOpCode(endReturn) == OpCode::RETURN);
}

HWTEST_F_L0(ControlflowOptimizationTests, Int32CheckRightIsZeroElimination)
{
    NativeAreaAllocator allocator;
    Circuit circuit(&allocator);
    CircuitBuilder builder(&circuit);
    GateAccessor acc(&circuit);
    auto depend = acc.GetDependRoot();
    auto state = acc.GetStateRoot();
    auto root = acc.GetCircuitRoot();
    auto arg0 = builder.Arguments(0);
    auto check = circuit.NewGate(circuit.Int32CheckRightIsZero(), {state, depend, arg0, root});
    auto constant = circuit.NewGate(circuit.Constant(0), {});
    auto endReturn = circuit.NewGate(circuit.Return(), {check, check, constant, circuit.GetReturnRoot()});
    Chunk chunk(&allocator);
    ControlFlowOptimization cfo(&circuit, &chunk, false, "controlflow optimization");
    cfo.Run();
    EXPECT_TRUE(acc.IsNop(check));
    EXPECT_TRUE(acc.GetOpCode(state) == OpCode::STATE_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(depend) == OpCode::DEPEND_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(arg0) == OpCode::ARG);
    EXPECT_TRUE(acc.GetOpCode(endReturn) == OpCode::RETURN);
}

HWTEST_F_L0(ControlflowOptimizationTests, RemainderIsNegativeZeroElimination)
{
    NativeAreaAllocator allocator;
    Circuit circuit(&allocator);
    CircuitBuilder builder(&circuit);
    GateAccessor acc(&circuit);
    auto depend = acc.GetDependRoot();
    auto state = acc.GetStateRoot();
    auto root = acc.GetCircuitRoot();
    auto arg0 = builder.Arguments(0);
    auto constant = circuit.NewGate(circuit.Constant(1), {});
    auto check = circuit.NewGate(circuit.RemainderIsNegativeZero(), {state, depend, arg0, constant, root});
    auto endReturn = circuit.NewGate(circuit.Return(), {check, check, constant, circuit.GetReturnRoot()});
    Chunk chunk(&allocator);
    ControlFlowOptimization cfo(&circuit, &chunk, false, "controlflow optimization");
    cfo.Run();
    EXPECT_TRUE(acc.IsNop(check));
    EXPECT_TRUE(acc.GetOpCode(state) == OpCode::STATE_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(depend) == OpCode::DEPEND_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(arg0) == OpCode::ARG);
    EXPECT_TRUE(acc.GetOpCode(endReturn) == OpCode::RETURN);
}

HWTEST_F_L0(ControlflowOptimizationTests, Int32DivWithCheckElimination)
{
    NativeAreaAllocator allocator;
    Circuit circuit(&allocator);
    CircuitBuilder builder(&circuit);
    GateAccessor acc(&circuit);
    auto depend = acc.GetDependRoot();
    auto state = acc.GetStateRoot();
    auto root = acc.GetCircuitRoot();
    auto arg0 = builder.Arguments(0);
    auto constant = circuit.NewGate(circuit.Constant(1), {});
    auto check = circuit.NewGate(circuit.Int32DivWithCheck(), {state, depend, arg0, constant, root});
    auto endReturn = circuit.NewGate(circuit.Return(), {check, check, constant, circuit.GetReturnRoot()});
    Chunk chunk(&allocator);
    ControlFlowOptimization cfo(&circuit, &chunk, false, "controlflow optimization");
    cfo.Run();
    EXPECT_TRUE(acc.IsNop(check));
    EXPECT_TRUE(acc.GetOpCode(state) == OpCode::STATE_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(depend) == OpCode::DEPEND_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(constant) == OpCode::CONSTANT);
    EXPECT_TRUE(acc.GetOpCode(arg0) == OpCode::ARG);
    EXPECT_TRUE(acc.GetOpCode(endReturn) == OpCode::RETURN);
}

HWTEST_F_L0(ControlflowOptimizationTests, OverflowCheckElimination)
{
    NativeAreaAllocator allocator;
    Circuit circuit(&allocator);
    CircuitBuilder builder(&circuit);
    GateAccessor acc(&circuit);
    auto depend = acc.GetDependRoot();
    auto state = acc.GetStateRoot();
    auto root = acc.GetCircuitRoot();
    auto arg0 = builder.Arguments(0);
    auto constant = circuit.NewGate(circuit.Constant(1), {});
    auto addWithOverflow = circuit.NewGate(circuit.AddWithOverflow(), {arg0, constant});
    auto check = circuit.NewGate(circuit.OverflowCheck(), {state, depend, addWithOverflow, root});
    auto endReturn = circuit.NewGate(circuit.Return(), {check, check, constant, circuit.GetReturnRoot()});
    Chunk chunk(&allocator);
    ControlFlowOptimization cfo(&circuit, &chunk, false, "controlflow optimization");
    cfo.Run();
    EXPECT_TRUE(acc.IsNop(check));
    EXPECT_TRUE(acc.IsNop(addWithOverflow));
    EXPECT_TRUE(acc.GetOpCode(state) == OpCode::STATE_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(depend) == OpCode::DEPEND_ENTRY);
    EXPECT_TRUE(acc.GetOpCode(constant) == OpCode::CONSTANT);
    EXPECT_TRUE(acc.GetOpCode(arg0) == OpCode::ARG);
    EXPECT_TRUE(acc.GetOpCode(endReturn) == OpCode::RETURN);
}

HWTEST_F_L0(ControlflowOptimizationTests, EmptyBranchElimination)
{
    NativeAreaAllocator allocator;
    Circuit circuit(&allocator);
    CircuitBuilder builder(&circuit);
    GateAccessor acc(&circuit);
    auto depend = acc.GetDependRoot();
    auto state = acc.GetStateRoot();
    auto arg0 = builder.Arguments(0);
    auto arg1 = builder.Arguments(1);
    auto arg2 = builder.Arguments(2);
    auto dead = circuit.DeadGate();
    auto icmp = circuit.NewGate(circuit.Icmp(1), MachineType::I1, {arg0, arg1}, GateType::NJSValue());
    auto ifBranch = circuit.NewGate(circuit.IfBranch(0), {state, icmp});
    auto ifTrue = circuit.NewGate(circuit.IfTrue(), {ifBranch});
    auto ifFalse = circuit.NewGate(circuit.IfFalse(), {ifBranch});
    auto merge = circuit.NewGate(circuit.Merge(2), {ifTrue, ifFalse});
    auto valueSelector = circuit.NewGate(circuit.ValueSelector(2), {merge, arg1, arg2});
    auto constant = circuit.NewGate(circuit.Constant(0), {});
    auto circuitReturn = circuit.NewGate(circuit.Return(), {merge, depend, constant, circuit.GetReturnRoot()});
    Chunk chunk(&allocator);
    ControlFlowOptimization cfo(&circuit, &chunk, false, "controlflow optimization");
    cfo.Run();
    EXPECT_TRUE(acc.IsNop(merge));
    EXPECT_TRUE(acc.IsNop(valueSelector));
    EXPECT_TRUE(acc.IsNop(ifTrue));
    EXPECT_TRUE(acc.IsNop(ifFalse));
    EXPECT_TRUE(acc.IsNop(ifBranch));
    EXPECT_TRUE(acc.GetOpCode(arg0) == OpCode::ARG);
    EXPECT_TRUE(acc.GetOpCode(arg1) == OpCode::ARG);
    EXPECT_TRUE(acc.GetOpCode(arg2) == OpCode::ARG);
    EXPECT_TRUE(acc.GetOpCode(circuitReturn) == OpCode::RETURN);
}


HWTEST_F_L0(ControlflowOptimizationTests, EmptyLoopElimination)
{
    // construct a circuit
    NativeAreaAllocator allocator;
    Circuit circuit(&allocator);
    Chunk chunk(&allocator);
    GateAccessor acc(&circuit);
    CircuitBuilder builder(&circuit);
    Environment env(0, &builder);
    builder.SetEnvironment(&env);
    auto loopLen = circuit.NewGate(circuit.Constant(10), {});

    DEFVALUE(index, (&builder), VariableType::INT32(), builder.Int32(0));

    Label loopHead(&env);
    Label loopBody(&env);
    Label loopExit(&env);
    builder.Jump(&loopHead);
    builder.LoopBegin(&loopHead);
    auto loopBegin = builder.GetState();
    auto loopEntry = acc.GetState(loopBegin);
    auto icmp = builder.Int32LessThan(*index, loopLen);
    builder.Branch(icmp, &loopBody, &loopExit);
    builder.Bind(&loopBody);
    auto ifTrue = builder.GetState();
    auto ifBranch = acc.GetState(ifTrue);
    index = builder.Int32Add(*index, builder.Int32(1));
    builder.LoopEnd(&loopHead);
    builder.Bind(&loopExit);
    auto ifFalse = builder.GetState();
    builder.Return();
    EXPECT_TRUE(Verifier::Run(&circuit));
    ControlFlowOptimization cfo(&circuit, &chunk, false, "controlflow optimization");
    cfo.Run();
    EXPECT_TRUE(acc.IsNop(loopBegin));
    EXPECT_TRUE(acc.IsNop(ifBranch));
    EXPECT_TRUE(acc.IsNop(ifTrue));
    EXPECT_TRUE(acc.IsNop(ifFalse));
    EXPECT_TRUE(Verifier::Run(&circuit));
}

}