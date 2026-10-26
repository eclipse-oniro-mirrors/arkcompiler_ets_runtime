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

// Migrated from test262:
//   built-ins/Array/prototype/copyWithin/coerced-values-start-change-start.js
// (esid: sec-array.prototype.copywithin)
//
// SECURITY: the `start` argument is coerced to an integer via ToInteger
// while the valueOf side effect shrinks the array, so the coerced start
// ends up out of bounds. The compiled copyWithin must re-read the length
// after the coercion and copy nothing.

//! METHOD arrays_copy_within_coerced_start
//! COMPILE_COUNT_GE *arrays_copy_within_coerced_start* 1
//! EXEC_COUNT_GE *arrays_copy_within_coerced_start* 1

function arrays_copy_within_coerced_start(): void
{
    function longDenseArray(): number[]
    {
        let a: number[] = [0];
        for (let i = 0; i < 1024; i++) {
            a[i] = i;
        }
        return a;
    }

    function shorten(): number
    {
        currArray.length = 20;
        return 1000;
    }

    function sameElements(left: Object, right: Object): boolean
    {
        let l = left as string[];
        let r = right as string[];
        if (l.length !== r.length) {
            return false;
        }
        for (let i = 0; i < l.length; i++) {
            if (l[i] !== r[i]) {
                return false;
            }
        }
        return true;
    }

    let expected: number[] = [];
    expected.length = 20;

    // Plain receiver: the shrunk array keeps its 20 elements.
    let currArray = longDenseArray();
    let result = currArray.copyWithin(0, { valueOf: shorten });
    print(sameElements(result, expected) ? 1 : 0);
    print(result.length === 20 ? 1 : 0);

    // Receiver with a long prototype: indices >= 20 resolve on the
    // prototype chain, so the first 24 slots come from proto[1000..1023].
    currArray = longDenseArray();
    Object.setPrototypeOf(currArray, longDenseArray());
    let expected2: number[] = [];
    expected2.length = 20;
    for (let i = 0; i < 24; i++) {
        expected2[i] = Object.getPrototypeOf(currArray)[i + 1000];
    }
    result = currArray.copyWithin(0, { valueOf: shorten });
    print(sameElements(result, expected2) ? 1 : 0);
    print(result.length === 24 ? 1 : 0);
}

arrays_copy_within_coerced_start();

ArkTools.arkSteedCompileSync(arrays_copy_within_coerced_start);

arrays_copy_within_coerced_start();
