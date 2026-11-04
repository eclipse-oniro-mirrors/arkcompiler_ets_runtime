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

//! PARAMS --compiler-jit-hotness-threshold=60000
//! COMPILE_COUNT_GE *spreadArrayBasic* 1
//! EXEC_COUNT_GE *spreadArrayBasic* 1
//! COMPILE_COUNT_GE *spreadArrayWithElements* 1
//! EXEC_COUNT_GE *spreadArrayWithElements* 1
//! COMPILE_COUNT_GE *spreadArrayCopy* 1
//! EXEC_COUNT_GE *spreadArrayCopy* 1
//! COMPILE_COUNT_GE *spreadString* 1
//! EXEC_COUNT_GE *spreadString* 1
//! COMPILE_COUNT_GE *spreadFunctionCall* 1
//! EXEC_COUNT_GE *spreadFunctionCall* 1
//! COMPILE_COUNT_GE *spreadMathMax* 1
//! EXEC_COUNT_GE *spreadMathMax* 1
//! COMPILE_COUNT_GE *spreadSetDedup* 1
//! EXEC_COUNT_GE *spreadSetDedup* 1
//! COMPILE_COUNT_GE *spreadWithExpressions* 1
//! EXEC_COUNT_GE *spreadWithExpressions* 1
//! COMPILE_COUNT_GE *spreadNestedArray* 1
//! EXEC_COUNT_GE *spreadNestedArray* 1
//! COMPILE_COUNT_GE *spreadObjectBasic* 1
//! EXEC_COUNT_GE *spreadObjectBasic* 1
//! COMPILE_COUNT_GE *spreadObjectOverride* 1
//! EXEC_COUNT_GE *spreadObjectOverride* 1
//! COMPILE_COUNT_GE *spreadObjectNewProps* 1
//! EXEC_COUNT_GE *spreadObjectNewProps* 1
//! COMPILE_COUNT_GE *spreadObjectCopy* 1
//! EXEC_COUNT_GE *spreadObjectCopy* 1
//! COMPILE_COUNT_GE *spreadObjectNested* 1
//! EXEC_COUNT_GE *spreadObjectNested* 1
//! COMPILE_COUNT_GE *spreadObjectComputed* 1
//! EXEC_COUNT_GE *spreadObjectComputed* 1
//! COMPILE_COUNT_GE *spreadObjectOrder* 1
//! EXEC_COUNT_GE *spreadObjectOrder* 1
//! COMPILE_COUNT_GE *restBasic* 1
//! EXEC_COUNT_GE *restBasic* 1
//! COMPILE_COUNT_GE *restWithLeading* 1
//! EXEC_COUNT_GE *restWithLeading* 1
//! COMPILE_COUNT_GE *restLength* 1
//! EXEC_COUNT_GE *restLength* 1
//! COMPILE_COUNT_GE *restMixedTypes* 1
//! EXEC_COUNT_GE *restMixedTypes* 1
//! COMPILE_COUNT_GE *restWithDestructure* 1
//! EXEC_COUNT_GE *restWithDestructure* 1
//! COMPILE_COUNT_GE *restObjectDestructure* 1
//! EXEC_COUNT_GE *restObjectDestructure* 1
//! COMPILE_COUNT_GE *restNested* 1
//! EXEC_COUNT_GE *restNested* 1
//! COMPILE_COUNT_GE *spreadRestCombined* 1
//! EXEC_COUNT_GE *spreadRestCombined* 1
//! COMPILE_COUNT_GE *cloneExtendArray* 1
//! EXEC_COUNT_GE *cloneExtendArray* 1
//! COMPILE_COUNT_GE *cloneExtendObject* 1
//! EXEC_COUNT_GE *cloneExtendObject* 1
//! COMPILE_COUNT_GE *testSpreadConstructor* 1
//! EXEC_COUNT_GE *testSpreadConstructor* 1
//! COMPILE_COUNT_GE *spreadMethodCall* 1
//! EXEC_COUNT_GE *spreadMethodCall* 1
//! COMPILE_COUNT_GE *restCallback* 1
//! EXEC_COUNT_GE *restCallback* 1
//! DEOPT_COUNT *Deoptimize* 0


declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

// ==================== Array Spread ====================

// Basic array spread
//! METHOD spreadArrayBasic
//! HAS CallRuntime StArraySpread
//! HAS Call
function spreadArrayBasic(): string {
    const arr1 = [1, 2, 3];
    const arr2 = [4, 5, 6];
    const merged = [...arr1, ...arr2];
    return merged.join(',');
}

// Array spread with elements
//! METHOD spreadArrayWithElements
//! HAS CallRuntime StArraySpread
function spreadArrayWithElements(): string {
    const arr = [2, 3, 4];
    const result = [1, ...arr, 5, 6];
    return result.join(',');
}

// Array copy using spread
//! METHOD spreadArrayCopy
//! HAS CallRuntime StArraySpread
function spreadArrayCopy(): string {
    const original = [1, 2, 3];
    const copy = [...original];
    copy[0] = 100;
    return `${original[0]},${copy[0]}`;
}

// Spread string into array
//! METHOD spreadString
//! HAS CallRuntime StArraySpread
function spreadString(): string {
    const str = "hello";
    const chars = [...str];
    return chars.join('-');
}

// Spread in function call
//! METHOD spreadFunctionCall
//! HAS CallRuntime CallSpread
//! HAS CallRuntime StArraySpread
function spreadFunctionCall(): string {
    const args = [1, 2, 3];
    const sum = (a: number, b: number, c: number) => a + b + c;
    return `${sum(...args)}`;
}

// Spread Math.max
//! METHOD spreadMathMax
//! HAS CallRuntime CallSpread
//! HAS CallRuntime StArraySpread
function spreadMathMax(): string {
    const numbers = [5, 2, 8, 1, 9, 3];
    const max = Math.max(...numbers);
    const min = Math.min(...numbers);
    return `${max},${min}`;
}

// Spread with Set (deduplication)
//! METHOD spreadSetDedup
//! HAS CallRuntime StArraySpread
//! COUNT CallKnownBuiltin 1
//! HAS_NOT CallRuntime OptNewObjRange
function spreadSetDedup(): string {
    const arr = [1, 2, 2, 3, 3, 3, 4];
    const unique = [...new Set(arr)];
    return unique.join(',');
}

// Spread in array literal with expressions
//! METHOD spreadWithExpressions
//! HAS CallRuntime StArraySpread
function spreadWithExpressions(): string {
    const getArr = () => [4, 5, 6];
    const result = [1, 2, 3, ...getArr()];
    return result.join(',');
}

// Nested array spread
//! METHOD spreadNestedArray
//! HAS CallRuntime StArraySpread
function spreadNestedArray(): string {
    const nested = [[1, 2], [3, 4]];
    const flat = [...nested[0], ...nested[1]];
    return flat.join(',');
}

// ==================== Object Spread ====================

// Basic object spread
//! METHOD spreadObjectBasic
//! HAS CallRuntime CopyDataProperties
function spreadObjectBasic(): string {
    const obj1 = { a: 1, b: 2 };
    const obj2 = { c: 3, d: 4 };
    const merged = { ...obj1, ...obj2 };
    return `${merged.a},${merged.b},${merged.c},${merged.d}`;
}

// Object spread with override
//! METHOD spreadObjectOverride
//! HAS CallRuntime CopyDataProperties
function spreadObjectOverride(): string {
    const defaults = { x: 0, y: 0, z: 0 };
    const custom = { x: 10, y: 20 };
    const result = { ...defaults, ...custom };
    return `${result.x},${result.y},${result.z}`;
}

// Object spread with new properties
//! METHOD spreadObjectNewProps
//! HAS CallRuntime CopyDataProperties
function spreadObjectNewProps(): string {
    const base = { a: 1 };
    const extended = { ...base, b: 2, c: 3 };
    return `${extended.a},${extended.b},${extended.c}`;
}

