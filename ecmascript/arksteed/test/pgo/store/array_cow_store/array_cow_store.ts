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

// G11.3 (P0): the copy-on-write transition of a JS-array element store. The
// store site is warmed on a writable receiver, so the compiled path keeps the
// `DeoptIfCOWElements` guard instead of the normal `EnsureWritableElements`
// construction. A fresh array literal is never written, therefore its elements
// stay copy-on-write: passing it to the installed store leaves the generated
// code through the copy-on-write guard (an eager deopt), and the interpreter
// completes the specified store on the receiver.
//
// This leaf deliberately carries the positive deopt direction; a zero-deopt
// counter is not asserted here, because the transition is the checked
// behavior. The normal `EnsureWritableElements` construction stays covered by
// the neighbouring writable-receiver leaves and by the normal array case
// `arrays/array_elements_normal`.
//
// Expected values follow ECMA-262 2021 10.4.2.1 ([[DefineOwnProperty]] for
// integer indices) and 23.1.3.1; the array identity is preserved while its
// element backing store is copied, so the sibling array is untouched. Every
// constant below is derived by hand.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD storeElement
//! HAS DeoptIfCOWElements
//! HAS StoreTaggedElement

//! COMPILE_COUNT_GE *storeElement* 1
//! EXEC_COUNT_GE *storeElement* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

function makeWritable(seed) {
    const array = [seed, seed + 1, seed + 2, seed + 3];
    // The first in-place write turns the copy-on-write literal into a writable
    // array before the target method ever observes it.
    array[0] = seed;
    return array;
}

function storeElement(array, index, value) {
    array[index] = value;
    return array[index];
}

const warm = makeWritable(1);
for (let i = 0; i < 20; i++) {
    storeElement(warm, 1, 10 + i);
}
ArkTools.arkSteedCompileSync(storeElement);

// Compiled entry on the writable receiver: no transition.
print(storeElement(warm, 2, 77));       // 77

// A fresh literal that was never written keeps copy-on-write elements. The
// installed store leaves through `DeoptIfCOWElements`, and the interpreter
// performs the specified write on the same array object.
const alias = [1, 2, 3, 4];
const sibling = [1, 2, 3, 4];
const sameObject = alias;
print(storeElement(alias, 1, 99));      // 99
print(alias === sameObject);            // true: the array identity is preserved
print(alias.join(","));                 // 1,99,3,4
print(sibling.join(","));               // 1,2,3,4: the sibling keeps its elements
