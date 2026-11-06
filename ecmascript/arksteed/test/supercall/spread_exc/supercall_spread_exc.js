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
 * An exception thrown by the super constructor body through the compiled
 * spread fast path, mirroring exc_ctor_throw for SUPERCALLTHISRANGE: the
 * CallConstructorSpread vertex invokes the super like a call, so a throw
 * inside it must surface through the compiled derived frame — uncaught
 * propagates unchanged, caught-and-retry rebinds this normally.
 */

//! METHOD ES
//! HAS         CallBaseConstructorSpread
//! HAS         AllocateInYoung
//! HAS         BranchIfFastSpreadArray
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

//! METHOD RetryS
//! HAS         CallBaseConstructorSpread
//! COUNT       CallBaseConstructorSpread 2
//! HAS         AllocateInYoung
//! COUNT       GetObjectSizeFromHClass 2
//! COUNT       ObjSizeTrackingStep 2
//! HAS_NOT     CallCommonStub FastSuperAllocateThis
//! HAS         BranchIfFastSpreadArray
//! COUNT       BranchIfFastSpreadArray 2
//! COUNT       BranchIfStoreArraySpreadAdmitted 2
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 2

class Boom {
    constructor(shouldThrow) {
        if (shouldThrow) {
            throw new RangeError("spread-ctor");
        }
        this.ok = true;
    }
}

class ES extends Boom {
    constructor(arr) {
        super(...arr);
    }
}

class RetryS extends Boom {
    constructor(shouldThrow) {
        let firstMsg = null;
        let thisWasUnbound = false;
        try {
            super(...[shouldThrow]);
            this.unreachable = true;
        } catch (e) {
            firstMsg = e.message;
            thisWasUnbound = (() => {
                try {
                    return this;
                } catch (inner) {
                    return inner instanceof ReferenceError;
                }
            })();
        }
        super(...[false]);
        this.first = firstMsg;
        this.thisUnbound = thisWasUnbound;
    }
}

ArkTools.arkSteedCompileSync(ES);
ArkTools.arkSteedCompileSync(RetryS);

try {
    new ES([true]);
    print("uncaught: NO THROW (bug)");
} catch (e) {
    print("uncaught: " + (e instanceof RangeError) + " " + e.message);
}
print("after-throw: " + new ES([false]).ok);

const r = new RetryS(true);
print("retry: " + r.first + " " + r.thisUnbound + " " + r.ok + " " + (r.unreachable === undefined));
