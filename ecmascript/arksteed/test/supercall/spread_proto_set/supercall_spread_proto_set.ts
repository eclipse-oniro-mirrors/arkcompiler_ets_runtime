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

/*
 * The SUPERCALLSPREAD operand is the frontend-materialized result of the
 * immediately preceding STARRAYSPREAD, so a polluted Object.prototype
 * prototype chain corrupts it one bytecode earlier: the materialization's
 * hole-to-undefined mapping must not fire while the chain above
 * Array.prototype can still resolve the hole. The starrayspread admission
 * rejects holey backings, which keeps this bytecode's raw slot copy exact.
 */

//! METHOD ES
//! HAS         BranchIfFastSpreadArray
//! HAS         CallBaseConstructorSpread
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1
//! DEOPT_COUNT * 0

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

class SSBase {
    constructor(a: any, b: any) {
        this.a = a;
        this.b = b;
    }
}

class ES extends SSBase {
    constructor(arr: number[]) {
        super(...arr);
    }
}

ArkTools.arkSteedCompileSync(ES);
ArkTools.waitJitCompileFinish(ES);

print("before: " + new ES([9, , 5]).b);
const np = Object.create(null);
np[1] = 'NP';
Object.setPrototypeOf(Object.prototype, np);
print("after: " + new ES([9, , 5]).b);
print("dense: " + new ES([1, 2]).b);
