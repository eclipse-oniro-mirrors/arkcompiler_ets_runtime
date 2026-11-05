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
//! DEOPT_COUNT *Deoptimize* 0

//! METHOD metadataReaderLeft
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *metadataReaderLeft* 1
//! EXEC_COUNT_GE *metadataReaderLeft* 1

//! METHOD metadataReaderRight
//! HAS_NOT CallRuntime LdExternalModuleVarByIndexOnJSFunc
//! COMPILE_COUNT_GE *metadataReaderRight* 1
//! EXEC_COUNT_GE *metadataReaderRight* 1

declare function print(arg: number): void;
declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}
import { metadataReaderLeft } from './reader_left';
import { metadataReaderRight } from './reader_right';
import { bumpLeft } from './value_left';
import { bumpRight } from './value_right';

for (let i = 0; i < 64; i++) {
    metadataReaderLeft();
    metadataReaderRight();
}
ArkTools.arkSteedCompileSync(metadataReaderLeft);
ArkTools.arkSteedCompileSync(metadataReaderRight);
print(metadataReaderLeft());
print(metadataReaderRight());
bumpLeft();
print(metadataReaderRight());
print(metadataReaderLeft());
bumpRight();
print(metadataReaderLeft());
print(metadataReaderRight());
