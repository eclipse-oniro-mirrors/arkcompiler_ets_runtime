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

//! PARAMS --compiler-enable-baselinejit=true --compiler-baselinejit-hotness-threshold=1 --compiler-jit-hotness-threshold=60000

// Baseline writes the same weak-constructor feedback consumed by ArkSteed.
//! METHOD      baseline_feedback_check
//! HAS         BranchIfReferenceEqual
//! HAS         HasInPrototypeChain
//! HAS         CallCommonStub InstanceofJit

class BaselineFeedbackPoint {}
class BaselineFeedbackCustom {}

let baselineCustomCalls = 0;
Object.defineProperty(BaselineFeedbackCustom, Symbol.hasInstance, {
    value: function (value: Object): boolean {
        baselineCustomCalls++;
        return true;
    }
});

function baseline_feedback_check(value: Object): number {
    return value instanceof BaselineFeedbackPoint ? 1 : 0;
}

function baseline_custom_check(value: Object): number {
    return value instanceof BaselineFeedbackCustom ? 1 : 0;
}

for (let i = 0; i < 50; i++) {
    baseline_feedback_check(new BaselineFeedbackPoint());
    baseline_custom_check({});
}

let baselineSupported = ArkTools.compileBaseline(baseline_feedback_check) &&
                        ArkTools.compileBaseline(baseline_custom_check);
if (baselineSupported) {
    // Make baseline the final writer before the ArkSteed compilation below.
    if (baseline_feedback_check(new BaselineFeedbackPoint()) !== 1) {
        throw new Error("baseline ordinary instanceof failed");
    }
    let callsBeforeBaselineCheck = baselineCustomCalls;
    if (baseline_custom_check({}) !== 1 || baselineCustomCalls <= callsBeforeBaselineCheck) {
        throw new Error("baseline custom @@hasInstance call failed");
    }
}

ArkTools.arkSteedCompileSync(baseline_feedback_check);
print("arkSteedHit=" + baseline_feedback_check(new BaselineFeedbackPoint()));
print("arkSteedMiss=" + baseline_feedback_check({}));
