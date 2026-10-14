/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence
// #C1,#C3,#C4; arksteed-jsts-p0-semantic-rows#S11). The original program below
// is unchanged: the bounded recursive proxy/Promise invocation, its printed
// bytes and the bounded counter `count` keep their original meaning.
//
// Address evidence comes from this leaf's own dump of `Test`. The module-local
// `count` is captured by `Test` through the module lexical environment, so its
// read lowers to a `LoadTaggedField` whose base is the lexical environment
// (`LdLexVar` -> `BuildEnvSlotLoad` -> `GetLexicalEnvSlotOffset`, offset 40) and
// its `--count` write lowers to `StoreEnvSlot` (`LowerStLexVar`, offset 40);
// the proxy invocation `test(a2)` keeps the generic `Call` form and
// `new Proxy(...)` is the `OptNewObjRange` runtime construction. This dump
// contains no `CallSelf` and no `CallKnownBuiltin` node, so no such assertion is
// invented for this leaf.
//
// The bounded counter is not consumed by preparation: no statement here
// executes `Test` before the original probes, so the original call sequence
// (executor call plus one proxy recursion step per remaining counter unit) and
// the printed bytes stay exactly the same. The compilation request is
// synchronous; the bounded `waitJitCompileFinish` spin check only waits for the
// already requested machine code and never runs `Test`. The `_GE` lower bounds
// are used because installation requests and marker counts aggregate over
// recompilations and activations; `DEOPT_COUNT *Deoptimize* 0` states the normal
// path, so no installed method may silently deoptimize into the interpreter.
// `--compiler-jit-hotness-threshold=60000` keeps the run deterministic: nothing
// is compiled implicitly before the explicit installation of `Test`.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD Test
//! HAS Call
//! HAS LoadTaggedField
//! HAS StoreEnvSlot
//! HAS CallRuntime OptNewObjRange
//! COMPILE_COUNT_GE *Test* 1
//! EXEC_COUNT_GE *Test* 1
//! DEOPT_COUNT *Deoptimize* 0

var count = 2;

function Test(a1, a2) {
    print("a2: " + a2);
    if (count > 0) {
        --count;
    } else {
        return;
    }
    const proxyMethod = {};
    const test = new Proxy(Test, proxyMethod);
    test(a2);
    return;
}

ArkTools.arkSteedCompileSync(Test);
ArkTools.waitJitCompileFinish(Test);
var compilerResult = true;
print("compilerResult: " + compilerResult);
new Promise(Test);