// Object copy using spread
//! METHOD spreadObjectCopy
//! HAS CallRuntime CopyDataProperties
function spreadObjectCopy(): string {
    const original = { x: 10, y: 20 };
    const copy = { ...original };
    copy.x = 100;
    return `${original.x},${copy.x}`;
}

// Nested object spread (shallow)
//! METHOD spreadObjectNested
//! HAS CallRuntime CopyDataProperties
function spreadObjectNested(): string {
    const obj = { a: 1, nested: { b: 2 } };
    const copy = { ...obj };
    copy.nested.b = 200;
    return `${obj.nested.b},${copy.nested.b}`;
}

// Spread with computed properties
//! METHOD spreadObjectComputed
//! HAS CallRuntime CopyDataProperties
function spreadObjectComputed(): string {
    const key = 'dynamic';
    const base = { a: 1 };
    const result = { ...base, [key]: 'value' };
    return `${result.a},${(result as any)[key]}`;
}

// Spread order matters
//! METHOD spreadObjectOrder
//! HAS CallRuntime CopyDataProperties
function spreadObjectOrder(): string {
    const result = { a: 1, ...{ a: 2 }, ...{ a: 3 } };
    return `${result.a}`;
}

// ==================== Rest Parameters ====================

// Basic rest parameters
//! METHOD restBasic
//! HAS CallRuntime OptCopyRestArgs
function restBasic(...args: number[]): string {
    return args.join(',');
}

// Rest with leading params
//! METHOD restWithLeading
//! HAS CallRuntime OptCopyRestArgs
function restWithLeading(first: number, second: number, ...rest: number[]): string {
    return `${first},${second},${rest.join('-')}`;
}

// Rest parameter length
//! METHOD restLength
//! HAS CallRuntime OptCopyRestArgs
function restLength(...args: any[]): string {
    return `${args.length}`;
}

// Rest with different types
//! METHOD restMixedTypes
//! HAS CallRuntime OptCopyRestArgs
//! HAS CallKnownBuiltin
function restMixedTypes(...args: (number | string)[]): string {
    return args.map(a => typeof a).join(',');
}

// Rest in arrow function
const restArrow = (...nums: number[]): string => {
    return nums.reduce((a, b) => a + b, 0).toString();
};

// Rest with destructuring
//! METHOD restWithDestructure
//! HAS CallKnownBuiltin
//! HAS Call
function restWithDestructure([first, ...rest]: number[]): string {
    return `${first}:${rest.join(',')}`;
}

// Object rest in destructuring
//! METHOD restObjectDestructure
//! HAS CallRuntime OptCreateObjectWithExcludedKeys
//! HAS CallKnownBuiltin
function restObjectDestructure(): string {
    const obj = { a: 1, b: 2, c: 3, d: 4 };
    const { a, ...rest } = obj;
    return `${a},${Object.keys(rest).length}`;
}

// Nested rest
//! METHOD restNested
//! HAS CallRuntime OptCopyRestArgs
function restNested(...arrays: number[][]): string {
    const flattened = arrays.flat();
    return flattened.join(',');
}

// ==================== Combined Patterns ====================

// Spread and rest together
//! METHOD spreadRestCombined
//! HAS Call
function spreadRestCombined(): string {
    const collectAndSpread = (...args: number[]) => [...args, ...args];
    const result = collectAndSpread(1, 2, 3);
    return result.join(',');
}

// Clone and extend array
//! METHOD cloneExtendArray
//! HAS CallRuntime StArraySpread
function cloneExtendArray(): string {
    const original = [1, 2, 3];
    const extended = [...original, 4, 5];
    return `${original.length},${extended.length}`;
}

// Clone and extend object
//! METHOD cloneExtendObject
//! HAS CallRuntime CopyDataProperties
//! HAS CallKnownBuiltin
function cloneExtendObject(): string {
    const original = { a: 1 };
    const extended = { ...original, b: 2 };
    return `${Object.keys(original).length},${Object.keys(extended).length}`;
}

