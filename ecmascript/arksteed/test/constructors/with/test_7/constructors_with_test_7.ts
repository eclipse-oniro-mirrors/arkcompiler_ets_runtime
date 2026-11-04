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
class PointBase {
    x: number;
    y: number;

    constructor(x: number, y: number) {
        this.x = x;
        this.y = y;
        return this.innerProduct.bind(this);
    }

    innerProduct(a: number, b: number): number {
        return this.innerProductImpl(a, b);
    }
}

class Point extends PointBase {
    innerProductImpl(a: number, b: number): number {
        return this.x * a + this.y * b;
    }
}

function constructor_with_return_7() {
    const p = new Point(4, 5);
    return p(100, 10);
}

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// every compiled target of this leaf (constructors and the compiled caller)
// carries its own compile/entry counters, and the actual construction form
// is annotated in the compiled caller's own dump. Output is unchanged.
//! COMPILE_COUNT_GE PointBase 1
//! EXEC_COUNT_GE *PointBase 1
//! COMPILE_COUNT_GE innerProduct 1
//! EXEC_COUNT_GE *innerProduct 1
//! COMPILE_COUNT_GE Point 1
//! EXEC_COUNT_GE *Point 1
//! COMPILE_COUNT_GE innerProductImpl 1
//! EXEC_COUNT_GE *innerProductImpl 1
//! COMPILE_COUNT_GE constructor_with_return_7 1
//! EXEC_COUNT_GE *constructor_with_return_7 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD constructor_with_return_7
//! HAS CallRuntime OptNewObjRange
//! HAS Call
//! METHOD Point
//! HAS CallRuntime OptSuperCallForwardAllArgs
//! METHOD PointBase
//! HAS Call

ArkTools.arkSteedCompileSync(PointBase);
ArkTools.waitJitCompileFinish(PointBase);
ArkTools.arkSteedCompileSync(PointBase.prototype.innerProduct);
ArkTools.waitJitCompileFinish(PointBase.prototype.innerProduct);
ArkTools.arkSteedCompileSync(Point);
ArkTools.waitJitCompileFinish(Point);
ArkTools.arkSteedCompileSync(Point.prototype.innerProductImpl);
ArkTools.waitJitCompileFinish(Point.prototype.innerProductImpl);
ArkTools.arkSteedCompileSync(constructor_with_return_7);
ArkTools.waitJitCompileFinish(constructor_with_return_7);


print(constructor_with_return_7());
