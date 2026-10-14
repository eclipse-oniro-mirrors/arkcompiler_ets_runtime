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

// Interpreter twin of `functions/call/call_closure_env_slot`: the same
// semantic program, inputs and printed constants with the JIT disabled
// (`--compiler-enable-jit=false`, the supported case configuration; the runner
// default is `--compiler-enable-jit=true`, so this declaration is what makes
// the member interpreter-only). It carries no JIT, IR or count annotations and
// calls no installation helper, so its expectation is the interpreter result
// of the specified computation and is byte-identical to the primary. The
// primary oracle remains the specified semantics plus the hand-computed
// constants; this agreement is additional differential evidence.

//! PARAMS --compiler-enable-jit=false

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
// environments are created afterwards and observed after the same point as in
// the primary member.
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
