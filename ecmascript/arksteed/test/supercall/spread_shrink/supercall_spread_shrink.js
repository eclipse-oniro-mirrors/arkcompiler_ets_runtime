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

/*
 * Length-shrunk spread arrays: setting length below the current length
 * keeps the larger elements backing (JSArray::SetCapacity only grows the
 * backing; the shrunk range is hole-filled) while the JSArray length field
 * drops, so an admitted array can have elementsLength > length. Like the
 * interpreter's SUPERCALLSPREAD (argvLen from the JSArray length field, argv
 * buffer iterated by the elements backing), the spread argc must follow the
 * length field — the hole-filled slots past the new length copy as undefined
 * but are never delivered as arguments — while the copy loop runs by the
 * elements backing's own length, mirroring the interpreter's
 * CopyJSArrayToTaggedArrayArgs iteration. A regression that ties the argc to
 * the elements length would inflate argc here (the rest join would gain a
 * trailing empty segment and argc would read 3).
 */

//! METHOD GA
//! HAS         BranchIfFastSpreadArray
//! HAS         CallBaseConstructorSpread
//! HAS         AllocateInYoung
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

class Base {
    constructor(...rest) {
        this.rest = rest.join("/");
        this.argc = rest.length;
    }
}

class GA extends Base {
    constructor(arr) {
        super(...arr);
    }
}

ArkTools.arkSteedCompileSync(GA);

// in-capacity warmup: admitted to the fast path
const w = new GA([1, 2, 3]);
print("warm: " + w.rest + " " + w.argc);

// shrink [1,2,3] to length 2: backing keeps 3 slots, argc must be 2 and
// the stale third slot must not leak as an argument
const shr = [1, 2, 3];
shr.length = 2;
const s = new GA(shr);
print("shrink: " + s.rest + " " + s.argc);

// shrink to 1 and back up to 2 with an explicit store: index 1 holds a
// fresh value, index 2 stays stale behind the length field
const sh2 = [1, 2, 3];
sh2.length = 1;
sh2[1] = "b";
const t = new GA(sh2);
print("re-store: " + t.rest + " " + t.argc);
