
/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

//! PARAMS --compiler-jit-hotness-threshold=60000

class Modifier {
}
class A extends Modifier {
    y = 1;
    z = 2;
    x = 0;

}
class B extends Modifier {
    y = 1;
    z = 2;
    x = 0;
}
class C extends Modifier {
    y = 1;
    z = 2;
    x = 0;
}
class D extends Modifier {
    y = 1;
    z = 2;
    x = 0;
}
class E extends Modifier {
    y = 1;
    z = 2;
    x = 5;
}
function visit(tag) {
    tag.x
    tag.x
    tag.x
    tag.x
    tag.x
    tag.x
    tag.x
    tag.x
    tag.x
    tag.x
    return tag.x;
}

let total = 0;
let a = new A();
let b = new B();
let c = new C();
let d = new D();
let e = new E();

function main(){
    let result = 0;
    for (let i = 0; i < 1000; i++) {
        visit(a);
        visit(b);
        visit(c);
        visit(d);
        result = visit(e);
    }
    print(result)
}
main()
ArkTools.arkSteedCompileSync(visit);

// G11.2 MEGA precondition: the named-access site is warmed with five receiver
// shapes. The delivered named IC holds at most four poly cases (the slot array
// is CACHE_MAX_LEN = 8 elements in two-element cases), so the fifth shape takes
// the overflow branch of `AddHandlerWithKey`/`AddWithoutKeyPoly`/`SetAsMega`:
// the slot pair becomes the MEGA state with Hole in the first slot instead of
// producing a five-element poly snapshot. The reader rejects that pair in its
// own state check (`TryGetICPolySnapshot` requires a non-weak TaggedArray in the
// first slot), so no poly feedback is recorded and the defensive
// `caseCount > MAX_NAMED_IC_POLY_CASES` guard is not the failure point. The
// method still compiles and runs; its named loads take the generic property
// path, whose values stay specified and produce no deopt event.
//! COMPILE_COUNT_GE *visit* 1
//! EXEC_COUNT_GE *visit* 1
//! DEOPT_COUNT *Deoptimize* 0

main()
