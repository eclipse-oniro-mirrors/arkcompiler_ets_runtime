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

//! PARAMS --compiler-jit-hotness-threshold=10000
//! METHOD read
//! HAS LoadTaggedField             # The env-slot half of the read is an actual LoadTaggedField on the env object.
//! COUNT_LE LoadTaggedField 6      # Each read currently lowering TDZ checks and a slot load.
//! COMPILE_COUNT_GE *read* 1
//! EXEC_COUNT_GE *read* 1
//! DEOPT_COUNT *Deoptimize* 0

function createReader()
{
    let captured = 7;

    function read()
    {
        return captured + captured;
    }

    return read;
}

const read = createReader();
for (let i = 0; i < 1000; i++) {
    read();
}

ArkTools.arkSteedCompileSync(read);
ArkTools.waitJitCompileFinish(read);

let ok = ArkTools.isAOTCompiled(read);
ok = ok && read() === 14;

print(ok ? "PASS" : "FAIL");
