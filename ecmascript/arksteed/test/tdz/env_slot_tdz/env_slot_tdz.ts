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

// Lexical TDZ carrier of G11.7 (S12). The module-level lexical `let counter`
// is read before its initialization from the compiled `readBeforeInit` and
// from the nested closure `readFromClosure`. Both compiled graphs carry the
// lexical hole comparison and the deferred `Throw` vertex whose stub is
// `ThrowUndefinedIfHole`. The oracle is the caught `ReferenceError` at the
// specified moment and the specified value after initialization; no deopt
// counter is claimed and the compiled-entry counter is not the TDZ oracle.
// The global-cell box guard is a different mechanism and is a separate leaf.
//! METHOD readBeforeInit
//! HAS LoadTaggedField
//! HAS Throw
//! METHOD readFromClosure
//! HAS LoadTaggedField
//! HAS Throw

declare function print(arg: string): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

function readBeforeInit(): number {
    return counter;   // ldlexvar counter → throw.undefinedifholewithname "counter"
}

function makeClosure(): () => number {
    function readFromClosure(): number {
        return counter;   // ldlexvar counter → throw.undefinedifholewithname "counter"
    }
    return readFromClosure;
}

const closure = makeClosure();

// Both targets are compiled while `counter` is still uninitialized, so the
// hole guard is a real runtime comparison in each graph.
ArkTools.arkSteedCompileSync(readBeforeInit);
ArkTools.arkSteedCompileSync(closure);

// Before initialization: the specified ReferenceError, caught at the call.
try {
    readBeforeInit();
    print("unexpected");
} catch (e) {
    print(e.name);
}

try {
    closure();
    print("unexpected");
} catch (e) {
    print(e.name);
}

let counter = 5;

// After initialization the same compiled code returns the specified value.
print(readBeforeInit());
print(closure());
