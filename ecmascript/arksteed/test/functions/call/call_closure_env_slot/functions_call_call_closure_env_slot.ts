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

// G11.6 (P0) calls and closures: JIT member of the independent constant pair.
// Its interpreter twin `functions/call/call_closure_env_slot_interp_twin`
// executes the same semantic program with `--compiler-enable-jit=false` and
// prints the byte-identical expectation; this member additionally installs the
// exercised methods and carries the JIT evidence annotations.
//
// Oracle (ECMA-262 edition 12 (2021): 10.2.1.2 OrdinaryCallBindThis, 10.2.2
// [[Construct]], 10.2.10 FunctionDeclarationInstantiation, 13.3.6.2
// EvaluateCall, 13.3.8.1 ArgumentListEvaluation): every printed line is the
// hand-computed result of the specified computation, not tier state.
// The captured binding starts at 10; the reader observes 10; the writer with
// the omitted delta selects the default 1 and reaches 11; an explicit
// `undefined` argument also selects the default and reaches 12; a supplied `0`
// keeps 17 instead of selecting the default; a rest call with the supplied
// deltas 2 and 3 reaches 17 and the following read observes the persisted 17;
// an empty rest call keeps 17; a separately created environment initialized to
// 100 still reads 100. `callKnown` reaches the known JS target
// `knownArithmetic`, whose `2 * 3 + 4` is 10 through a non-folded caller; the
// known builtin `Math.abs` over the dynamic `-7` yields 7; the single generic
// call site yields 13 for `genericFirst` and 23 for `genericSecond` while the
// module-level reference is reassigned between the final calls; the positional
// decimal weight receives the supplied 1, 2, 3 through spread and collects them
// through a rest parameter, so `1*1 + 2*10 + 3*100 = 321`; reversing either the
// spread input or the rest traversal yields 123 instead, which distinguishes the
// required argument order. Two fresh
// constructions with the value 17 produce two distinct receivers that both
// carry the `Box` prototype, and the receiver-returning method compares equal
// to its receiver.
//
// Evidence attribution: the captured-slot read of `readValue` lowers to a
// `LoadTaggedField` whose base is the lexical environment (`LdLexVar` ->
// `BuildEnvSlotLoad` -> `GetLexicalEnvSlotOffset`); the captured-slot writes of
// `addValue` and `addAll` lower to `StoreEnvSlot` (`LowerStLexVar`).
// `callKnown` reaches its monomorphic target through `CallKnownJSFunction`;
// `callGeneric` keeps the generic `Call` form because both ordinary targets are
// present in the feedback domain of that site during warm-up, so the later
// reassignment of the reference is not a monomorphic-guard violation.
//
// Warm-up is bounded (200 iterations) and stays below
// `--compiler-jit-hotness-threshold=60000`, so nothing is compiled before the
// explicit `ArkTools.arkSteedCompileSync` installation. The `_GE` forms are
// used because the installation helper is unconditional and compilation
// markers aggregate over recompilations, while the E1 entry marker prints once
// per activation of the generated code; this construction can therefore only
// bound the counts from below. `DEOPT_COUNT *Deoptimize* 0` states the normal
// path: no installed method may silently deoptimize into the interpreter.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD makeCounter
//! COMPILE_COUNT_GE *makeCounter* 1
//! EXEC_COUNT_GE *makeCounter* 1

//! METHOD readValue
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *readValue* 1
//! EXEC_COUNT_GE *readValue* 1
//! DEOPT_COUNT *Deoptimize* 0

//! METHOD addValue
//! HAS StoreEnvSlot
//! COMPILE_COUNT_GE *addValue* 1
//! EXEC_COUNT_GE *addValue* 1

//! METHOD addAll
//! HAS StoreEnvSlot
//! COMPILE_COUNT_GE *addAll* 1
//! EXEC_COUNT_GE *addAll* 1

//! METHOD callKnown
//! HAS CallKnownJSFunction
//! COMPILE_COUNT_GE *callKnown* 1
//! EXEC_COUNT_GE *callKnown* 1

