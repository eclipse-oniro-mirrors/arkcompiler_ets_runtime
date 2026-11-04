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

#ifndef OHOS_ARKCOMPILER_SECURITY_GUARD_REPORT_H
#define OHOS_ARKCOMPILER_SECURITY_GUARD_REPORT_H

#include <cstdint>
#include <string>

namespace OHOS::ArkCompiler {

struct AotCompilerArgs;

// Event type reported to SecurityGuard. Both events share the same event id
// (0x0120010A3) and are differentiated by the "type" field.
enum class SgEventType : int32_t {
    COMPILE = 0,
    CODE_SIGN = 1,
};

// Isolated SecurityGuard event reporter for AOT compilation.
//
// The implementation is kept in a dedicated translation unit so that
// SecurityGuard headers (cJSON.h, event_info.h, sg_collect_client.h) are
// only included there. Those headers transitively pull in ipc_types.h,
// which declares `constexpr int INVALID_FD = -1;`. The ets_runtime
// `ecmascript/platform/file.h` header defines a macro with the same name,
// and the macro expansion corrupts the constexpr declaration when both
// headers are visible in the same translation unit. Isolating the
// SecurityGuard includes avoids the collision.
class SecurityGuardReport {
public:
    // Build the event content JSON (bundle identity/package metadata plus
    // "type" and the result code). Pure function: no SecurityGuard SDK
    // dependency, deterministic output for given inputs. Returns an empty
    // string when the JSON serialization fails.
    static std::string BuildEventContent(const AotCompilerArgs &args, int32_t result, SgEventType type);

    // Report the AOT compilation result (type=COMPILE) or the code signing
    // result (type=CODE_SIGN). The AotCompilerArgs carry identity/package
    // metadata; `result` is the corresponding return code.
    static void ReportAotSecurityInfo(const AotCompilerArgs &args, int32_t result, SgEventType type);
};
} // namespace OHOS::ArkCompiler

#endif // OHOS_ARKCOMPILER_SECURITY_GUARD_REPORT_H
