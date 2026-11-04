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

class CallInitFrameStateBox {
    base = 40;

    constructor(delta) {
        this.value = this.base + delta;
    }
}

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// the compiled constructor and the compiled same-domain caller keep their own
// compile/entry counters and a whole-process zero-deopt direction. The actual
// construction form of this leaf is annotated in the caller's own dump, and
// the constructor's own dump carries the captured module-binding read: the
// `ldlexvar` of `CallInitFrameStateBox` is a lexical-environment slot load
// (`LoadTaggedField` with the environment as base, offset from
// `GetLexicalEnvSlotOffset`). The constructor writes only the `this.value`
// property, so no `StoreEnvSlot` is claimed. The original interpreted
// construction path, the printed value and every earlier observation stay
// unchanged.
//! COMPILE_COUNT_GE CallInitFrameStateBox 1
//! EXEC_COUNT_GE *CallInitFrameStateBox 1
//! COMPILE_COUNT_GE probeInitConstruction 1
//! EXEC_COUNT_GE *probeInitConstruction 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD probeInitConstruction
//! HAS CallRuntime OptNewObjRange
//! METHOD CallInitFrameStateBox
//! HAS LoadTaggedField

ArkTools.arkSteedCompileSync(CallInitFrameStateBox);
ArkTools.waitJitCompileFinish(CallInitFrameStateBox);

function lazy_deopt_callinit_frame_state() {
    const obj = new CallInitFrameStateBox(2);
    print(obj.value);
}

function probeInitConstruction() {
    return new CallInitFrameStateBox(2);
}
ArkTools.arkSteedCompileSync(probeInitConstruction);
ArkTools.waitJitCompileFinish(probeInitConstruction);

lazy_deopt_callinit_frame_state();

// Bounded silent preparation: the same construction once more through the
// compiled same-domain caller. It prints nothing and changes no earlier
// observation.
probeInitConstruction();
