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

//! COMPILE_COUNT_GE *Point* 1
//! EXEC_COUNT_GE *Point* 1
//! COMPILE_COUNT_GE *spread_constructor_1* 1
//! EXEC_COUNT_GE *spread_constructor_1* 1
//! DEOPT_COUNT *Deoptimize* 0

class Point {
    x: number;
    y: number;
    z: number;

    constructor(x: number, y: number, z: number) {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    sum(): number {
        return this.x + this.y + this.z;
    }
}

//! METHOD spread_constructor_1
//! HAS CallRuntime NewObjApply
//! HAS CallRuntime StArraySpread
//! HAS Call
function spread_constructor_1(coords: [number, number, number]): number {
    const p = new Point(...coords);
    return p.sum();
}

ArkTools.arkSteedCompileSync(Point);
ArkTools.arkSteedCompileSync(spread_constructor_1);


print(spread_constructor_1([1, 2, 3]));
