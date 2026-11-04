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

#include "security_guard_report.h"

#ifdef SECURITY_GUARD_ENABLE
#include <memory>

#include "aot_compiler_args.h"
#include "cJSON.h"
#include "ecmascript/log_wrapper.h"
#include "event_info.h"
#include "sg_collect_client.h"

namespace OHOS::ArkCompiler {
namespace {
constexpr int64_t AOT_COMPILE_EVENT_ID = 0x0120010A3;
constexpr const char *AOT_COMPILE_EVENT_VERSION = "1.0";

const char *GetEventType(SgEventType type)
{
    return type == SgEventType::CODE_SIGN ? "codesign" : "compile";
}
} // namespace

std::string SecurityGuardReport::BuildEventContent(const AotCompilerArgs &args, int32_t result,
    SgEventType type)
{
    cJSON *json = cJSON_CreateObject();
    if (json == nullptr) {
        LOG_SA(ERROR) << "ReportAotSecurityInfo: cJSON_CreateObject failed";
        return "";
    }
    cJSON_AddStringToObject(json, "type", GetEventType(type));
    cJSON_AddStringToObject(json, "bundleName", args.bundleName.c_str());
    cJSON_AddStringToObject(json, "moduleName", args.moduleName.c_str());
    cJSON_AddStringToObject(json, "appIdentifier", args.appIdentifier.c_str());
    cJSON_AddNumberToObject(json, "bundleUid", args.bundleUid);
    cJSON_AddNumberToObject(json, "bundleGid", args.bundleGid);
    cJSON_AddNumberToObject(json, "processUid", args.processUid);
    cJSON_AddStringToObject(json, "hapPath", args.hapPath.c_str());
    cJSON_AddStringToObject(json, "outputPath", args.outputPath.c_str());
    cJSON_AddStringToObject(json, "anFileName", args.anFileName.c_str());
    cJSON_AddNumberToObject(json, "bundleType", args.bundleType);
    cJSON_AddNumberToObject(json, "triggerType", args.triggerType);
    cJSON_AddNumberToObject(json, "isScreenOff", static_cast<double>(args.isScreenOff));
    cJSON_AddNumberToObject(json, "isEncryptedBundle", static_cast<double>(args.isEncryptedBundle));
    cJSON_AddNumberToObject(json, "compileResult", result);

    char *jsonStr = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (jsonStr == nullptr) {
        LOG_SA(ERROR) << "ReportAotSecurityInfo: cJSON_PrintUnformatted failed";
        return "";
    }
    std::string content(jsonStr);
    cJSON_free(jsonStr);
    return content;
}

void SecurityGuardReport::ReportAotSecurityInfo(const AotCompilerArgs &args, int32_t result,
    SgEventType type)
{
    // Empty content means the JSON build failed; skip submitting (same
    // behaviour as reporting inline before the extraction).
    std::string content = BuildEventContent(args, result, type);
    if (content.empty()) {
        return;
    }

    auto eventInfo = std::make_shared<OHOS::Security::SecurityGuard::EventInfo>(
        AOT_COMPILE_EVENT_ID, AOT_COMPILE_EVENT_VERSION, content);
    int32_t reportRet = OHOS::Security::SecurityGuard::NativeDataCollectKit::ReportSecurityInfoAsync(eventInfo);
    LOG_SA(INFO) << "ReportAotSecurityInfo: ReportSecurityInfoAsync ret=" << reportRet
                 << " type=" << GetEventType(type) << " result=" << result;
}
} // namespace OHOS::ArkCompiler
#endif // SECURITY_GUARD_ENABLE
