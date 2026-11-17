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

// Eager frame construction must preserve values with inline and multiword
// liveness sets. The catch probe keeps 80 values live only through the handler;
// coercing the unexpected argument throws after the optimized guard deopts.
// Fixed expectations use sum(seed + i, i=0..79) = 80*seed + 3160.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD e4SmallFrame
//! HAS I32AddWithOverflow
//! COMPILE_COUNT_GE *e4SmallFrame* 1
//! EXEC_COUNT_GE *e4SmallFrame* 1

//! METHOD e4LargeFrame
//! HAS I32AddWithOverflow
//! COMPILE_COUNT_GE *e4LargeFrame* 1
//! EXEC_COUNT_GE *e4LargeFrame* 1

//! METHOD e4CatchFrame
//! HAS I32AddWithOverflow
//! COMPILE_COUNT_GE *e4CatchFrame* 1
//! EXEC_COUNT_GE *e4CatchFrame* 1

//! DEOPT_COUNT_GE *Deoptimize* 3

function e4SmallFrame(x, seed) {
    const a = seed + 1;
    const b = seed + 2;
    return (x + 1) + a + b;
}

function e4LargeFrame(x, seed) {
    const liveValue0 = seed + 0, liveValue1 = seed + 1, liveValue2 = seed + 2, liveValue3 = seed + 3;
    const liveValue4 = seed + 4, liveValue5 = seed + 5, liveValue6 = seed + 6, liveValue7 = seed + 7;
    const liveValue8 = seed + 8, liveValue9 = seed + 9, liveValue10 = seed + 10, liveValue11 = seed + 11;
    const liveValue12 = seed + 12, liveValue13 = seed + 13, liveValue14 = seed + 14, liveValue15 = seed + 15;
    const liveValue16 = seed + 16, liveValue17 = seed + 17, liveValue18 = seed + 18, liveValue19 = seed + 19;
    const liveValue20 = seed + 20, liveValue21 = seed + 21, liveValue22 = seed + 22, liveValue23 = seed + 23;
    const liveValue24 = seed + 24, liveValue25 = seed + 25, liveValue26 = seed + 26, liveValue27 = seed + 27;
    const liveValue28 = seed + 28, liveValue29 = seed + 29, liveValue30 = seed + 30, liveValue31 = seed + 31;
    const liveValue32 = seed + 32, liveValue33 = seed + 33, liveValue34 = seed + 34, liveValue35 = seed + 35;
    const liveValue36 = seed + 36, liveValue37 = seed + 37, liveValue38 = seed + 38, liveValue39 = seed + 39;
    const liveValue40 = seed + 40, liveValue41 = seed + 41, liveValue42 = seed + 42, liveValue43 = seed + 43;
    const liveValue44 = seed + 44, liveValue45 = seed + 45, liveValue46 = seed + 46, liveValue47 = seed + 47;
    const liveValue48 = seed + 48, liveValue49 = seed + 49, liveValue50 = seed + 50, liveValue51 = seed + 51;
    const liveValue52 = seed + 52, liveValue53 = seed + 53, liveValue54 = seed + 54, liveValue55 = seed + 55;
    const liveValue56 = seed + 56, liveValue57 = seed + 57, liveValue58 = seed + 58, liveValue59 = seed + 59;
    const liveValue60 = seed + 60, liveValue61 = seed + 61, liveValue62 = seed + 62, liveValue63 = seed + 63;
    const liveValue64 = seed + 64, liveValue65 = seed + 65, liveValue66 = seed + 66, liveValue67 = seed + 67;
    const liveValue68 = seed + 68, liveValue69 = seed + 69, liveValue70 = seed + 70, liveValue71 = seed + 71;
    const liveValue72 = seed + 72, liveValue73 = seed + 73, liveValue74 = seed + 74, liveValue75 = seed + 75;
    const liveValue76 = seed + 76, liveValue77 = seed + 77, liveValue78 = seed + 78, liveValue79 = seed + 79;
    return (x + 1) +
        liveValue0 + liveValue1 + liveValue2 + liveValue3 + liveValue4 +
        liveValue5 + liveValue6 + liveValue7 + liveValue8 + liveValue9 +
        liveValue10 + liveValue11 + liveValue12 + liveValue13 + liveValue14 +
        liveValue15 + liveValue16 + liveValue17 + liveValue18 + liveValue19 +
        liveValue20 + liveValue21 + liveValue22 + liveValue23 + liveValue24 +
        liveValue25 + liveValue26 + liveValue27 + liveValue28 + liveValue29 +
        liveValue30 + liveValue31 + liveValue32 + liveValue33 + liveValue34 +
        liveValue35 + liveValue36 + liveValue37 + liveValue38 + liveValue39 +
        liveValue40 + liveValue41 + liveValue42 + liveValue43 + liveValue44 +
        liveValue45 + liveValue46 + liveValue47 + liveValue48 + liveValue49 +
        liveValue50 + liveValue51 + liveValue52 + liveValue53 + liveValue54 +
        liveValue55 + liveValue56 + liveValue57 + liveValue58 + liveValue59 +
        liveValue60 + liveValue61 + liveValue62 + liveValue63 + liveValue64 +
        liveValue65 + liveValue66 + liveValue67 + liveValue68 + liveValue69 +
        liveValue70 + liveValue71 + liveValue72 + liveValue73 + liveValue74 +
        liveValue75 + liveValue76 + liveValue77 + liveValue78 + liveValue79;
}

