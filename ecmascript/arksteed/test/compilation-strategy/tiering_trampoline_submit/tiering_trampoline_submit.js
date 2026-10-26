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
 * Deferred compilation submitted from the JSCall bridge (trampoline).
 *
 * A is compiled synchronously, so every later call of B runs under machine
 * code and the interpreter call IC never sees it.  B becomes hot only
 * through those calls, so the tiering check inside the bridge is the only
 * submission path: without it B stays at TieringDecision::ARKSTEED_REQUESTED
 * forever.
 *
 * The caller is force-compiled with arkSteedCompileSync so its installation
 * is deterministic and needs no bounded polling loop.  B is sized above
 * --compiler-max-inline-bytecodes (default 45) so it is not inlined into A,
 * and its body is IC-free (pure int arithmetic plus one lexvar store) so
 * IC-change budget top-ups cannot delay its tier-up.
 */

let bCalls = 0;

function B(x) {
    let r = x;
    r = r + 1;
    r = r + 2;
    r = r + 3;
    r = r + 4;
    r = r + 5;
    r = r + 6;
    r = r + 7;
    r = r + 8;
    r = r + 9;
    r = r + 10;
    r = r + 11;
    r = r + 12;
    r = r + 13;
    r = r + 14;
    r = r + 15;
    r = r + 16;
    bCalls = bCalls + 1;
    return r;
}

function A(callB) {
    if (callB) {
        return B(1);
    }
    return 7;
}

// Phase 1: record mono call-site feedback while B stays cold (3 executions
// are far below B's warmup budget of ~(8 + threshold) full executions).
for (let i = 0; i < 3; i++) {
    A(true);
}

// Phase 2: compile the caller synchronously; its installation is immediate.
ArkTools.arkSteedCompileSync(A);
print("A compiled: " + ArkTools.arkSteedIsCompiled(A));

// Phase 3: A only runs as compiled code now; its calls to B bypass the
// interpreter call IC.  Drive B hot through the bridge.
for (let i = 0; i < 2000; i++) {
    A(true);
}
ArkTools.waitAllJitCompileFinish();

print("B compiled: " + ArkTools.arkSteedIsCompiled(B));
print("B calls: " + bCalls);
