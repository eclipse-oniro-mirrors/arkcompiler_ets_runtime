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

// Warm templates without automatic compilation; use ordinary GC explicitly.
//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=0 --compiler-jit-call-threshold=60000 --compiler-enable-baselinejit=false --enable-cmc-gc=false

//! METHOD create32
//! HAS_NOT BranchIfInt32Compare
//! COUNT Phi 1
//! COUNT CallRuntime AllocateInYoung 1
//! COUNT CanAllocateInYoung 1
//! COUNT BranchIfTrue 1
//! COUNT LoadTaggedField 1 # Load the literal HClass before allocation.
//! HAS_NOT LoadInt32Field
//! HAS_NOT CallRuntime CreateObjectWithBuffer
//! COUNT AllocateInYoung 1
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 1
//! BYTECODE_COUNT createobjectwithbuffer 0x0,* 1
//! HAS HeapConstant
//! COUNT InitializeObjectWithBuffer 1
//! HAS_NOT CallCommonStub CreateObjectHavingMethod
//! CODEGEN_COUNT * InitializeObjectWithBuffer * 1
//! CODEGEN_COUNT AllocateInYoung bump allocation 1

function create32() {
    return {
        p0: 0.25, p1: 1.25, p2: 2.25, p3: 3.25, p4: 4.25, p5: 5.25, p6: 6.25, p7: 7.25,
        p8: 8.25, p9: 9.25, p10: 10.25, p11: 11.25, p12: 12.25, p13: 13.25, p14: 14.25, p15: 15.25,
        p16: 16.25, p17: 17.25, p18: 18.25, p19: 19.25, p20: 20.25, p21: 21.25, p22: 22.25, p23: 23.25,
        p24: 24.25, p25: 25.25, p26: 26.25, p27: 27.25, p28: 28.25, p29: 29.25, p30: 30.25, p31: 31.25
    };
}

//! METHOD create33
//! HAS_NOT CallRuntime CreateObjectWithBuffer
//! COUNT AllocateInYoung 1
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 1
//! BYTECODE_COUNT createobjectwithbuffer 0x0,* 1
//! HAS HeapConstant
//! COUNT InitializeObjectWithBuffer 1
//! HAS_NOT CallCommonStub CreateObjectHavingMethod
//! CODEGEN_COUNT * InitializeObjectWithBuffer * 1
//! CODEGEN_COUNT AllocateInYoung bump allocation 1

function create33() {
    return {
        p0: 0.25, p1: 1.25, p2: 2.25, p3: 3.25, p4: 4.25, p5: 5.25, p6: 6.25, p7: 7.25,
        p8: 8.25, p9: 9.25, p10: 10.25, p11: 11.25, p12: 12.25, p13: 13.25, p14: 14.25, p15: 15.25,
        p16: 16.25, p17: 17.25, p18: 18.25, p19: 19.25, p20: 20.25, p21: 21.25, p22: 22.25, p23: 23.25,
        p24: 24.25, p25: 25.25, p26: 26.25, p27: 27.25, p28: 28.25, p29: 29.25, p30: 30.25, p31: 31.25,
        p32: 32.25
    };
}

//! METHOD create64
//! HAS_NOT CallRuntime CreateObjectWithBuffer
//! COUNT AllocateInYoung 1
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 1
//! BYTECODE_COUNT createobjectwithbuffer 0x0,* 1
//! HAS HeapConstant
//! COUNT InitializeObjectWithBuffer 1
//! HAS_NOT CallCommonStub CreateObjectHavingMethod
//! CODEGEN_COUNT * InitializeObjectWithBuffer * 1
//! CODEGEN_COUNT AllocateInYoung bump allocation 1

function create64() {
    return {
        p0: 0.25, p1: 1.25, p2: 2.25, p3: 3.25, p4: 4.25, p5: 5.25, p6: 6.25, p7: 7.25,
        p8: 8.25, p9: 9.25, p10: 10.25, p11: 11.25, p12: 12.25, p13: 13.25, p14: 14.25, p15: 15.25,
        p16: 16.25, p17: 17.25, p18: 18.25, p19: 19.25, p20: 20.25, p21: 21.25, p22: 22.25, p23: 23.25,
        p24: 24.25, p25: 25.25, p26: 26.25, p27: 27.25, p28: 28.25, p29: 29.25, p30: 30.25, p31: 31.25,
        p32: 32.25, p33: 33.25, p34: 34.25, p35: 35.25, p36: 36.25, p37: 37.25, p38: 38.25, p39: 39.25,
        p40: 40.25, p41: 41.25, p42: 42.25, p43: 43.25, p44: 44.25, p45: 45.25, p46: 46.25, p47: 47.25,
        p48: 48.25, p49: 49.25, p50: 50.25, p51: 51.25, p52: 52.25, p53: 53.25, p54: 54.25, p55: 55.25,
        p56: 56.25, p57: 57.25, p58: 58.25, p59: 59.25, p60: 60.25, p61: 61.25, p62: 62.25, p63: 63.25
    };
}

// Distinct fractional values expose truncated, reordered or uninitialized slots.
function hasValues(object, count, mutated = false) {
    let keys = Object.keys(object);
    if (keys.length !== count || Object.getPrototypeOf(object) !== Object.prototype) {
        return false;
    }
    for (let i = 0; i < count; ++i) {
        if (keys[i] !== "p" + i) {
            return false;
        }
        let value = object[keys[i]];
        if (mutated && i === count - 1) {
            if (value === null || typeof value !== "object" || value.marker !== count) {
                return false;
            }
        } else if (value !== i + 0.25) {
            return false;
        }
    }
    return true;
}

function checkCapacity(factory, count) {
    let before;
    for (let i = 0; i < 32; ++i) {
        before = factory();
    }
    print(count + "-interpreted:" + (!ArkTools.arkSteedIsCompiled(factory) && hasValues(before, count)));
    ArkTools.arkSteedCompileAsync(factory);
    ArkTools.waitJitCompileFinish(factory);
    print(count + "-compiled:" + ArkTools.arkSteedIsCompiled(factory));

    // Collect after installation, before the first execution of the compiled body.
    ArkTools.forceFullGC();
    let first = factory();
    let second = factory();
    print(count + "-values-and-identity:" + (hasValues(first, count) && hasValues(second, count) &&
        hasValues(before, count) && first !== second && first !== before && second !== before));

    // The last slot crosses the old limit for both larger templates. Keep its
    // heap value alive only through the returned object, including after promotion.
    // Capacity failure may eagerly deopt; retained and fresh values must remain correct.
    first["p" + (count - 1)] = { marker: count };
    function retainedAndFresh() {
        let fresh = factory();
        return hasValues(first, count, true) && hasValues(second, count) && hasValues(before, count) &&
            hasValues(fresh, count) && fresh !== first && fresh !== second && fresh !== before;
    }
    print(count + "-independent:" + retainedAndFresh());
    ArkTools.GC.startGC("young", undefined, true);
    print(count + "-young-gc:" + retainedAndFresh());
    ArkTools.forceFullGC();
    print(count + "-full-gc:" + retainedAndFresh());

    first["p" + (count - 1)] = { marker: count };
    ArkTools.GC.startGC("young", undefined, true);
    print(count + "-retained-store:" + retainedAndFresh());
}

checkCapacity(create32, 32);
checkCapacity(create33, 33);
checkCapacity(create64, 64);
