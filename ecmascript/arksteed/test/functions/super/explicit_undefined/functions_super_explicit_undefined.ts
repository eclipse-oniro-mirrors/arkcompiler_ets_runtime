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

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3,#C4):
// the original probes and the byte-identical expectation of this leaf are unchanged.
// The existing installation of `CallSuperExplicitUndefinedBase` stays. The super call lives
// in the derived constructor `CallSuperExplicitUndefinedDerived`, which is additionally
// compiled as bounded, silent same-domain preparation, so the leaf's own dump carries the
// actually reached super-call form `CallRuntime OptSuperCall` instead of a borrowed one; a
// `Call` substitution for that runtime form is not claimed.
// `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and entry of the claimed
// methods (E1/E2), and `DEOPT_COUNT *Deoptimize* 0` asserts that the normal path does not
// silently fall back into the interpreter.
// The targets write no captured variable (the leaf creates no closure), so the conditional
// `LoadTaggedField`/`StoreEnvSlot` slot annotation is not applicable and is not fabricated;
// the `LoadTaggedField` nodes of these dumps are the function-context and property loads
// that C3 excludes from captured-binding evidence. No `CallSelf`/`CallKnownBuiltin` is
// reached here, and none was transferred to the pair.
//! COMPILE_COUNT_GE CallSuperExplicitUndefinedBase 1
//! EXEC_COUNT_GE *CallSuperExplicitUndefinedBase 1
//! COMPILE_COUNT_GE CallSuperExplicitUndefinedDerived 1
//! EXEC_COUNT_GE *CallSuperExplicitUndefinedDerived 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD CallSuperExplicitUndefinedDerived
//! HAS CallRuntime OptSuperCall

class CallSuperExplicitUndefinedBase {
    make: string;
    model: string;
    year: number;

    constructor(make: string = "Unknown", model: string = "Model", year: number = 2020) {
        this.make = make;
        this.model = model;
        this.year = year;
    }
}

class CallSuperExplicitUndefinedDerived extends CallSuperExplicitUndefinedBase {
    constructor() {
        super(undefined, "Civic");
    }
}

ArkTools.arkSteedCompileSync(CallSuperExplicitUndefinedBase);
ArkTools.waitJitCompileFinish(CallSuperExplicitUndefinedBase);
ArkTools.arkSteedCompileSync(CallSuperExplicitUndefinedDerived);
ArkTools.waitJitCompileFinish(CallSuperExplicitUndefinedDerived);


let car = new CallSuperExplicitUndefinedDerived();
print(car.make);
print(car.model);
print(car.year);
