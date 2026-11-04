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
class Rectangle {
    width: number;
    height: number;

    constructor();
    constructor(width: number);
    constructor(width: number, height: number);
    constructor(width?: number, height?: number) {
        this.width = width || 1;
        this.height = height || this.width;
    }

    area(): number {
        return this.width * this.height;
    }
}

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// the original target `Rectangle` keeps its own compilation and entry evidence,
// and the silent same-domain caller `probeOverloadConstruction` carries the
// actual construction form of all three overload sites. Output is unchanged.
//! COMPILE_COUNT_GE Rectangle 1
//! EXEC_COUNT_GE *Rectangle 1
//! COMPILE_COUNT_GE probeOverloadConstruction 1
//! EXEC_COUNT_GE *probeOverloadConstruction 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD probeOverloadConstruction
//! HAS CallRuntime OptNewObjRange

ArkTools.arkSteedCompileSync(Rectangle);
ArkTools.waitJitCompileFinish(Rectangle);


function probeOverloadConstruction() {
    return [new Rectangle(), new Rectangle(5), new Rectangle(4, 6)];
}
ArkTools.arkSteedCompileSync(probeOverloadConstruction);
ArkTools.waitJitCompileFinish(probeOverloadConstruction);


let rect1 = new Rectangle();
print(rect1.width);
print(rect1.height);
print(rect1.area());

let rect2 = new Rectangle(5);
print(rect2.width);
print(rect2.height);
print(rect2.area());

let rect3 = new Rectangle(4, 6);
print(rect3.width);
print(rect3.height);
print(rect3.area());

// Bounded silent preparation: the three overload construction forms through the
// compiled caller. It prints nothing and changes no earlier observation.
probeOverloadConstruction();
