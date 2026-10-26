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

//! METHOD nullish_literal_greater
//! COMPILE_COUNT_GE *nullish_literal_greater* 1
//! EXEC_COUNT_GE *nullish_literal_greater* 1
function nullish_literal_greater(): boolean {
    return null > 1;
}

//! METHOD nullish_literal_lesseq
//! COMPILE_COUNT_GE *nullish_literal_lesseq* 1
//! EXEC_COUNT_GE *nullish_literal_lesseq* 1
function nullish_literal_lesseq(): boolean {
    return null <= null;
}

//! METHOD undefined_literal_greater
//! COMPILE_COUNT_GE *undefined_literal_greater* 1
//! EXEC_COUNT_GE *undefined_literal_greater* 1
function undefined_literal_greater(): boolean {
    return 1 > undefined;
}

//! METHOD greater_than_null
//! COMPILE_COUNT_GE *greater_than_null* 1
//! EXEC_COUNT_GE *greater_than_null* 1
function greater_than_null(value: number): boolean {
    return value > null;
}

//! METHOD loose_equal_null
//! HAS TaggedEqual
//! HAS_NOT CallCommonStub Equal
//! HAS_NOT CallCommonStub NotEqual
//! COMPILE_COUNT_GE *loose_equal_null* 1
//! EXEC_COUNT_GE *loose_equal_null* 1
function loose_equal_null(value: object | null | undefined): boolean {
    return value == null;
}

//! METHOD loose_not_equal_null
//! HAS TaggedEqual
//! HAS_NOT CallCommonStub Equal
//! HAS_NOT CallCommonStub NotEqual
//! COMPILE_COUNT_GE *loose_not_equal_null* 1
//! EXEC_COUNT_GE *loose_not_equal_null* 1
function loose_not_equal_null(value: object | null | undefined): boolean {
    return value != null;
}

//! METHOD nullish_compare_matrix
//! COMPILE_COUNT_GE *nullish_compare_matrix* 1
//! EXEC_COUNT_GE *nullish_compare_matrix* 1
function nullish_compare_matrix(): void {
    // Greater / greater-equal with nullish operands.
    print(null > 1 ? 1 : 0);                 // 0 > 1
    print(null > null ? 1 : 0);              // 0 > 0
    print(null > undefined ? 1 : 0);         // 0 > NaN
    print(undefined > undefined ? 1 : 0);    // NaN > NaN
    print(1 > null ? 1 : 0);                 // 1 > 0
    print(1 > undefined ? 1 : 0);            // 1 > NaN
    print(undefined > 1 ? 1 : 0);            // NaN > 1
    print(null > -1 ? 1 : 0);                // 0 > -1
    print(null >= 1 ? 1 : 0);                // 0 >= 1
    print(1 >= null ? 1 : 0);                // 1 >= 0
    print(null >= null ? 1 : 0);             // 0 >= 0
    print(undefined >= undefined ? 1 : 0);   // NaN >= NaN
    // Less / less-equal with nullish operands.
    print(null < 1 ? 1 : 0);                 // 0 < 1
    print(1 < null ? 1 : 0);                 // 1 < 0
    print(null < null ? 1 : 0);              // 0 < 0
    print(undefined < 1 ? 1 : 0);            // NaN < 1
    print(1 < undefined ? 1 : 0);            // 1 < NaN
    print(null <= 1 ? 1 : 0);                // 0 <= 1
    print(1 <= null ? 1 : 0);                // 1 <= 0
    print(null <= null ? 1 : 0);             // 0 <= 0
    print(undefined <= undefined ? 1 : 0);   // NaN <= NaN
    // Non-number operands still coerce through ToNumber.
    print(null > "1" ? 1 : 0);               // 0 > ToNumber("1") = 1
    print(null > true ? 1 : 0);              // 0 > ToNumber(true) = 1
    print("1" > undefined ? 1 : 0);          // 1 > NaN
    print(true > undefined ? 1 : 0);         // 1 > NaN
}

declare function print(arg: number): string;

class Box {
    value: number = 0;
}

let box = new Box();

ArkTools.arkSteedCompileSync(nullish_literal_greater);
ArkTools.arkSteedCompileSync(nullish_literal_lesseq);
ArkTools.arkSteedCompileSync(undefined_literal_greater);
ArkTools.arkSteedCompileSync(greater_than_null);
ArkTools.arkSteedCompileSync(loose_equal_null);
ArkTools.arkSteedCompileSync(loose_not_equal_null);
ArkTools.arkSteedCompileSync(nullish_compare_matrix);

print(nullish_literal_greater() ? 1 : 0);
print(nullish_literal_lesseq() ? 1 : 0);
print(undefined_literal_greater() ? 1 : 0);
print(greater_than_null(1) ? 1 : 0);
print(greater_than_null(0) ? 1 : 0);
print(greater_than_null(-1) ? 1 : 0);
print(loose_equal_null(null) ? 1 : 0);
print(loose_equal_null(undefined) ? 1 : 0);
print(loose_equal_null(box) ? 1 : 0);
print(loose_not_equal_null(box) ? 1 : 0);
print(loose_not_equal_null(null) ? 1 : 0);

nullish_compare_matrix();

//! DEOPT_COUNT *Deoptimize* 0
