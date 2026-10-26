/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//! PARAMS --compiler-jit-hotness-threshold=60000
//! METHOD makePair
//! COUNT StoreTaggedElement 2                      # (1 per fast path) * 2

// After warmup with ints the lowered append stores check the value kind
// against the recorded elements kind. Feeding non-int values must eagerly
// deopt to the interpreter and still build correct arrays; going back to
// ints keeps working.
function makePair(a, b)
{
    return [a, b];
}

for (let i = 0; i < 20000; i++) {
    makePair(i, i + 1);
}

ArkTools.arkSteedCompileSync(makePair);
ArkTools.waitJitCompileFinish(makePair);

let r1 = makePair("x", "y");
print("strings: " + r1.join(",") + " len=" + r1.length);

let marker = { tag: 7 };
let r2 = makePair(marker, 3.5);
print("mixed: " + r2[0].tag + "," + r2[1] + " same:" + (r2[0] === marker));

let r3 = makePair(1.5, null);
print("double/null: " + r3.join(",") + " len=" + r3.length);

let ok = true;
for (let i = 0; i < 512; i++) {
    let r = makePair(i, i + 2);
    ok = ok && r.length === 2 && r[0] === i && r[1] === i + 2;
    if ((i & 63) === 0) {
        ArkTools.gc();
    }
}
let r4 = makePair(7, 8);
ok = ok && r4.join(",") === "7,8";
print("ints-after-deopt: " + (ok ? "PASS" : "FAIL"));
