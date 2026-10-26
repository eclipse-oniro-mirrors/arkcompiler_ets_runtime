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

// Interpreter twin of `pgo/lexical_env/env_slot_read_write` (V3/A5/E6): the
// same target methods, warm-up and printed observations with the JIT disabled.
// The primary oracle stays the specified ECMA-262 binding semantics and the
// hand-computed constants; interpreter agreement is a necessary but not
// sufficient check. The twin carries no JIT/IR/count annotation and no
// installation call, so it is the controlled JIT-off counterpart of the
// primary's positive compilation/entry evidence.
//! PARAMS --compiler-enable-jit=false

function readWrite(delta)
{
    const before = counter;
    counter = before + delta;
    return counter;
}

function storeObject(value)
{
    const before = stored;
    stored = value;
    return before;
}

function makeBump()
{
    function bump(step)
    {
        counter = counter + step;
        return counter;
    }
    return bump;
}

const bump = makeBump();

let counter = 0;
let stored = null;
const seed = { payload: 1 };

for (let i = 0; i < 1000; i++) {
    readWrite(1);
    bump(0);
    storeObject(seed);
}

print(readWrite(7));       // 1000 + 7 = 1007
print(bump(5));            // 1007 + 5 = 1012
print(counter);            // 1012

const replacement = { payload: 2 };
const previous = storeObject(replacement);
print(previous === seed ? 1 : 0);       // previous heap value was the seed
print(stored === replacement ? 1 : 0);  // the slot now holds the replacement
print(readWrite(0));       // 1012 + 0 = 1012
