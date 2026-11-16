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

// Test: sendable prototype method load IC — SharedTypedArray (whitelist-excluded
// type) prototype method load hits the PrototypeHandler after distillation, and
// the answer (method object) stays correct after ArkSteed compilation.
//! METHOD loadProtoMethod
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *loadProtoMethod* 1

let sta = new SendableUint8Array(8);

function loadProtoMethod(arr) {
    let r;
    for (let i = 0; i < 200; i++) { r = arr.fill; }
    return r;
}

print("before compile: " + (loadProtoMethod(sta) !== undefined));
ArkTools.arkSteedCompileSync(loadProtoMethod);
ArkTools.waitJitCompileFinish(loadProtoMethod);
print(ArkTools.isAOTCompiled(loadProtoMethod));
print("after compile: " + (loadProtoMethod(sta) !== undefined));