//! METHOD callBuiltin
//! COMPILE_COUNT_GE *callBuiltin* 1
//! EXEC_COUNT_GE *callBuiltin* 1

//! METHOD callGeneric
//! HAS Call
//! COMPILE_COUNT_GE *callGeneric* 1
//! EXEC_COUNT_GE *callGeneric* 1

// @ts-nocheck
declare function print(value: any): void;

// Closure factory: the returned methods read and write the captured binding
// `value` through the lexical environment of `makeCounter`.
function makeCounter(start) {
    let value = start;
    function readValue() {
        return value;
    }
    function addValue(delta = 1) {
        value = value + delta;
        return value;
    }
    function addAll(...deltas) {
        for (let index = 0; index < deltas.length; index++) {
            value = value + deltas[index];
        }
        return value;
    }
    return { readValue: readValue, addValue: addValue, addAll: addAll };
}

function knownArithmetic(left, right) {
    return left * right + 4;
}

function callKnown(left, right) {
    return knownArithmetic(left, right);
}

function callBuiltin(value) {
    return Math.abs(value);
}

function genericFirst(value) {
    return value + 10;
}

function genericSecond(value) {
    return value * 7 + 2;
}

let genericReference = genericFirst;

function callGeneric(value) {
    return genericReference(value);
}

// Order-sensitive positional probe: the spread call delivers the arguments in
// order, the rest parameter collects them in that order, and each position
// carries its own decimal weight, so a reordered delivery is observable.
function decimalWeight(...digits) {
    let weight = 0;
    let place = 1;
    for (let index = 0; index < digits.length; index++) {
        weight = weight + digits[index] * place;
        place = place * 10;
    }
    return weight;
}

class Box {
    constructor(value) {
        this.value = value;
    }
    getValue() {
        return this.value;
    }
    self() {
        return this;
    }
}

// Bounded warm-up: disposable environments and call sites only; the retained
// environments are created afterwards and observed after installation.
let warmup = 0;
for (let index = 0; index < 200; index++) {
    const disposable = makeCounter(index);
    warmup = warmup + disposable.readValue();
    warmup = warmup + disposable.addValue();
    warmup = warmup + disposable.addValue(undefined);
    warmup = warmup + disposable.addAll(...[1, 2]);
    warmup = warmup + callKnown(index, 2);
    warmup = warmup + callBuiltin(-(index + 1));
    // Both ordinary targets are represented at the generic site while its
    // feedback is prepared.
    genericReference = genericFirst;
    warmup = warmup + callGeneric(index);
    genericReference = genericSecond;
    warmup = warmup + callGeneric(index);
    warmup = warmup + decimalWeight(...[index, index + 1, index + 2]);
}

const mainCounter = makeCounter(10);
const separateCounter = makeCounter(100);

ArkTools.arkSteedCompileSync(makeCounter);
ArkTools.arkSteedCompileSync(mainCounter.readValue);
ArkTools.arkSteedCompileSync(mainCounter.addValue);
ArkTools.arkSteedCompileSync(mainCounter.addAll);
ArkTools.arkSteedCompileSync(callKnown);
ArkTools.arkSteedCompileSync(callBuiltin);
ArkTools.arkSteedCompileSync(callGeneric);

print(mainCounter.readValue());
print(mainCounter.addValue());
print(mainCounter.addValue(undefined));
print(mainCounter.addAll(...[2, 3]));
print(mainCounter.addValue(0));
print(mainCounter.addAll());
print(mainCounter.readValue());
print(separateCounter.readValue());
print(callKnown(2, 3));
print(callBuiltin(-7));
genericReference = genericFirst;
print(callGeneric(3));
genericReference = genericSecond;
print(callGeneric(3));
print(decimalWeight(...[1, 2, 3]));

const freshCounter = makeCounter(1000);
print(freshCounter.readValue());

const firstBox = new Box(17);
const secondBox = new Box(17);
print(firstBox.getValue());
print(secondBox.getValue());
print(firstBox === secondBox);
print(firstBox instanceof Box);
print(secondBox instanceof Box);
print(firstBox.self() === firstBox);
