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

// Arrow target (hole PROTO_OR_DYNCLASS) plus a stale named-load IC: the migration
// after warmup leaves the load IC on the pre-migration HClass while the INSTANCEOF
// gate reads the post-migration one.

const arrowTarget = (a: number) => a + 1;
arrowTarget.x = 1;

function check(o: Object, c: Object): number {
    try {
        let v = (c as any).x;
        return o instanceof c ? v : 0;
    } catch (e) {
        return 2;
    }
}

for (let i = 0; i < 50; i++) {
    check(new Object(), arrowTarget);
}

arrowTarget.y = 2;

ArkTools.arkSteedCompileSync(check);

print("r=" + check(new Object(), arrowTarget));
