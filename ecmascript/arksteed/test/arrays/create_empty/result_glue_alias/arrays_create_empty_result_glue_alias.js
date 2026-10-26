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

//! METHOD Index
//! COUNT_GE AllocateInYoung 1
//! COUNT_GE InitializeEmptyArray 1

function isRange(arg) {
    return false;
}

function isMatrix(arg) {
    return false;
}

function Matrix(arg) {
    this.data = arg;
}

Matrix.prototype.size = function () {
    return [this.data.length];
};

function _createImmutableMatrix(arg) {
    return new Matrix(arg);
}

// Mirrors the mathjs `Index` constructor: the empty-array literal runs while the
// arguments object, the loop counter and an inlined matrix probe stay live.
// The bump allocator reads glue after writing the allocation result, so register
// allocation must keep the fixed return register separate from its glue input.
// Initialization then reuses the result while its restored glue/layout inputs remain live.
// The count is a lower bound because the inlined matrix helpers create their own literals.
function Index(ranges) {
    if (!(this instanceof Index)) {
        throw new SyntaxError('Constructor must be called with the new operator');
    }

    this._dimensions = [];
    this._isScalar = true;

    for (var i = 0, ii = arguments.length; i < ii; i++) {
        var arg = arguments[i];

        if (isRange(arg)) {
            this._dimensions.push(arg);
            this._isScalar = false;
        } else if (Array.isArray(arg) || isMatrix(arg)) {
            var m = _createImmutableMatrix(arg.valueOf());
            this._dimensions.push(m);
            var size = m.size();
            if (size.length !== 1 || size[0] !== 1) {
                this._isScalar = false;
            }
        } else if (typeof arg === 'number') {
            this._dimensions.push(_createImmutableMatrix([arg]));
        } else if (typeof arg === 'string') {
            this._dimensions.push(arg);
        } else {
            throw new TypeError('Dimension must be an Array, Matrix, number, string, or Range');
        }
    }
}

let interpreted = new Index([0, 1, 2], 0);
let interpretedOk = interpreted._dimensions.length === 2 && interpreted._isScalar === false;
print("interpreted:" + interpretedOk);

ArkTools.arkSteedCompileSync(Index);
ArkTools.waitJitCompileFinish(Index);
print("compiled:" + ArkTools.arkSteedIsCompiled(Index));

let compiledOk = true;
let previous = null;
for (let i = 0; i < 4096; i++) {
    let index = new Index([0, 1, 2], 0);
    if (!(index instanceof Index) || index._dimensions.length !== 2 || index._isScalar !== false ||
        index === previous) {
        compiledOk = false;
        break;
    }
    previous = index;
}
print("compiled-instances:" + compiledOk);

// The inline path allocates from the young space buffer, so keep the object reachable
// across a collection to prove the returned array survives with the right contents.
let survivor = new Index([0, 1, 2], 0);
ArkTools.gc();
print("survivor:" + (survivor instanceof Index && survivor._dimensions.length === 2 &&
    survivor._isScalar === false));
