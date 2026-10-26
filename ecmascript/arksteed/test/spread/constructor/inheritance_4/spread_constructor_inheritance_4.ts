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

//! COMPILE_COUNT_GE GrandParent 1
//! EXEC_COUNT_GE *GrandParent 1
//! COMPILE_COUNT_GE Parent 1
//! EXEC_COUNT_GE *[!d]Parent 1 # matches the Parent entry, not the GrandParent one
//! COMPILE_COUNT_GE *Child* 1
//! EXEC_COUNT_GE *Child* 1
//! COMPILE_COUNT_GE *spread_constructor_inheritance_4* 1
//! EXEC_COUNT_GE *spread_constructor_inheritance_4 1
//! DEOPT_COUNT *Deoptimize* 0

class GrandParent {
    a: number;
    b: number;

    constructor(a: number, b: number) {
        this.a = a;
        this.b = b;
    }
}

//! METHOD Parent
//! HAS CallRuntime OptSuperCall
class Parent extends GrandParent {
    c: number;

    constructor(a: number, b: number, c: number) {
        super(a, b);
        this.c = c;
    }
}

//! METHOD Child
//! HAS CallRuntime OptSuperCall
class Child extends Parent {
    d: number;

    constructor(a: number, b: number, c: number, d: number) {
        super(a, b, c);
        this.d = d;
    }

    total(): number {
        return this.a + this.b + this.c + this.d;
    }
}

//! METHOD spread_constructor_inheritance_4
//! HAS CallRuntime NewObjApply
//! HAS CallRuntime StArraySpread
//! HAS Call
function spread_constructor_inheritance_4(coords: [number, number, number, number]): number {
    const c = new Child(...coords);
    return c.total();
}

ArkTools.arkSteedCompileSync(GrandParent);
ArkTools.arkSteedCompileSync(Parent);
ArkTools.arkSteedCompileSync(Child);
ArkTools.arkSteedCompileSync(spread_constructor_inheritance_4);


print(spread_constructor_inheritance_4([1, 2, 3, 4]));
