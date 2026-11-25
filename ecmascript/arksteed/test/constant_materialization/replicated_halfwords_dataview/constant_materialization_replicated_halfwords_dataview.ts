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

// Regression mirror of the test262 cases that exposed the ARM64
// TryReplicateHWords bug (DataView/prototype/getFloat64/{toindex-byteoffset,
// return-values-custom-offset, return-values}): the buffer is filled with
// 0x43/0x44 bytes so getFloat64 returns a double whose bit pattern repeats a
// halfword with an unencodable logical-immediate replication. Comparing the
// result against the expected literal forces the F64 compare path (the
// literal's static type is DOUBLE), materializing the poison constant via
// ArkSteedAssembler::Move(int64) -> AssemblerAarch64::Mov during JIT
// compilation. Before the fix this aborted the VM; the comparisons must
// now hold bit-exactly.
//   A = 10846169068898440     -> 0x4343444443434444
//   B = 747563348316297500000 -> 0x4444434344444343
//   C = 10843961455708296     -> 0x4343434343434444 (wrong-value control)

//! COMPILE_COUNT_GE *checkBigEndian* 1
//! EXEC_COUNT_GE *checkBigEndian* 1
//! COMPILE_COUNT_GE *checkCustomOffset* 1
//! EXEC_COUNT_GE *checkCustomOffset* 1
//! COMPILE_COUNT_GE *checkLittleEndian* 1
//! EXEC_COUNT_GE *checkLittleEndian* 1
//! COMPILE_COUNT_GE *checkSecondPattern* 1
//! EXEC_COUNT_GE *checkSecondPattern* 1
//! COMPILE_COUNT_GE *checkWrongConstant* 1
//! EXEC_COUNT_GE *checkWrongConstant* 1
//! DEOPT_COUNT *Deoptimize* 0

function checkBigEndian(): boolean {
    const buffer = new ArrayBuffer(8);  // 8: bytes in one Float64 value.
    const view = new DataView(buffer);
    const bytes = [0x43, 0x43, 0x44, 0x44, 0x43, 0x43, 0x44, 0x44];  // 0x43, 0x44: bytes of A in big-endian order.
    for (let i = 0; i < 8; i++) {  // 8: bytes in one Float64 value.
        view.setUint8(i, bytes[i]);
    }
    return view.getFloat64(0) === 10846169068898440.0;  // 10846169068898440.0: expected pattern A.
}

function checkCustomOffset(): boolean {
    const buffer = new ArrayBuffer(16);  // 16: buffer capacity for an 8-byte value at a nonzero offset.
    const view = new DataView(buffer);
    const bytes = [0x43, 0x43, 0x44, 0x44, 0x43, 0x43, 0x44, 0x44];  // 0x43, 0x44: bytes of A in big-endian order.
    for (let i = 0; i < 8; i++) {  // 8: bytes in one Float64 value.
        view.setUint8(i + 2, bytes[i]);  // 2: byte offset from the start of the buffer.
    }
    return view.getFloat64(2) === 10846169068898440.0;  // 2: byte offset. 10846169068898440.0: expected pattern A.
}

function checkLittleEndian(): boolean {
    const buffer = new ArrayBuffer(8);  // 8: bytes in one Float64 value.
    const view = new DataView(buffer);
    const bytes = [0x44, 0x44, 0x43, 0x43, 0x44, 0x44, 0x43, 0x43];  // 0x43, 0x44: bytes of A in little-endian order.
    for (let i = 0; i < 8; i++) {  // 8: bytes in one Float64 value.
        view.setUint8(i, bytes[i]);
    }
    return view.getFloat64(0, true) === 10846169068898440.0;  // 10846169068898440.0: expected pattern A.
}

function checkSecondPattern(): boolean {
    const buffer = new ArrayBuffer(8);  // 8: bytes in one Float64 value.
    const view = new DataView(buffer);
    const bytes = [0x44, 0x44, 0x43, 0x43, 0x44, 0x44, 0x43, 0x43];  // 0x43, 0x44: bytes of B in big-endian order.
    for (let i = 0; i < 8; i++) {  // 8: bytes in one Float64 value.
        view.setUint8(i, bytes[i]);
    }
    return view.getFloat64(0) === 747563348316297500000.0;  // 747563348316297500000.0: expected pattern B.
}

function checkWrongConstant(): boolean {
    const buffer = new ArrayBuffer(8);  // 8: bytes in one Float64 value.
    const view = new DataView(buffer);
    const bytes = [0x43, 0x43, 0x44, 0x44, 0x43, 0x43, 0x44, 0x44];  // 0x43, 0x44: bytes of A in big-endian order.
    for (let i = 0; i < 8; i++) {  // 8: bytes in one Float64 value.
        view.setUint8(i, bytes[i]);
    }
    return view.getFloat64(0) === 10843961455708296.0;  // 10843961455708296.0: pattern C, which must differ from A.
}

ArkTools.arkSteedCompileSync(checkBigEndian);
ArkTools.arkSteedCompileSync(checkCustomOffset);
ArkTools.arkSteedCompileSync(checkLittleEndian);
ArkTools.arkSteedCompileSync(checkSecondPattern);
ArkTools.arkSteedCompileSync(checkWrongConstant);

print(checkBigEndian());
print(checkCustomOffset());
print(checkLittleEndian());
print(checkSecondPattern());
print(checkWrongConstant());