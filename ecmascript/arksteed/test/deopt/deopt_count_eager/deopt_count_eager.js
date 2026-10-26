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

// Positive deopt evidence (ISSUE-0018): the compiled target is specialized on
// boolean feedback, so a call with a string leaves the generated code through
// an eager deopt. The eager event is counted exactly once (DEOPT_COUNT), while
// the compilation markers and the resume bytecode instruction logged by the
// same event are not. The annotation implies both deopt traces; the ArkSteed
// eager path skips the GC-capable JS stack trace, and its record is matched
// through the normalized `Deoptimize*` spelling.

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD target
//! COMPILE_COUNT target 1
//! EXEC_COUNT_GE *target 1
//! DEOPT_COUNT *Deoptimize* 1

function target(x) {
    return +x;
}

// Warm up with booleans only, so the compiled code guards on that feedback.
for (let i = 0; i < 2000; i++) {
    target(i % 2 === 0);
}
ArkTools.arkSteedCompileSync(target);
print(target(true));
print(target(false));
print(target("12"));
print(ArkTools.arkSteedIsCompiled(target));
print("OK");
