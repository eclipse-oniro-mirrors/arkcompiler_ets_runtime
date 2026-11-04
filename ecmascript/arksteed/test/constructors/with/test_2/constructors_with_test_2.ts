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
class Point {
    x: number;
    y: number;

    constructor(x: number, y: number) {
        this.x = x;
        this.y = y;
        return {x: x, y: y};
    }
}

function constructor_with_return_2() {
    return new Point(4, 5);
}

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// every compiled target of this leaf (constructors and the compiled caller)
// carries its own compile/entry counters, and the actual construction form
// is annotated in the compiled caller's own dump. Output is unchanged.
//! COMPILE_COUNT_GE Point 1
//! EXEC_COUNT_GE *Point 1
//! COMPILE_COUNT_GE constructor_with_return_2 1
//! EXEC_COUNT_GE *constructor_with_return_2 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD constructor_with_return_2
//! HAS CallRuntime OptNewObjRange

ArkTools.arkSteedCompileSync(Point);
ArkTools.waitJitCompileFinish(Point);
ArkTools.arkSteedCompileSync(constructor_with_return_2);
ArkTools.waitJitCompileFinish(constructor_with_return_2);


const p = constructor_with_return_2();
print(p instanceof Point);
