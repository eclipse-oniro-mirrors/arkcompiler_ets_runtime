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
 * FORWARDALLARGS with builtin super constructors. The builtin never gets an
 * arksteed code entry, so the not-compiled branch routes the call through
 * the generic [[Construct]] trampoline with the caller-frame argument copy -
 * the trampoline clobbers every caller-saved register, which is why the
 * reservation after the call is freed by restoring the frame bottom, never
 * by recomputing the count from a register (recomputing from a clobbered
 * register drifts rsp and crashes on the next push; this file is the
 * regression lock for that).
 *
 * Behavioral checks mirror builtin_super (the plain super() variant): Array
 * elements survive, Error keeps its message, Map is usable, an Object
 * subclass still constructs - but every argument travels through the
 * forwarded caller frame, so the argc fidelity of the copy loop is also
 * under test.
 */

//! METHOD ExtArray
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorForwardAllArgs
//! HAS         CallDerivedConstructorForwardAllArgs
//! HAS         ActualArgc
//! COUNT       CallRuntime OptSuperCall* 1

//! METHOD ExtObject
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorForwardAllArgs
//! HAS         CallDerivedConstructorForwardAllArgs
//! COUNT       CallRuntime OptSuperCall* 1

class ExtArray extends Array {
}

class ExtError extends Error {
}

class ExtMap extends Map {
}

class ExtObject extends Object {
}

ArkTools.arkSteedCompileSync(ExtArray);
ArkTools.arkSteedCompileSync(ExtError);
ArkTools.arkSteedCompileSync(ExtMap);
ArkTools.arkSteedCompileSync(ExtObject);

const a = new ExtArray(1, 2, 3);
print("array: " + a.length + " " + a[0] + " " + a[1] + " " + a[2] + " " + (a instanceof ExtArray));

const e = new ExtError("boom");
print("error: " + (e instanceof ExtError) + " " + (e.message === "boom"));

const mp = new ExtMap([[1, 11], [2, 22]]);
print("map: " + mp.size + " " + mp.get(2) + " " + (mp instanceof ExtMap));

const o = new ExtObject();
o.tag = 7;
print("object: " + o.tag + " " + (o instanceof ExtObject));
