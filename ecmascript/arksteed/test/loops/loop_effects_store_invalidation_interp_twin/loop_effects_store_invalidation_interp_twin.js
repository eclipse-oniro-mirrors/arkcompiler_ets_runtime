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

// Interpreter twin of `loops/loop_effects_store_invalidation` (D3/A5): the
// same target method, the same warm-up, the same journal layout and the same
// printed per-iteration sequence with the JIT disabled. The primary oracle
// stays the specified ECMAScript evaluation order and the hand-computed
// constants; interpreter agreement is a necessary but not sufficient check.
// The twin carries no JIT/IR/count annotation and no code installation call,
// so it is the controlled JIT-off counterpart of the primary's positive
// compilation/entry evidence. Neither member prints a JIT-state boolean.
//! PARAMS --compiler-enable-jit=false

function loopEffectsStoreInvalidation(receiver, alias, callback, journal, outer)
{
    let observed = receiver.value;
    for (let i = 0; i < outer; i++) {
        observed = receiver.value;
        journal[i * 6] = observed;
        receiver.value = observed + 1;
        journal[i * 6 + 1] = receiver.value;
        alias.value = receiver.value + 10;
        journal[i * 6 + 2] = receiver.value;
        callback(receiver);
        journal[i * 6 + 3] = receiver.value;
        for (let j = 0; j < 2; j++) {
            receiver.value = receiver.value + 100;
            journal[i * 6 + 4 + j] = receiver.value;
        }
    }
    return observed;
}

function growByThousand(target)
{
    target.value = target.value + 1000;
}

const receiver = { value: 1 };
const alias = receiver;
const journal = new Int32Array(18);
const warmJournal = new Int32Array(12);

for (let i = 0; i < 500; i++) {
    receiver.value = 1;
    loopEffectsStoreInvalidation(receiver, alias, growByThousand, warmJournal, 2);
}

receiver.value = 1;
loopEffectsStoreInvalidation(receiver, alias, growByThousand, journal, 3);
for (let i = 0; i < 18; i++) {
    print(journal[i]);
}
