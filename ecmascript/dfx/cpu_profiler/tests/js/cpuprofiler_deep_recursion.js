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

function recurse(n) {
    if (n <= 0) {
        return 0;
    }
    return recurse(n - 1) + 1;
}

// Drive the JS stack to exhaustion so SIGPROF samples land at the deepest
// frames the interpreter allows: there only the alternate signal stack can
// host the profiler handler (plan §6.7, UT-7).
try {
    recurse(10000000);
} catch (e) {
    // RangeError: maximum call stack size exceeded -- expected termination.
}

// Keep the JS thread busy so the sampler keeps firing after the deep phase.
for (let i = 0; i < 50000; i++) {
}
