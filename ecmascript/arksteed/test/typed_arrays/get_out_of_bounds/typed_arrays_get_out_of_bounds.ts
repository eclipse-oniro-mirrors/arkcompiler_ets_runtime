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

// Migrated from test262:
//   built-ins/TypedArrayConstructors/internals/Get/key-is-out-of-bounds.js
// (esid: sec-integer-indexed-exotic-objects-get-p-receiver)
//
// Integer-indexed exotic [[Get]] must return undefined for a numeric index
// that is < 0 or >= [[ArrayLength]], WITHOUT consulting the prototype
// (OrdinaryGet). The prototype carries throwing accessors for the probed
// keys; if the compiled code takes the ordinary property path the trap
// prints a marker and the expected output mismatches.

//! METHOD typed_arrays_get_out_of_bounds
//! COMPILE_COUNT_GE *typed_arrays_get_out_of_bounds* 1
//! EXEC_COUNT_GE *typed_arrays_get_out_of_bounds* 1
function typed_arrays_get_out_of_bounds(): void
{
    let ctors = [Int8Array, Uint8Array, Int16Array, Uint16Array,
                 Int32Array, Uint32Array, Float32Array, Float64Array];
    // The shared %TypedArray% prototype carries the traps.
    let proto = Object.getPrototypeOf(Int8Array.prototype);
    let trapDesc = {
        get: function () {
            print("TRAP");
            return undefined;
        },
        configurable: true
    };
    Object.defineProperty(proto, "-1", trapDesc);
    Object.defineProperty(proto, "2", trapDesc);
    Object.defineProperty(proto, "3", trapDesc);

    for (let i = 0; i < ctors.length; i++) {
        let sample = new ctors[i]([42, 43]);
        print(sample["-1"] === undefined ? 1 : 0);
        print(sample["2"] === undefined ? 1 : 0);
        print(sample["3"] === undefined ? 1 : 0);
    }
}

typed_arrays_get_out_of_bounds();

ArkTools.arkSteedCompileSync(typed_arrays_get_out_of_bounds);

typed_arrays_get_out_of_bounds();
