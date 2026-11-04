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

// Interpreter twin of `functions/call/compiled_1` (E6/A5): the same
// computation with the JIT disabled. The expected output is identical to the
// JIT case, so the differential comparison covers the result of the target
// method; the primary oracle stays the specified semantics and the
// hand-computed constants (6.25 and 225), the interpreter agreement is a
// necessary but not sufficient check. The twin carries no JIT annotations:
// with `--compiler-enable-jit=false` nothing is compiled and the E1 entry
// marker never prints, which is exactly the controlled violation the JIT case
// is sensitive to.

//! PARAMS --compiler-enable-jit=false

function foo(x: number): number {
    return x * x;
}

function call_compiled_function_1(x: number): number {
    return foo(x);
}

print(call_compiled_function_1(-2.5));
print(call_compiled_function_1(15));
