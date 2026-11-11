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

// Cold-name carrier: `readEarly` is compiled while `early` is still the hole
// and while the name string is still unmaterialized, so the hole branch of the
// throw.undefinedifholewithname bytecode lowers to an unfired-throw `Deopt`
// vertex. Firing the read from JIT-compiled code must execute that deopt,
// rebuild the interpreter frame at the same bytecode, materialize the name and
// throw a ReferenceError carrying the variable name; the follow-up call after
// initialization still returns the attached value from the compiled entry.
//! METHOD readEarly
//! COUNT_LE Deopt 1
//! HAS_NOT CallCommonStub GetStringFromConstPool
//! COMPILE_COUNT_GE *readEarly* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

declare function print(arg: string): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

function readEarly(): number {
    return early;
}

ArkTools.arkSteedCompileSync(readEarly);

try {
    print("unexpected " + readEarly());
} catch (e) {
    print(e.message);
}

let early = 9;

print(readEarly());
