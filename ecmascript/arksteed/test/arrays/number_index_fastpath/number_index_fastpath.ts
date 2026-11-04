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

// @ts-nocheck

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD numericIndexMixed
//! HAS_NOT BranchIfInt64Compare
//! HAS_NOT Phi
//! HAS LoadTaggedElement
//! HAS F64ToI32Trunc
//! HAS_NOT CheckedTaggedIntToI32
//! COMPILE_COUNT numericIndexMixed 1
//! EXEC_COUNT_GE *numericIndexMixed 5
function numericIndexMixed(array, key) {
    return array[key];
}

//! METHOD numericIndexSignedZero
//! HAS LoadTaggedElement
//! HAS_NOT BranchIfInt64Compare
//! COMPILE_COUNT numericIndexSignedZero 1
//! EXEC_COUNT_GE *numericIndexSignedZero 1
function numericIndexSignedZero(array, key) {
    return array[key] + ':' + 1 / (key + key);
}

//! METHOD numericIndexRangePair
//! HAS LoadTaggedElement
//! HAS_NOT BranchIfInt64Compare
//! COMPILE_COUNT numericIndexRangePair 1
//! EXEC_COUNT_GE *numericIndexRangePair 2
function numericIndexRangePair(first, second, key) {
    return first[key] + ':' + second[key];
}

//! METHOD numericIndexHolePair
//! HAS LoadTaggedElement
//! HAS_NOT BranchIfInt64Compare
//! COMPILE_COUNT numericIndexHolePair 1
//! EXEC_COUNT_GE *numericIndexHolePair 1
function numericIndexHolePair(first, second, key) {
    return first[key] + ':' + second[key];
}

const values = ['zero', 'one', 'two'];
const doubleKeys = new Float64Array([1, 2, -0]);
for (let i = 0; i < 64; i++) {
    numericIndexMixed(values, i & 1);
    numericIndexMixed(values, doubleKeys[i & 1]);
    numericIndexSignedZero(values, 0);
    numericIndexRangePair(values, values, 1);
    numericIndexHolePair(values, values, 1);
}
ArkTools.arkSteedCompileSync(numericIndexMixed);
print(numericIndexMixed(values, 1));
print(numericIndexMixed(values, doubleKeys[1]));
print(numericIndexMixed(values, 0));
print(numericIndexMixed(values, doubleKeys[0]));
print(numericIndexMixed(values, doubleKeys[2]));
print(ArkTools.arkSteedIsCompiled(numericIndexMixed));
ArkTools.arkSteedCompileSync(numericIndexSignedZero);
print(ArkTools.arkSteedIsCompiled(numericIndexSignedZero));
print(numericIndexSignedZero(values, doubleKeys[2]));
ArkTools.arkSteedCompileSync(numericIndexRangePair);
print(numericIndexRangePair(values, values, doubleKeys[1]));
print(ArkTools.arkSteedIsCompiled(numericIndexRangePair));
print(numericIndexRangePair(values, ['zero'], doubleKeys[1]));
ArkTools.arkSteedCompileSync(numericIndexHolePair);
print(ArkTools.arkSteedIsCompiled(numericIndexHolePair));
print(numericIndexHolePair(values, ['zero', , 'two'], 1));
print('OK');