// Spread in class constructor
class SpreadConstructor {
    values: number[];
    constructor(...args: number[]) {
        this.values = [...args];
    }
    getValues(): string {
        return this.values.join(',');
    }
}

//! METHOD testSpreadConstructor
//! COUNT CallNewObjRangeBase 1
//! COUNT CallNewObjRangeDerived 1
//! HAS_NOT CallRuntime OptNewObjRange
//! HAS CallKnownJSFunction
function testSpreadConstructor(): string {
    const obj = new SpreadConstructor(1, 2, 3, 4, 5);
    return obj.getValues();
}

// Spread in method call
//! METHOD spreadMethodCall
//! HAS CallRuntime CallSpread
//! HAS CallRuntime StArraySpread
function spreadMethodCall(): string {
    const arr = [1, 2, 3, 4, 5];
    const result = arr.concat(...[[6, 7], [8, 9]]);
    return result.join(',');
}

// Rest in callback
//! METHOD restCallback
//! HAS Call
function restCallback(): string {
    const process = (callback: (...args: number[]) => number) => {
        return callback(1, 2, 3, 4, 5);
    };
    const sum = (...nums: number[]) => nums.reduce((a, b) => a + b, 0);
    return `${process(sum)}`;
}

// Test wrappers
function testRestBasic(): string {
    return restBasic(1, 2, 3, 4, 5);
}

function testRestWithLeading(): string {
    return restWithLeading(1, 2, 3, 4, 5);
}

function testRestLength(): string {
    return restLength(1, 'a', true, null, undefined);
}

function testRestMixedTypes(): string {
    return restMixedTypes(1, 'a', 2, 'b');
}

function testRestArrow(): string {
    return restArrow(1, 2, 3, 4, 5);
}

function testRestWithDestructure(): string {
    return restWithDestructure([1, 2, 3, 4, 5]);
}

function testRestNested(): string {
    return restNested([1, 2], [3, 4], [5, 6]);
}

// Warmup
for (let i = 0; i < 20; i++) {
    spreadArrayBasic();
    spreadArrayWithElements();
    spreadArrayCopy();
    spreadString();
    spreadFunctionCall();
    spreadMathMax();
    spreadSetDedup();
    spreadWithExpressions();
    spreadNestedArray();
    spreadObjectBasic();
    spreadObjectOverride();
    spreadObjectNewProps();
    spreadObjectCopy();
    spreadObjectNested();
    spreadObjectComputed();
    spreadObjectOrder();
    testRestBasic();
    testRestWithLeading();
    testRestLength();
    testRestMixedTypes();
    testRestArrow();
    testRestWithDestructure();
    restObjectDestructure();
    testRestNested();
    spreadRestCombined();
    cloneExtendArray();
    cloneExtendObject();
    testSpreadConstructor();
    spreadMethodCall();
    restCallback();
}

// JIT compile
ArkTools.arkSteedCompileSync(spreadArrayBasic);
ArkTools.arkSteedCompileSync(spreadArrayWithElements);
ArkTools.arkSteedCompileSync(spreadArrayCopy);
ArkTools.arkSteedCompileSync(spreadString);
ArkTools.arkSteedCompileSync(spreadFunctionCall);
ArkTools.arkSteedCompileSync(spreadMathMax);
ArkTools.arkSteedCompileSync(spreadSetDedup);
ArkTools.arkSteedCompileSync(spreadWithExpressions);
ArkTools.arkSteedCompileSync(spreadNestedArray);
ArkTools.arkSteedCompileSync(spreadObjectBasic);
ArkTools.arkSteedCompileSync(spreadObjectOverride);
ArkTools.arkSteedCompileSync(spreadObjectNewProps);
ArkTools.arkSteedCompileSync(spreadObjectCopy);
ArkTools.arkSteedCompileSync(spreadObjectNested);
ArkTools.arkSteedCompileSync(spreadObjectComputed);
ArkTools.arkSteedCompileSync(spreadObjectOrder);
ArkTools.arkSteedCompileSync(restBasic);
ArkTools.arkSteedCompileSync(restWithLeading);
ArkTools.arkSteedCompileSync(restLength);
ArkTools.arkSteedCompileSync(restMixedTypes);
ArkTools.arkSteedCompileSync(restWithDestructure);
ArkTools.arkSteedCompileSync(restObjectDestructure);
ArkTools.arkSteedCompileSync(restNested);
ArkTools.arkSteedCompileSync(spreadRestCombined);
ArkTools.arkSteedCompileSync(cloneExtendArray);
ArkTools.arkSteedCompileSync(cloneExtendObject);
ArkTools.arkSteedCompileSync(testSpreadConstructor);
ArkTools.arkSteedCompileSync(spreadMethodCall);
ArkTools.arkSteedCompileSync(restCallback);

