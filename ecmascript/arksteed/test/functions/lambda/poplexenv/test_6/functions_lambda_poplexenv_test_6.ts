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

// G11.6 (P0) additive call evidence for `functions/lambda`
// (`arksteed-g116-call-closure-and-additive-evidence` C1/C3/C4; S11; V1/V2).
// `call_compiled_lambda_poplexenv_6` compiles and invokes the closure factory
// `factory`, which returns a per-`f` closure over the class `Multiplier`; the two
// returned closures are anonymous methods, so no attributable method pattern
// exists for them and only the named factory is claimed. `factory` is installed
// by the pre-existing installation call before its first invocation (bounded
// test-side preparation), so its call site lowers to the generic `Call` vertex
// observed in the caller dump and its entry marker is attributable.
// `COMPILE_COUNT_GE`/`EXEC_COUNT_GE` attribute compilation and entry of each
// claimed method; `DEOPT_COUNT *Deoptimize* 0` rules out a silent deopt into the
// interpreter. The pre-existing computed results and the expectation are
// unchanged.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD call_compiled_lambda_poplexenv_6
//! HAS Call
//! COMPILE_COUNT_GE *call_compiled_lambda_poplexenv_6* 1
//! EXEC_COUNT_GE *call_compiled_lambda_poplexenv_6* 1

//! METHOD factory
//! COMPILE_COUNT_GE *factory* 1
//! EXEC_COUNT_GE *factory* 1

//! DEOPT_COUNT *Deoptimize* 0

// poplexenv in factory + inner lambdas: each level has its own class
let compiled = false;

function call_compiled_lambda_poplexenv_6(factor: number, x: number, y: number): number {
    const factory = function(f: number) {
        class Multiplier {
            static offset: number = 0;
            static scale(v: number): number { return v * f + Multiplier.offset; }
        }
        return function(v: number): number {
            return Multiplier.scale(v);
        };
    };
    // Bounded test-side preparation: `factory` is installed before its first
    // call, so every invocation of the compiled caller enters the installed code
    // and emits the E1 entry marker. All three pre-existing installation calls
    // and every computed result are preserved.
    if (!compiled) {
        ArkTools.arkSteedCompileSync(factory);
    }
    const f1 = factory(factor);
    const f2 = factory(factor * 2);
    if (!compiled) {
        ArkTools.arkSteedCompileSync(f1);
        ArkTools.arkSteedCompileSync(f2);
        compiled = true;
    }
    return f1(x) + f2(y);
}

ArkTools.arkSteedCompileSync(call_compiled_lambda_poplexenv_6);
print(call_compiled_lambda_poplexenv_6(3, 5, 2));
print(call_compiled_lambda_poplexenv_6(2, 10, 7));
print(call_compiled_lambda_poplexenv_6(1, 3, 8));
