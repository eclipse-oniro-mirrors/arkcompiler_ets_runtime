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

// Self-healing carrier: the interpreter fires the same TDZ read once BEFORE
// `readEarly` is compiled, which materializes the name string into the shared
// constant pool. The subsequent compilation must therefore fold the name into
// a heap constant and keep a real deferred `Throw` on the hole branch: no
// unfired-throw deopt is emitted and no runtime deopt occurs.
//! METHOD readEarly
//! HAS HeapConstant
//! HAS Throw
//! HAS_NOT CallCommonStub GetStringFromConstPool
//! HAS_NOT Deopt
//! COMPILE_COUNT_GE *readEarly* 1
//! DEOPT_COUNT *Deoptimize* 0

declare function print(arg: string): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

function readEarly(): number {
    return early;
}

try {
    print("unexpected " + readEarly());
} catch (e) {
    print(e.name);
}

let early = 9;

ArkTools.arkSteedCompileSync(readEarly);

print(readEarly());