print("compile spreadArrayBasic: " + true);
print("compile spreadArrayWithElements: " + true);
print("compile spreadArrayCopy: " + true);
print("compile spreadString: " + true);
print("compile spreadFunctionCall: " + true);
print("compile spreadMathMax: " + true);
print("compile spreadSetDedup: " + true);
print("compile spreadWithExpressions: " + true);
print("compile spreadNestedArray: " + true);
print("compile spreadObjectBasic: " + true);
print("compile spreadObjectOverride: " + true);
print("compile spreadObjectNewProps: " + true);
print("compile spreadObjectCopy: " + true);
print("compile spreadObjectNested: " + true);
print("compile spreadObjectComputed: " + true);
print("compile spreadObjectOrder: " + true);
print("compile restBasic: " + true);
print("compile restWithLeading: " + true);
print("compile restLength: " + true);
print("compile restMixedTypes: " + true);
print("compile restWithDestructure: " + true);
print("compile restObjectDestructure: " + true);
print("compile restNested: " + true);
print("compile spreadRestCombined: " + true);
print("compile cloneExtendArray: " + true);
print("compile cloneExtendObject: " + true);
print("compile testSpreadConstructor: " + true);
print("compile spreadMethodCall: " + true);
print("compile restCallback: " + true);

// Verify
print("spreadArrayBasic: " + spreadArrayBasic());
print("spreadArrayWithElements: " + spreadArrayWithElements());
print("spreadArrayCopy: " + spreadArrayCopy());
print("spreadString: " + spreadString());
print("spreadFunctionCall: " + spreadFunctionCall());
print("spreadMathMax: " + spreadMathMax());
print("spreadSetDedup: " + spreadSetDedup());
print("spreadWithExpressions: " + spreadWithExpressions());
print("spreadNestedArray: " + spreadNestedArray());
print("spreadObjectBasic: " + spreadObjectBasic());
print("spreadObjectOverride: " + spreadObjectOverride());
print("spreadObjectNewProps: " + spreadObjectNewProps());
print("spreadObjectCopy: " + spreadObjectCopy());
print("spreadObjectNested: " + spreadObjectNested());
print("spreadObjectComputed: " + spreadObjectComputed());
print("spreadObjectOrder: " + spreadObjectOrder());
print("testRestBasic: " + testRestBasic());
print("testRestWithLeading: " + testRestWithLeading());
print("testRestLength: " + testRestLength());
print("testRestMixedTypes: " + testRestMixedTypes());
print("testRestArrow: " + testRestArrow());
print("testRestWithDestructure: " + testRestWithDestructure());
print("restObjectDestructure: " + restObjectDestructure());
print("testRestNested: " + testRestNested());
print("spreadRestCombined: " + spreadRestCombined());
print("cloneExtendArray: " + cloneExtendArray());
print("cloneExtendObject: " + cloneExtendObject());
print("testSpreadConstructor: " + testSpreadConstructor());
print("spreadMethodCall: " + spreadMethodCall());
print("restCallback: " + restCallback());
