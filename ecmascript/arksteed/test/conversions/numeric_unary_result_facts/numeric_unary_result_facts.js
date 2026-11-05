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

//! PARAMS --compiler-jit-hotness-threshold=60000

// Each unary result must retain its own raw result rather than its input.
// Fractional warmup values keep INC and DEC on the floating-point path.
//! METHOD afterIncrement
//! HAS F64Add
//! COUNT F64Mul 2
//! COUNT CheckedNumberToF64 1
//! HAS_NOT TaggedNumberToF64
//! HAS_NOT TaggedDoubleToF64
function afterIncrement(value) {
    let result = value * 0.5;
    ++result;
    return result * 0.5;
}

//! METHOD afterDecrement
//! HAS F64Sub
//! COUNT F64Mul 2
//! COUNT CheckedNumberToF64 1
//! HAS_NOT TaggedNumberToF64
//! HAS_NOT TaggedDoubleToF64
function afterDecrement(value) {
    let result = value * 0.5;
    --result;
    return result * 0.5;
}

//! METHOD chainedUnaryResults
//! HAS F64Neg
//! HAS F64Add
//! HAS F64Sub
//! COUNT F64Mul 2
//! COUNT CheckedNumberToF64 1
//! HAS_NOT TaggedNumberToF64
//! HAS_NOT TaggedDoubleToF64
function chainedUnaryResults(value) {
    let result = value * 0.5;
    result = -result;
    ++result;
    --result;
    return result * 0.5;
}

// The original and incremented value stay live, and the new result has two
// consumers. Reusing the input alternative changes the returned value.
//! METHOD originalAndIncremented
//! HAS F64Add
//! COUNT F64Mul 4
//! COUNT CheckedNumberToF64 1
//! HAS_NOT TaggedNumberToF64
//! HAS_NOT TaggedDoubleToF64
function originalAndIncremented(value) {
    const original = value * 0.5;
    let incremented = original;
    ++incremented;
    return original * 0.5 + incremented * 0.25 + incremented * 0.5;
}

for (let iteration = 0; iteration < 2000; ++iteration) {
    afterIncrement(2.5);
    afterDecrement(2.5);
    chainedUnaryResults(2.5);
    originalAndIncremented(2.5);
}
ArkTools.arkSteedCompileSync(afterIncrement);
ArkTools.arkSteedCompileSync(afterDecrement);
ArkTools.arkSteedCompileSync(chainedUnaryResults);
ArkTools.arkSteedCompileSync(originalAndIncremented);

print(ArkTools.arkSteedIsCompiled(afterIncrement));
print(ArkTools.arkSteedIsCompiled(afterDecrement));
print(ArkTools.arkSteedIsCompiled(chainedUnaryResults));
print(ArkTools.arkSteedIsCompiled(originalAndIncremented));
print(afterIncrement(2.5));
print(afterDecrement(2.5));
print(chainedUnaryResults(2.5));
print(originalAndIncremented(2.5));

print(afterIncrement(0) === 0.5);
print(afterIncrement(-0) === 0.5);
print(afterDecrement(0) === -0.5);
print(afterDecrement(-0) === -0.5);
print(Object.is(chainedUnaryResults(0), 0));
print(Object.is(chainedUnaryResults(-0), 0));
print(afterIncrement(Infinity) === Infinity);
print(afterIncrement(-Infinity) === -Infinity);
print(afterDecrement(Infinity) === Infinity);
print(afterDecrement(-Infinity) === -Infinity);
print(chainedUnaryResults(Infinity) === -Infinity);
print(chainedUnaryResults(-Infinity) === Infinity);
print(Number.isNaN(afterIncrement(NaN)));
print(Number.isNaN(afterDecrement(NaN)));
print(Number.isNaN(chainedUnaryResults(NaN)));
print(originalAndIncremented(-2.5) === -0.8125);

// Non-number inputs fail the initial numeric guard and resume JS conversion.
// Run these after the numeric cases because eager deopt may invalidate code.
print(afterIncrement("2.5"));
print(afterDecrement("2.5"));
print(chainedUnaryResults("2.5"));
print(originalAndIncremented("2.5"));
