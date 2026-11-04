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

// The env-slot read/write pair of G11.7 (S12). A module-level lexical `let`
// is read and written inside the compiled `readWrite`, the same binding is
// reached from the nested closure `bump`, and a heap-valued write in
// `storeObject` keeps the explicit `SetValueWithBarrier` half of the store.
// Each target method is compiled with `ArkTools.arkSteedCompileSync` and
// entered afterwards; the three printed observations are derived only from
// the specified JavaScript binding semantics.
//! PARAMS --compiler-jit-hotness-threshold=10000

//! METHOD readWrite
//! HAS LoadTaggedField
//! HAS StoreEnvSlot
//! COMPILE_COUNT_GE *readWrite* 1
//! EXEC_COUNT_GE *readWrite* 1
function readWrite(delta)
{
    // Slot read before the write is not forwarded: the load vertex is real.
    const before = counter;
    counter = before + delta;
    return counter;
}

//! METHOD storeObject
//! HAS LoadTaggedField
//! HAS StoreEnvSlot
//! HAS SetValueWithBarrier
//! COMPILE_COUNT_GE *storeObject* 1
//! EXEC_COUNT_GE *storeObject* 1
function storeObject(value)
{
    const before = stored;
    stored = value;
    return before;
}

function makeBump()
{
    //! METHOD bump
    //! HAS LoadTaggedField
    //! HAS StoreEnvSlot
    //! COMPILE_COUNT_GE *bump* 1
    //! EXEC_COUNT_GE *bump* 1
    function bump(step)
    {
        counter = counter + step;
        return counter;
    }
    return bump;
}

const bump = makeBump();

//! DEOPT_COUNT *Deoptimize* 0

let counter = 0;
let stored = null;
const seed = { payload: 1 };

for (let i = 0; i < 1000; i++) {
    readWrite(1);
    bump(0);
    storeObject(seed);
}

ArkTools.arkSteedCompileSync(readWrite);
ArkTools.arkSteedCompileSync(bump);
ArkTools.arkSteedCompileSync(storeObject);
ArkTools.waitJitCompileFinish(readWrite);
ArkTools.waitJitCompileFinish(bump);
ArkTools.waitJitCompileFinish(storeObject);

// counter: 1000 after the warm-up. Specified results of the compiled reads
// and writes follow.
print(readWrite(7));       // 1000 + 7 = 1007
print(bump(5));            // 1007 + 5 = 1012
print(counter);            // 1012

const replacement = { payload: 2 };
const previous = storeObject(replacement);
print(previous === seed ? 1 : 0);       // previous heap value was the seed
print(stored === replacement ? 1 : 0);  // the slot now holds the replacement
print(readWrite(0));       // 1012 + 0 = 1012
