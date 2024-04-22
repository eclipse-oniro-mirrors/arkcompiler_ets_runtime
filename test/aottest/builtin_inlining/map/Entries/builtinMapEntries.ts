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

function doEntries(x : any) {
    return myMap.entries(x);
}

function printEntries(x : any) {
    try {
        print(doEntries(x));
    } finally {
    }
}

let myMap = new Map([[0, 0], [0.0, 5], [-1, 1], [2000, 0.5], [56, "oops"], ["xyz", ", ."]]);

// Check without params
print(myMap.entries()); //: [object Map Iterator]

// Check with single param
print(myMap.entries(0).next().value); //: 0,5

// Check with 2 params
print(myMap.entries(0, 0).next().value); //: 0,5

// Check with 3 params
print(myMap.entries(-1, 10.2, 15).next().value); //: 0,5

// Check own methods
print(myMap.entries().throw); //: function throw() { [native code] }
print(myMap.entries().return); //: function return() { [native code] }

// Check using in loop
for (let key of myMap.entries()) {
    print(key);
}
//: 0,5
//: -1,1
//: 2000,0.5
//: 56,oops
//: xyz,, .

// Replace standard builtin
let true_entries = myMap.entries
myMap.entries = replace

// no deopt
print(myMap.entries(2.5)); //: 2.5
myMap.entries = true_entries

if (ArkTools.isAOTCompiled(printEntries)) {
    // Replace standard builtin after call to standard builtin was profiled
    myMap.entries = replace
}
printEntries(2.5); //pgo: [object Map Iterator]
//aot: 2.5

printEntries("abc"); //pgo: [object Map Iterator]
//aot: abc

myMap.entries = true_entries

// Check IR correctness inside try-block
try {
    printEntries(2.5); //: [object Map Iterator]
    printEntries("abc"); //: [object Map Iterator]
} catch (e) {
}

// Check using in a loop
let iter1 = myMap.entries();
for (let key of iter1) {
    print(key);
}
//: 0,5
//: -1,1
//: 2000,0.5
//: 56,oops
//: xyz,, .

// Check reusing possibility
for (let key of iter1) {
    print(key);
} // <nothing>

// Check using out of boundaries
print(iter1.next().value); //: undefined

// Check using after inserting / deleting
let iter2 = myMap.entries();
//aot: [trace] aot inline builtin: Map.delete, caller function name:func_main_0@builtinMapEntries
myMap.delete(-1);
myMap.set(2000, 1e-98);
myMap.set("xyz", -100);
for (let key of iter2) {
    print(key);
}
//: 0,5
//: 2000,1e-98
//: 56,oops
//: xyz,-100

