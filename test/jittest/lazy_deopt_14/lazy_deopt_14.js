/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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
 * Description:
 * 1. This code tests the lazy deoptimization that occurs in stobjbyname.
 *    After the JIT code for function 'Test2' is compiled,
 *    modifying an HClass invalidates the function,
 *    and subsequent accesses to it will detect this invalidation.
 * 2. Test2 call ChangePrototypeValue inlined.
 * 3. Test GC effect.
 * 4. Test a function perform lazy deoptimization on two of the functions on the call stack.
 */

function ChangePrototypeValue2(obj, shouldChange) {
    print("ChangeProto2 start.");
    if (shouldChange) {
        // Change the property 'x' at the second level of the prototype chain,
        // triggering lazy deoptimization of the JIT-compiled 'Test2' function.
        Object.defineProperty(obj.__proto__, 'y', {
            set: function (value) {
                this._y = 3;
                print("Set value to 3.");
            },
            enumerable: true,
            configurable: true
        });
        Object.defineProperty(obj.__proto__.__proto__, 'x', {
            set: function (value) {
                this._x = 2;
                print("Set value to 2.");
            },
            enumerable: true,
            configurable: true
        });
    }
    print("ChangeProto2 end.");
}

function ChangePrototypeValue(obj, shouldChange) {
    print("ChangeProto start.");
    ChangePrototypeValue2(obj, shouldChange);
    obj.y = 1;
    print("obj.y: ", obj.y);
    print("ChangeProto end.");
    // Additional code to prevent aggressive inlining.
    let test = {};
    test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x;
    test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x;
    test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x;
    test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x; test.x;
}


// Test function that calls ChangePrototypeValue and prints the value of obj.x.
function Test2(obj, shouldChange) {
    print("Test2 start.");
    ChangePrototypeValue(obj, shouldChange);
    obj.x = 1;
    print("Test2 obj.x :", obj.x);
    print("Test2 end.");
}

class A {}
class B extends A {}
class C extends B {}

// Set the initial value of property x through A.prototype.
A.prototype.x = 1;

const c = new C();

// Initial call to test without changing the prototype's property.
Test2(c, false);

ArkTools.jitCompileAsync(Test2);
print(ArkTools.waitJitCompileFinish(Test2));

ArkTools.jitCompileAsync(ChangePrototypeValue);
print(ArkTools.waitJitCompileFinish(ChangePrototypeValue));

print("------------------------------------------------------");
// Call test with the flag set to true to modify the prototype property, triggering lazy deoptimization.
const c2 = new C();
Test2(c2, true);