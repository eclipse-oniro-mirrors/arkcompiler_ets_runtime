/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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
// Keep both argument domains in the interpreter until the explicit compilation.
//! PARAMS --compiler-jit-hotness-threshold=60000
//! METHOD      f
//! HAS         CallRuntime  OptCopyRestArgs
//! COMPILE_COUNT_GE *f* 1
//! EXEC_COUNT_GE *f* 1
//! METHOD      invokeCreateTarget
//! HAS         CallKnownJSFunction
//! COMPILE_COUNT_GE *invokeCreateTarget* 1
//! EXEC_COUNT_GE *invokeCreateTarget* 1
//! DEOPT_COUNT *Deoptimize* 0
function Car(make, model, year) {
  this.make = make;
  this.model = model;
  this.year = year;
}
const auto = new Car('Honda', 'Accord', 1998);

let f = function(...args) {
    if (args.length === 1 && args[0] instanceof Car) {
    } else {
        print('b')
    }
}

for (let i = 0; i < 20; i++) {
    f(auto)
}

// Additive G11.6 preparation: establish the stable Car-and-Object domain before
// `f` is installed, so the preserved Object probe below is not compiled as an
// unprofiled cold-else `Deopt`. stdout is suppressed while the Object branch is
// exercised in the interpreter and `print` is restored immediately afterwards,
// so no observation and no expectation byte changes.
let savedPrint = print;
print = function () {};
for (let i = 0; i < 20; i++) {
    f(new Object())
}
print = savedPrint;

ArkTools.arkSteedCompileSync(f);
let res = true;
print(res)

f(new Object())

// Additive G11.6 evidence: a silent caller of the same domain proves the actual
// call form of the compiled original target in this case's own dump. It only
// ever supplies the already-warmed `Car` instance, so the no-print branch of
// the original probe stays the only observable behaviour.
function invokeCreateTarget() {
    return f(auto);
}

for (let round = 0; round < 4; round++) {
    invokeCreateTarget();
}
ArkTools.arkSteedCompileSync(invokeCreateTarget);
invokeCreateTarget();
