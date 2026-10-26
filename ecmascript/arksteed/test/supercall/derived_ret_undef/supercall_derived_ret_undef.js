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

/*
 * The frontend guarantee, end to end: a derived super constructor with an
 * explicit `return undefined` never crosses the call boundary raw — its
 * bytecode rewrites the value to `return this` (the stricteq guard after the
 * super call) — so the derived arm's object test passes, no TypeError fires,
 * and the caller's this is the object allocated by the base at the bottom of
 * the chain.
 */

//! METHOD FD
//! HAS         FastSuperAllocateThis
//! HAS         CallBaseConstructor
//! HAS         CallDerivedConstructor
//! COUNT       CallRuntime OptSuperCall 1

class Root {}

class PDer extends Root {
    constructor() { super(); return undefined; }
}

class FD extends PDer {
    constructor() { super(); this.tag = "ok"; }
}

new FD();
ArkTools.arkSteedCompileSync(PDer);
ArkTools.arkSteedCompileSync(FD);

const fd = new FD();
print("derived-undef: " + (fd instanceof FD) + " " + fd.tag);
