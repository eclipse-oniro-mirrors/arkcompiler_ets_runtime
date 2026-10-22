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

/*
 * @tc.name:sendableirbaseline
 * @tc.desc: performance/functional baseline for the 16 IR-ized sendable bytecodes
 * @tc.type: FUNC
 */

/*
 * Entry module of the sendable IR baseline test. Every benchmark function shares the
 * uniform signature (selector: number, n: number): the selector picks the encoding
 * variant of the target instruction (e.g. 0 = imm8 slot/import, 1 = wide slot/import;
 * unused selectors are documented per function), so one function per instruction
 * family replaces one-function-per-value. Static identifier references inside the
 * timed loops are kept verbatim: the emitted bytecode encoding (imm4/imm8/wide,
 * import/local-export index) depends on them, not on runtime values.
 *
 * Covered bytecodes (see docs of task 20260807932667):
 *   1  CALLRUNTIME_DEFINESENDABLECLASS          -> benchDefineSendableClass (class defined in loop body)
 *   2  CALLRUNTIME_LDSENDABLECLASS              -> SelfRef.benchLdSendableClass (self reference inside sendable class)
 *   3  CALLRUNTIME_NEWSENDABLEENV (imm8)        -> module init of dynamically imported ./sendableirnewsenv
 *   4  CALLRUNTIME_WIDENEWSENDABLEENV           -> module init of dynamically imported ./sendableirwidesenv
 *   5  CALLRUNTIME_LDSENDABLEVAR imm4      -> benchSendableVarImm4 selector 0 (module scope sendable fn, slot < 16)
 *   6  CALLRUNTIME_LDSENDABLEVAR imm8           -> sendableirwidesenv.benchSendableVar selector 0 (slot in 16..255)
 *   7  CALLRUNTIME_WIDELDSENDABLEVAR            -> sendableirwidesenv.benchSendableVar selector 1 (slot >= 256)
 *   8  CALLRUNTIME_STSENDABLEVAR imm4           -> benchSendableVarImm4 selector 1
 *   9  CALLRUNTIME_STSENDABLEVAR imm8           -> sendableirwidesenv.benchSendableVar selector 2
 *   10 CALLRUNTIME_WIDESTSENDABLEVAR            -> sendableirwidesenv.benchSendableVar selector 3
 *   11 CALLRUNTIME_LDSENDABLEEXTERNALMODULEVAR     -> sendableirextuser.benchLdExtModuleVar selector 0
 *   12 CALLRUNTIME_WIDELDSENDABLEEXTERNALMODULEVAR -> sendableirextuser.benchLdExtModuleVar selector 1
 *   13 CALLRUNTIME_LDLAZYSENDABLEMODULEVAR          -> sendableirlazyuser.benchLdLazyModuleVar selector 0
 *   14 CALLRUNTIME_WIDELDLAZYSENDABLEMODULEVAR      -> sendableirlazyuser.benchLdLazyModuleVar selector 1
 *   15 CALLRUNTIME_LDSENDABLELOCALMODULEVAR         -> benchLdBaseValLocalModuleVar (own exported var, index < 256)
 *   16 CALLRUNTIME_WIDELDSENDABLELOCALMODULEVAR     -> sendableirlocalwide.benchLdLocalModuleVar selector 1
 *
 * Timing: one warmup call, then a timed call per (instruction, selector); elapsed
 * milliseconds are printed on their own line so expect_output.txt matches them with
 * the __INT__ wildcard. NEWSENDABLEENV variants only execute in func_main_0 of a
 * module, so they are timed as a module-init aggregate around a dynamic import.
 */

declare function print(arg: any): string;

import {benchLdExtModuleVar} from "./sendableirextuser";
import {benchLdLazyModuleVar} from "./sendableirlazyuser";
import {benchLdLocalModuleVar} from "./sendableirlocalwide";

// local export read by a sendable function of this module: LDSENDABLELOCALMODULEVAR (index < 256)
export var baseVal: number = 3;

// module scope sendable entities, all captured by nested functions so they are
// stored in the sendable env created by NEWSENDABLEENV at module init (imm8
// numVars == 3) and addressed by ST/LDSENDABLEVAR imm4 (slots 0..2)
function sfA() {
    "use sendable";
    return 1;
}

function sfB() {
    "use sendable";
    return 2;
}

class SelfRef {
    x: number = 1;
    constructor() {
        "use sendable";
    }
    // self reference inside the sendable class itself: LDSENDABLECLASS (selector unused)
    benchLdSendableClass(selector: number, n: number): boolean {
        let r: any = 0;
        for (let i = 0; i < n; i++) {
            r = SelfRef;
            r = SelfRef;
            r = SelfRef;
            r = SelfRef;
            r = SelfRef;
            r = SelfRef;
            r = SelfRef;
            r = SelfRef;
            r = SelfRef;
            r = SelfRef;
        }
        return r === SelfRef;
    }
}