function e4CatchFrame(x, seed) {
    const liveValue0 = seed + 0, liveValue1 = seed + 1, liveValue2 = seed + 2, liveValue3 = seed + 3;
    const liveValue4 = seed + 4, liveValue5 = seed + 5, liveValue6 = seed + 6, liveValue7 = seed + 7;
    const liveValue8 = seed + 8, liveValue9 = seed + 9, liveValue10 = seed + 10, liveValue11 = seed + 11;
    const liveValue12 = seed + 12, liveValue13 = seed + 13, liveValue14 = seed + 14, liveValue15 = seed + 15;
    const liveValue16 = seed + 16, liveValue17 = seed + 17, liveValue18 = seed + 18, liveValue19 = seed + 19;
    const liveValue20 = seed + 20, liveValue21 = seed + 21, liveValue22 = seed + 22, liveValue23 = seed + 23;
    const liveValue24 = seed + 24, liveValue25 = seed + 25, liveValue26 = seed + 26, liveValue27 = seed + 27;
    const liveValue28 = seed + 28, liveValue29 = seed + 29, liveValue30 = seed + 30, liveValue31 = seed + 31;
    const liveValue32 = seed + 32, liveValue33 = seed + 33, liveValue34 = seed + 34, liveValue35 = seed + 35;
    const liveValue36 = seed + 36, liveValue37 = seed + 37, liveValue38 = seed + 38, liveValue39 = seed + 39;
    const liveValue40 = seed + 40, liveValue41 = seed + 41, liveValue42 = seed + 42, liveValue43 = seed + 43;
    const liveValue44 = seed + 44, liveValue45 = seed + 45, liveValue46 = seed + 46, liveValue47 = seed + 47;
    const liveValue48 = seed + 48, liveValue49 = seed + 49, liveValue50 = seed + 50, liveValue51 = seed + 51;
    const liveValue52 = seed + 52, liveValue53 = seed + 53, liveValue54 = seed + 54, liveValue55 = seed + 55;
    const liveValue56 = seed + 56, liveValue57 = seed + 57, liveValue58 = seed + 58, liveValue59 = seed + 59;
    const liveValue60 = seed + 60, liveValue61 = seed + 61, liveValue62 = seed + 62, liveValue63 = seed + 63;
    const liveValue64 = seed + 64, liveValue65 = seed + 65, liveValue66 = seed + 66, liveValue67 = seed + 67;
    const liveValue68 = seed + 68, liveValue69 = seed + 69, liveValue70 = seed + 70, liveValue71 = seed + 71;
    const liveValue72 = seed + 72, liveValue73 = seed + 73, liveValue74 = seed + 74, liveValue75 = seed + 75;
    const liveValue76 = seed + 76, liveValue77 = seed + 77, liveValue78 = seed + 78, liveValue79 = seed + 79;
    try {
        return x + 1;
    } catch (error) {
        return (
            liveValue0 + liveValue1 + liveValue2 + liveValue3 + liveValue4 +
            liveValue5 + liveValue6 + liveValue7 + liveValue8 + liveValue9 +
            liveValue10 + liveValue11 + liveValue12 + liveValue13 + liveValue14 +
            liveValue15 + liveValue16 + liveValue17 + liveValue18 + liveValue19 +
            liveValue20 + liveValue21 + liveValue22 + liveValue23 + liveValue24 +
            liveValue25 + liveValue26 + liveValue27 + liveValue28 + liveValue29 +
            liveValue30 + liveValue31 + liveValue32 + liveValue33 + liveValue34 +
            liveValue35 + liveValue36 + liveValue37 + liveValue38 + liveValue39 +
            liveValue40 + liveValue41 + liveValue42 + liveValue43 + liveValue44 +
            liveValue45 + liveValue46 + liveValue47 + liveValue48 + liveValue49 +
            liveValue50 + liveValue51 + liveValue52 + liveValue53 + liveValue54 +
            liveValue55 + liveValue56 + liveValue57 + liveValue58 + liveValue59 +
            liveValue60 + liveValue61 + liveValue62 + liveValue63 + liveValue64 +
            liveValue65 + liveValue66 + liveValue67 + liveValue68 + liveValue69 +
            liveValue70 + liveValue71 + liveValue72 + liveValue73 + liveValue74 +
            liveValue75 + liveValue76 + liveValue77 + liveValue78 + liveValue79);
    }
}

// Warm all feedback with small integers and leave compilation explicit.
for (let i = 0; i < 2000; ++i) {
    e4SmallFrame(i, i & 7);
    e4LargeFrame(i, i & 7);
    e4CatchFrame(i, i & 7);
}

ArkTools.arkSteedCompileSync(e4SmallFrame);
ArkTools.arkSteedCompileSync(e4LargeFrame);
ArkTools.arkSteedCompileSync(e4CatchFrame);
print("small-compiled:" + ArkTools.arkSteedIsCompiled(e4SmallFrame));
print("large-compiled:" + ArkTools.arkSteedIsCompiled(e4LargeFrame));
print("catch-compiled:" + ArkTools.arkSteedIsCompiled(e4CatchFrame));

print("small-normal:" + e4SmallFrame(7, 5));
print("large-normal:" + e4LargeFrame(7, 5));
print("catch-normal:" + e4CatchFrame(7, 5));

// Each overflow probe reaches an int32-specialized operation with a tagged int.
print("small-overflow:" + e4SmallFrame(2147483647, 5));
print("large-overflow:" + e4LargeFrame(2147483647, 5));

let coercions = 0;
const throwingValue = {
    valueOf() {
        ++coercions;
        throw 99;
    }
};
print("catch-restored:" + e4CatchFrame(throwingValue, 5));
print("coercions:" + coercions);
