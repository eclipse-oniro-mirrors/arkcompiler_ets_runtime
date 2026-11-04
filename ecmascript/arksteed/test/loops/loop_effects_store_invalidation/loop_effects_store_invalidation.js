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

// G4.2 (P0) loop effects: a property value loaded from the receiver must not
// survive a write of the same name. `loopEffectsStoreInvalidation` reads
// `receiver.value` once before the loop and at the top of every iteration, and
// writes it inside the loop through the very same name, through the alias
// variable `alias` (the same object, which the compiler cannot prove), through
// the callback it cannot see into, and from a nested loop. The values read
// after each write and at the start of the next iteration are appended to a
// `Int32Array` journal at construction-fixed indices (`i * 6 + k`); the write
// of the journal is an elements write, so it does not invalidate the loaded
// property itself, and the driver prints the journal in order. A stale
// forwarded load or a reordered write changes the printed sequence, which is
// checked against the hand-computed values of the case's own expectation. The
// target is warmed while the raised hotness threshold keeps the warm-up in the
// interpreter, installed with the existing `ArkTools.arkSteedCompileSync`
// path, and entered afterwards; the compilation/entry counters prove the
// generated code ran and the zero deopt counter rules out a silent bailout
// into the interpreter after the entry. The construction fixes both
// multiplicities at one: 500 warm-up calls stay interpreted under the
// threshold, the synchronous install is the only compilation, and the single
// measured invocation is the only entry (an already installed ordinary
// compilation is rejected), so exact `COMPILE_COUNT`/`EXEC_COUNT` counts of
// one are asserted instead of lower bounds.
//! PARAMS --compiler-jit-hotness-threshold=10000

//! METHOD loopEffectsStoreInvalidation
//! HAS StoreTaggedField
//! HAS CallKnownJSFunction
//! COUNT_LE LoadTaggedField 6
//! COMPILE_COUNT *loopEffectsStoreInvalidation* 1
//! EXEC_COUNT *loopEffectsStoreInvalidation* 1
//! DEOPT_COUNT *Deoptimize* 0
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

ArkTools.arkSteedCompileSync(loopEffectsStoreInvalidation);
ArkTools.waitJitCompileFinish(loopEffectsStoreInvalidation);

receiver.value = 1;
loopEffectsStoreInvalidation(receiver, alias, growByThousand, journal, 3);
for (let i = 0; i < 18; i++) {
    print(journal[i]);
}
