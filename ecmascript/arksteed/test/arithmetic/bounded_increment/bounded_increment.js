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

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD lessThan
//! HAS I32Inc
//! HAS_NOT I32IncWithOverflow
function lessThan(value, bound) {
    if (value < bound) {
        ++value;
    }
    return value;
}

//! METHOD notGreaterEqual
//! HAS I32Inc
//! HAS_NOT I32IncWithOverflow
function notGreaterEqual(value, bound) {
    if (value >= bound) {
        return value;
    }
    return ++value;
}

//! METHOD swappedGreaterThan
//! HAS I32Inc
//! HAS_NOT I32IncWithOverflow
function swappedGreaterThan(value, bound) {
    if (bound > value) {
        ++value;
    }
    return value;
}

//! METHOD notSwappedLessEqual
//! HAS I32Inc
//! HAS_NOT I32IncWithOverflow
function notSwappedLessEqual(value, bound) {
    if (bound <= value) {
        return value;
    }
    return ++value;
}

//! METHOD nonStrictBound
//! HAS I32IncWithOverflow
function nonStrictBound(value, bound) {
    if (value <= bound) {
        ++value;
    }
    return value;
}

//! METHOD oppositeEdge
//! HAS I32IncWithOverflow
function oppositeEdge(value, bound) {
    if (value < bound) {
        return value;
    }
    return ++value;
}

//! METHOD replacedInput
//! HAS I32IncWithOverflow
function replacedInput(value, bound, replacement) {
    if (value < bound) {
        value = replacement;
        ++value;
    }
    return value;
}

//! METHOD mergedPaths
//! HAS I32IncWithOverflow
function mergedPaths(value, bound, otherPath) {
    if (value < bound || otherPath) {
        ++value;
    }
    return value;
}

// The entry edge bounds the first increment, but a later loop iteration can
// increment INT32_MAX. An incomplete loop header must retain its overflow guard.
//! METHOD headerIncrement
//! HAS I32IncWithOverflow
function headerIncrement(value, bound) {
    if (value < bound) {
        do {
            ++value;
        } while (value <= bound);
    }
    return value;
}

for (let iteration = 0; iteration < 2000; ++iteration) {
    lessThan(1, 3);
    notGreaterEqual(1, 3);
    swappedGreaterThan(1, 3);
    notSwappedLessEqual(1, 3);
    nonStrictBound(1, 3);
    oppositeEdge(3, 3);
    replacedInput(1, 3, 1);
    mergedPaths(1, 3, false);
    mergedPaths(3, 3, true);
    headerIncrement(1, 3);
}

ArkTools.arkSteedCompileSync(lessThan);
ArkTools.arkSteedCompileSync(notGreaterEqual);
ArkTools.arkSteedCompileSync(swappedGreaterThan);
ArkTools.arkSteedCompileSync(notSwappedLessEqual);
ArkTools.arkSteedCompileSync(nonStrictBound);
ArkTools.arkSteedCompileSync(oppositeEdge);
ArkTools.arkSteedCompileSync(replacedInput);
ArkTools.arkSteedCompileSync(mergedPaths);
ArkTools.arkSteedCompileSync(headerIncrement);

print(ArkTools.arkSteedIsCompiled(lessThan));
print(ArkTools.arkSteedIsCompiled(notGreaterEqual));
print(ArkTools.arkSteedIsCompiled(swappedGreaterThan));
print(ArkTools.arkSteedIsCompiled(notSwappedLessEqual));
print(ArkTools.arkSteedIsCompiled(nonStrictBound));
print(ArkTools.arkSteedIsCompiled(oppositeEdge));
print(ArkTools.arkSteedIsCompiled(replacedInput));
print(ArkTools.arkSteedIsCompiled(mergedPaths));
print(ArkTools.arkSteedIsCompiled(headerIncrement));

const max = 2147483647;
print(lessThan(max - 1, max));
print(notGreaterEqual(max - 1, max));
print(swappedGreaterThan(max - 1, max));
print(notSwappedLessEqual(max - 1, max));
print(lessThan(-2147483648, 0));
print(lessThan(max, max));
print(nonStrictBound(max, max));
print(oppositeEdge(max, max));
print(replacedInput(1, 3, max));
print(mergedPaths(max, max, true));
print(headerIncrement(max - 1, max));
