/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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
 * @tc.name: Atomics.wait
 * @tc.desc: test Atomics.wait
 * @tc.type: FUNC
 * @tc.require: issue#11914
 */

{
    const v21 = new SharedArrayBuffer(32);
    const v22 = new BigInt64Array(v21);
    Atomics.or(v22, Int16Array, false);
    assert_equal(v22.toString(), "0,0,0,0");
    assert_equal(Atomics.wait(v22, false, true), "not-equal");
}

{
    let buf1 = new SharedArrayBuffer(8);
    let view1 = new BigInt64Array(buf1);
    try {
        Atomics.wait(view1, 0, 0x41414141);
    } catch (e) {
        assert_equal(e instanceof TypeError, true);
    }
}

{
    let buf2 = new SharedArrayBuffer(8);
    let view2 = new BigInt64Array(buf2);
    try {
        Atomics.wait(view2, 0, {});
    } catch(e) {
        assert_equal(e instanceof SyntaxError, true);
    }
}

{
    let buf = new SharedArrayBuffer(8);
    let view = new BigInt64Array(buf);
    assert_equal(Atomics.wait(view, 0, true), "not-equal");
}
{
    const evil = {
        valueOf() {
            ArkTools.forceFullGC();
            return 0x41;
        },
        [Symbol.toPrimitive]() {
            ArkTools.forceFullGC();
            return 1n;
        }
    }
    const view = new BigInt64Array(1)
    let done = false;
    Atomics.add(view, 0, evil);
    Atomics.compareExchange(view, 0, 0n, evil);
    done = true;
    assert_equal(done, true);
}

/*
 * @tc.name: Atomics.compareExchange
 * @tc.desc: valueOf of expected/replacement may trigger GC which moves the
 *          ArrayBuffer backing store. The atomic operation must use the
 *          re-fetched data pointer instead of a stale one, and must write to
 *          the right index without corrupting neighboring elements.
 * @tc.type: FUNC
 */
{
    const makeEvil = function(value) {
        return {
            valueOf() {
                // Allocate garbage to add memory pressure, then force GC so the
                // typed array backing store may be moved/reclaimed.
                for (let i = 0; i < 100; i++) {
                    let garbage = new Array(100).fill(i);
                }
                ArkTools.forceFullGC();
                return value;
            }
        };
    };

    const intCtors = [Int8Array, Uint8Array, Int16Array, Uint16Array, Int32Array, Uint32Array];
    for (const ctor of intCtors) {
        for (let round = 0; round < 10; round++) {
            const view = new ctor(8);
            view[3] = 0x2A;
            // Both ToInteger(expected) and ToInteger(replacement) run user code
            // which triggers GC between the index validation and the write.
            const old = Atomics.compareExchange(view, 3, makeEvil(0x2A), makeEvil(0x41));
            assert_equal(old, 0x2A);
            assert_equal(view[3], 0x41);
            // Neighboring elements must not be touched by the atomic write.
            assert_equal(view[2], 0);
            assert_equal(view[4], 0);
            assert_equal(view[7], 0);
        }
    }
}

/*
 * @tc.name: Atomics.exchange
 * @tc.desc: GC triggered inside valueOf of the value argument must not leave a
 *          stale data pointer used by the atomic read-modify-write.
 * @tc.type: FUNC
 */
{
    const makeEvil = function(value) {
        return {
            valueOf() {
                ArkTools.forceFullGC();
                return value;
            }
        };
    };

    const intCtors = [Int8Array, Uint8Array, Int16Array, Uint16Array, Int32Array, Uint32Array];
    for (const ctor of intCtors) {
        const view = new ctor(4);
        view[1] = 0x11;
        assert_equal(Atomics.exchange(view, 1, makeEvil(0x22)), 0x11);
        assert_equal(view[1], 0x22);
        assert_equal(view[0], 0);
        assert_equal(view[2], 0);
    }
}

/*
 * @tc.name: Atomics.add/and/or/xor/sub
 * @tc.desc: read-modify-write ops with a value argument whose valueOf triggers
 *          GC must operate on the relocated buffer and produce correct results.
 * @tc.type: FUNC
 */
{
    const makeEvil = function(value) {
        return {
            valueOf() {
                ArkTools.forceFullGC();
                return value;
            }
        };
    };

    const view = new Int32Array(4);
    view[2] = 0x0F;
    assert_equal(Atomics.add(view, 2, makeEvil(1)), 0x0F);
    assert_equal(view[2], 0x10);
    assert_equal(Atomics.and(view, 2, makeEvil(0x18)), 0x10);
    assert_equal(view[2], 0x10);
    assert_equal(Atomics.or(view, 2, makeEvil(0x02)), 0x10);
    assert_equal(view[2], 0x12);
    assert_equal(Atomics.xor(view, 2, makeEvil(0xFF)), 0x12);
    assert_equal(view[2], 0xED);
    assert_equal(Atomics.sub(view, 2, makeEvil(0xED)), 0xED);
    assert_equal(view[2], 0);
}

/*
 * @tc.name: Atomics.compareExchange
 * @tc.desc: failed compareExchange (expected mismatch) with GC triggered inside
 *          valueOf must still return the current value and write nothing.
 * @tc.type: FUNC
 */
{
    const makeEvil = function(value) {
        return {
            valueOf() {
                ArkTools.forceFullGC();
                return value;
            }
        };
    };

    const view = new Uint16Array(4);
    view[0] = 0x1234;
    const old = Atomics.compareExchange(view, 0, makeEvil(0x5678), makeEvil(0x9ABC));
    assert_equal(old, 0x1234);
    assert_equal(view[0], 0x1234);
}

/*
 * @tc.name: Atomics.compareExchange
 * @tc.desc: BigInt64Array/BigUint64Array path with GC triggered during
 *          ToBigInt of the replacement argument.
 * @tc.type: FUNC
 */
{
    const makeEvilBigInt = function(value) {
        return {
            valueOf() {
                ArkTools.forceFullGC();
                return value;
            }
        };
    };

    const i64 = new BigInt64Array(4);
    i64[1] = 100n;
    assert_equal(Atomics.compareExchange(i64, 1, makeEvilBigInt(100n), makeEvilBigInt(200n)), 100n);
    assert_equal(i64[1], 200n);
    assert_equal(i64[0], 0n);
    assert_equal(i64[2], 0n);

    const u64 = new BigUint64Array(4);
    u64[3] = 1n;
    assert_equal(Atomics.exchange(u64, 3, makeEvilBigInt(2n)), 1n);
    assert_equal(u64[3], 2n);
    assert_equal(u64[2], 0n);
}

test_end();