// DEFINESENDABLECLASS executes once per loop iteration (class definition inside the loop body; selector unused)
function benchDefineSendableClass(selector: number, n: number): number {
    let r = 0;
    for (let i = 0; i < n; i++) {
        class LocalS {
            y: number = 2;
            constructor() {
                "use sendable";
            }
        }
        r += new LocalS().y;
    }
    return r;
}

// selector 0: LDSENDABLEVAR imm4 (load module scope sendable function sfA, slot < 16, level 0)
// selector 1: STSENDABLEVAR imm4 (store to the sendable env slot of sfB, slot < 16, level 0)
function benchSendableVarImm4(selector: number, n: number): boolean {
    if (selector === 0) {
        let r: any = 0;
        for (let i = 0; i < n; i++) {
            r = sfA;
            r = sfA;
            r = sfA;
            r = sfA;
            r = sfA;
            r = sfA;
            r = sfA;
            r = sfA;
            r = sfA;
            r = sfA;
        }
        return r === sfA;
    }
    let f: any = sfA;
    for (let i = 0; i < n; i++) {
        f = sfB;
        f = sfB;
        f = sfB;
        f = sfB;
        f = sfB;
        f = sfB;
        f = sfB;
        f = sfB;
        f = sfB;
        f = sfB;
    }
    return sfB === f;
}

// LDSENDABLELOCALMODULEVAR imm8: read own exported module var in sendable context (selector unused)
function benchLdBaseValLocalModuleVar(selector: number, n: number): number {
    "use sendable";
    let r = 0;
    for (let i = 0; i < n; i++) {
        r += baseVal;
        r += baseVal;
        r += baseVal;
        r += baseVal;
        r += baseVal;
        r += baseVal;
        r += baseVal;
        r += baseVal;
        r += baseVal;
        r += baseVal;
    }
    return r;
}

const WARMUP = 50;
const LOOP = 100000;
const LOOP_CLASS_DEF = 200;

function timedRun(name: string, fn: any, n: number, selector: number): void {
    let probe = fn(selector, WARMUP);
    let start = Date.now();
    let result = fn(selector, n);
    let elapsed = Date.now() - start;
    print(name + " result " + probe + " " + result);
    print(name + " time-ms");
    print(elapsed);
}

print("sendable-ir-baseline start");

timedRun("DEFINESENDABLECLASS", benchDefineSendableClass, LOOP_CLASS_DEF, 0);
timedRun("LDSENDABLECLASS", new SelfRef().benchLdSendableClass, LOOP, 0);
timedRun("LDSENDABLEVAR_IMM4", benchSendableVarImm4, LOOP, 0);
timedRun("STSENDABLEVAR_IMM4", benchSendableVarImm4, LOOP, 1);
timedRun("LDSENDABLELOCALMODULEVAR", benchLdBaseValLocalModuleVar, LOOP, 0);
timedRun("LDSENDABLEEXTERNALMODULEVAR", benchLdExtModuleVar, LOOP, 0);
timedRun("WIDE_LDSENDABLEEXTERNALMODULEVAR", benchLdExtModuleVar, LOOP, 1);
timedRun("LDLAZYSENDABLEMODULEVAR", benchLdLazyModuleVar, LOOP, 0);
timedRun("WIDE_LDLAZYSENDABLEMODULEVAR", benchLdLazyModuleVar, LOOP, 1);
timedRun("WIDE_LDSENDABLELOCALMODULEVAR", benchLdLocalModuleVar, LOOP, 1);

// NEWSENDABLEENV (imm8) / WIDENEWSENDABLEENV only execute during func_main_0 of
// a module, so they are timed as a module-init aggregate around a dynamic
// import of dedicated modules.
let importStart = Date.now();
import("./sendableirnewsenv").then((m) => {
    let initMs = Date.now() - importStart;
    print("NEWSENDABLEENV module-init-ms");
    print(initMs);
    print("NEWSENDABLEENV check " + (m.newsenvCheck() === 60));

    importStart = Date.now();
    import("./sendableirwidesenv").then((m2) => {
        let wideInitMs = Date.now() - importStart;
        print("WIDENEWSENDABLEENV module-init-ms");
        print(wideInitMs);
        timedRun("LDSENDABLEVAR_IMM8", m2.benchSendableVar, LOOP, 0);
        timedRun("WIDE_LDSENDABLEVAR", m2.benchSendableVar, LOOP, 1);
        timedRun("STSENDABLEVAR_IMM8", m2.benchSendableVar, LOOP, 2);
        timedRun("WIDE_STSENDABLEVAR", m2.benchSendableVar, LOOP, 3);
        print("sendable-ir-baseline done");
    });
});
