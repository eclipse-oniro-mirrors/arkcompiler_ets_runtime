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

// Compare only C interpreter container indexed access with existing ASM behavior.
// Shared native forEach behavior must remain unchanged.
function assertEqual(actual, expected, label) {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(label + ": " + JSON.stringify(actual) + " != " + JSON.stringify(expected));
    }
}

function create(name) {
    const Constructor = ArkPrivate.Load(ArkPrivate[name]);
    const object = name === "BitVector" || name === "FastBuffer" ? new Constructor(2) : new Constructor();
    if (name === "PlainArray") object.add(0, 42);
    else if (name === "Stack") object.push(42);
    else if (name === "Deque") object.insertEnd(42);
    else if (name !== "BitVector" && name !== "FastBuffer") object.add(42);
    return object;
}

// Constant and dynamic numeric keys must read and update existing slots.
for (const name of ["ArrayList", "Vector", "List", "LinkedList", "PlainArray", "Queue", "Deque", "Stack",
    "BitVector", "FastBuffer"]) {
    const object = create(name);
    object[0] = 1;
    assertEqual(object[0], 1, name + " constant index read/write");
    for (const key of [0, 0.0]) {
        object[key] = 0;
        assertEqual(object[key], 0, name + " dynamic index read/write " + typeof key);
        object[key] = 1;
        assertEqual(object[0], 1, name + " dynamic write updates existing slot " + typeof key);
    }

    // ASM distinguishes dynamic string keys from numeric indexed access.
    const stringKey = "0";
    const stringValue = ["ArrayList", "Vector", "Queue", "Deque", "Stack"].includes(name) ? 1 : undefined;
    assertEqual(object[stringKey], stringValue, name + " dynamic string read");
    let stringWriteError;
    try {
        object[stringKey] = 0;
    } catch (exception) {
        stringWriteError = exception;
    }
    assertEqual(stringWriteError !== undefined, true, name + " dynamic string write throws");
    assertEqual(stringWriteError.name, "TypeError", name + " dynamic string write error type");
    assertEqual(object[0], 1, name + " dynamic string write preserves existing slot");

    // Out-of-range writes must follow ASM's property slow path and must not grow the container.
    const index = 100;
    object[index] = 77;
    let threw = false;
    try {
        object[index];
    } catch (exception) {
        threw = true;
    }
    assertEqual(threw, true, name + " out-of-range write did not grow container");
    assertEqual(object[0], 1, name + " out-of-range write preserves existing slot");
}

// Dynamic INT32_MAX keys use the property slow path in both interpreters.
// Keep an Int-tagged key: the existing Vector native path requires GetInt().
const maxIndex = (Math.pow(2, 31) - 1) | 0;
for (const name of ["ArrayList", "Vector"]) {
    const object = create(name);
    let error;
    try {
        object[maxIndex];
    } catch (exception) {
        error = exception;
    }
    assertEqual(error !== undefined, true, name + " dynamic INT32_MAX throws");
    assertEqual(error.name, name === "ArrayList" ? "BusinessError" : "RangeError", name + " dynamic INT32_MAX");
}

// Object.assign shares native property helpers and must keep their existing behavior.
for (const name of ["ArrayList", "Vector", "List", "LinkedList", "PlainArray", "Queue", "Deque", "Stack"]) {
    const object = create(name);
    assertEqual(Object.assign(object, {"0": 17}) === object, true, name + " Object.assign identity");
    assertEqual(object[0], 17, name + " Object.assign updates existing slot");
    if (["ArrayList", "Vector", "Queue", "Deque", "Stack"].includes(name)) {
        const copy = Object.assign({}, object);
        assertEqual(copy[0], 17, name + " Object.assign reads existing slot");
    }
}
// Range calls bypass the ASM fast builtin and must keep the original native behavior.
for (const name of ["Stack", "PlainArray"]) {
    for (const invoke of [
        (object, callback) => object.forEach(callback, undefined, 0, 0),
        (object, callback) => object.forEach.apply(object, [callback, undefined, 0, 0]),
        (object, callback) => Reflect.apply(object.forEach, object, [callback, undefined, 0, 0])
    ]) {
        const object = create(name);
        if (name === "Stack") object.push(43);
        else object.add(1, 43);
        const visited = [];
        invoke(object, (value) => {
            visited.push(value);
            if (visited.length === 1) {
                if (name === "Stack") object.push(44);
                else object.add(2, 44);
            }
        });
        assertEqual(visited, [42, 43], name + " native forEach preserves initial range");
    }
}
// Match FastBuffer.swap16/32/64: C multiplication previously produced Double-tagged indices.
function reverseBufferBytes(buffer, width) {
    for (let group = 0; group < buffer.length / width; group++) {
        const start = width * group;
        const end = start + width - 1;
        for (let offset = 0; offset < width / 2; offset++) {
            const value = buffer[start + offset];
            buffer[start + offset] = buffer[end - offset];
            buffer[end - offset] = value;
        }
    }
}
const BufferConstructor = ArkPrivate.Load(ArkPrivate.FastBuffer);
for (const width of [2, 4, 8]) {
    const buffer = new BufferConstructor(8);
    for (let index = 0; index < buffer.length; index++) buffer[index] = index + 1;
    reverseBufferBytes(buffer, width);
    for (let index = 0; index < buffer.length; index++) {
        const expected = Math.floor(index / width) * width + width - (index % width);
        assertEqual(buffer[index], expected, "FastBuffer swap" + width * 8 + " index " + index);
    }
    // Exercise the same operations after the C interpreter has allocated IC feedback.
    for (let repeat = 0; repeat < 1000; repeat++) reverseBufferBytes(buffer, width);
    for (let index = 0; index < buffer.length; index++) {
        const expected = Math.floor(index / width) * width + width - (index % width);
        assertEqual(buffer[index], expected, "FastBuffer warm swap" + width * 8 + " index " + index);
    }
}
print("container consistency success");
