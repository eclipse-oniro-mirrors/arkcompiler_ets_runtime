/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//! PARAMS --compiler-jit-hotness-threshold=150 --compiler-arksteed-enable-inline=true --compiler-min-inline-call-frequency=0.5 --compiler-max-inline-bytecodes=128 --compiler-arksteed-max-cumulative-inline-bytecode-size=256 --compiler-arksteed-max-graph-nodes-before-inline=1000
//! METHOD readSecond
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! HAS LoadTaggedElement
//! COMPILE_COUNT_GE *readSecond* 1
//! EXEC_COUNT_GE *readSecond* 1
//! DEOPT_COUNT *Deoptimize* 0

import {helper} from './helper';

function readSecond() {
    return helper();
}

for (let i = 0; i < 64; i++) {
    readSecond();
}
print("compiled:" + ArkTools.arkSteedCompileSync(readSecond));
ArkTools.waitJitCompileFinish(readSecond);
print(readSecond());
