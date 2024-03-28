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

declare interface ArkTools {
    isAOTCompiled(args: any): boolean;
}
declare function print(arg:any):string;
function replace(a : number)
{
    return a;
}

// Several params:
print(Math.sign(3, -0.12)) //: 1
print(Math.sign(-3, 0.12)) //: -1
print(Math.sign(-3, 0.12, -0.0)) //: -1
print(Math.sign(-4, 0.12, -0.0, 0.0)) //: -1

print(Math.sign(3)) //: 1
print(Math.sign(-3)) //: -1

// Test +0.0 and -0.0
// 0.0 and -0.0:
print(1 / Math.sign(0)) //: Infinity
print(1 / Math.sign(-0)) //: -Infinity

// Infinities
print(Math.sign(-Infinity)) //: -1
print(Math.sign(Infinity)) //: 1

// Replace, no deopt
let trueSign = Math.sign
Math.sign = replace
print(Math.sign(-12)) //: -12
Math.sign = trueSign

// Replace for the callee:
function doSign(a : any): number
{
    return Math.sign(a)
}

function printSign(a: any)
{
    try {
        print(doSign(a))
    } finally {
    }
}

// Check:
printSign(1) //: 1
printSign(Math.PI) //: 1
printSign(-Math.PI) //: -1
printSign(NaN) //: NaN
printSign(-1.5) //: -1
printSign(Infinity) //: 1

if (ArkTools.isAOTCompiled(printSign)) {
    // Replace standard builtin after call to standard builtin was profiled
    Math.sign = replace
}

// Check deopt from compiled
printSign(2)         //pgo: 1
//aot: [trace] Check Type: NotCallTarget1
//aot: 2

printSign(-Math.PI)  //pgo: -1
//aot: [trace] Check Type: NotCallTarget1
//aot: -3.141592653589793

Math.sign = trueSign
let obj = {};
obj.valueOf = (() => { return -23; })
//aot: [trace] Check Type: NotNumber2
printSign(obj);      //: -1
printSign(-1.5)      //: -1
printSign(Infinity)  //: 1

// Check IR correctness inside try-block
try {
    printSign(-12) //: -1
    //aot: [trace] Check Type: NotNumber2
    printSign("-12") //: -1
} catch (e) {
    print(e)
}

function Throwing() {
    this.value = -14;
}
Throwing.prototype.valueOf = function() {
    if (this.value > 0) {
        throw new Error("exception");
    }
    return this.value;
}
let throwingObj = new Throwing();

function tryCatchTest(obj: any, v : number)
{
    try {
        let x = obj.valueOf(); // exception
        print(Math.sign(x));
        print(Math.sign(v));
    } catch(e) {
        print(e);
        print(Math.sign(-v));
    } finally {
        print(Math.sign(0)); // 0
    }
}

// Test try-catch-deopt 1
tryCatchTest(throwingObj, ArkTools.isAOTCompiled(tryCatchTest) * 1)
//: -1
//pgo: 0
//aot: 1
//: 0

// Test try-catch-deopt 2
throwingObj.value = 14
tryCatchTest(throwingObj, ArkTools.isAOTCompiled(tryCatchTest) * 1)
//: Error: exception
//pgo: 0
//aot: -1
//: 0
