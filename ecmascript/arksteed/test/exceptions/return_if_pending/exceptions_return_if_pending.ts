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
//! METHOD              store
//! HAS                 SetValueWithBarrier
//! HAS                 StoreTaggedElementWithBarrier
//! CODEGEN_COUNT       *Exception check* 2
//! CODEGEN_COUNT       *Return with pending exception* 1
//! COMPILE_COUNT_GE    *store* 1

//! METHOD              sameName
//! HAS                 StringEqual
//! CODEGEN_COUNT       *Exception check* 0
//! COMPILE_COUNT_GE    *sameName* 1
//! DEOPT_COUNT         *Deoptimize* 0

declare function print(arg: any): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

function makeStore(): any {
    var captured: any = null;

    function store(array: any[], holder: any, fn: (value: any) => number): number {
        captured = holder;        // Lexical env store of a heap value: write barrier only.
        array[0] = holder;        // Object element store: write barrier only.
        const first = fn(holder); // Generic call: can throw -> exception check 1.
        return fn(first);         // Generic call: can throw -> exception check 2.
    }

    function getCaptured(): any {
        return captured;
    }
    return {store, getCaptured};
}

function sameName(left: string, right: string): boolean {
    return left === right;
}

const helpers = makeStore();
const store = helpers.store;
const oneA = (): number => 1;
const oneB = (): number => 1;
const array = [null];

// Runtime-computed strings keep the comparison's operand type feedback alive
let nameA = "probe" + "-x";
let nameB = "other" + "-y";
for (let i = 0; i < 2000; i++) {
    store(array, {value: i}, oneA);
    store(array, {value: i}, oneB);
    sameName(nameA, nameB);
}
ArkTools.arkSteedCompileSync(store);
ArkTools.arkSteedCompileSync(sameName);

const held = {value: 7};
const result = store(array, held, oneA);
const namesMatch = sameName(nameA, nameA) && !sameName(nameA, nameB);
print(result === 1 && helpers.getCaptured() === held && array[0] === held && namesMatch ? "PASS" : "FAIL");
