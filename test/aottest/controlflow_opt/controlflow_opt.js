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

function neg(a, b) {
    // ValueCheckNegOverflow
    let len = a - 3
    for (let i = 0; i < len; ++ i) {
        a += i
        let c = -a
    }
    return b
}

function shr(a, b) {
    // Int32UnsignedUpperBoundCheck
    let len = a - 3
    for (let i = 0; i < len; ++ i) {
        a += i
        let c = a >>> len
    }
    return b
}

function mod(a, b) {
    // Int32CheckRightIsZero & RemainderIsNegativeZero
    let len = a - 3
    for (let i = 0; i < len; ++ i) {
        a += i
        let c = a % len
    }
    return b
}

function div(a, b) {
    // Int32DivWithCheck
    let len = a - 3
    for (let i = 1; i <= len; ++ i) {
        let c = a / i
        c += i
    }
    return b
}

function test_switch(a) {
    let b = 0
    switch (a) {
        case 4:
            b = a - 1
            break
        case 5:
            b = a - 2
            break
        case 6: // dead code
            a -= 3
            break
        default: // dead code
            a -= 4
            break
    }
    return b
}

print(neg(5, -4))
print(shr(5, 2))
print(mod(5, 3))
print(div(6, 4))
print(test_switch(4))
print(test_switch(5))
print(test_switch(6))
print(test_switch(7))
