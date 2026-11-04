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

#ifdef SECURITY_GUARD_ENABLE

#include <cstdint>
#include <string>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "aot_compiler_args.h"
#include "aot_compiler_error_utils.h"
#include "security_guard_report.h"

using namespace testing::ext;

namespace OHOS::ArkCompiler {
namespace {
constexpr int32_t TEST_BUNDLE_UID = 20020079;
constexpr int32_t TEST_PROCESS_UID = 3060;

AotCompilerArgs MakeTestArgs()
{
    AotCompilerArgs args;
    args.bundleName = "com.ohos.contacts";
    args.moduleName = "entry";
    args.appIdentifier = "5765880207853624761";
    args.bundleUid = TEST_BUNDLE_UID;
    args.bundleGid = TEST_BUNDLE_UID;
    args.processUid = TEST_PROCESS_UID;
    args.hapPath = "/data/app/el1/100/com.ohos.contacts/entry.hap";
    args.outputPath = "/data/app/el1/public/aot_compiler/ark_cache/com.ohos.contacts/arm64";
    args.anFileName = "/data/app/el1/public/aot_compiler/ark_cache/com.ohos.contacts/arm64/entry.an";
    args.bundleType = 0;
    args.triggerType = 1;
    args.isScreenOff = 1;
    args.isEncryptedBundle = 0;
    return args;
}

void VerifyBaseContent(const nlohmann::json &content, const AotCompilerArgs &args)
{
    EXPECT_EQ(content.at("bundleName").get<std::string>(), args.bundleName);
    EXPECT_EQ(content.at("moduleName").get<std::string>(), args.moduleName);
    EXPECT_EQ(content.at("appIdentifier").get<std::string>(), args.appIdentifier);
    EXPECT_EQ(content.at("bundleUid").get<int32_t>(), args.bundleUid);
    EXPECT_EQ(content.at("bundleGid").get<int32_t>(), args.bundleGid);
    EXPECT_EQ(content.at("processUid").get<int32_t>(), args.processUid);
    EXPECT_EQ(content.at("hapPath").get<std::string>(), args.hapPath);
    EXPECT_EQ(content.at("outputPath").get<std::string>(), args.outputPath);
    EXPECT_EQ(content.at("anFileName").get<std::string>(), args.anFileName);
    EXPECT_EQ(content.at("bundleType").get<int32_t>(), args.bundleType);
    EXPECT_EQ(content.at("triggerType").get<int32_t>(), args.triggerType);
    EXPECT_EQ(content.at("isScreenOff").get<uint32_t>(), args.isScreenOff);
    EXPECT_EQ(content.at("isEncryptedBundle").get<uint32_t>(), args.isEncryptedBundle);
}
} // namespace

class SecurityGuardReportTest : public testing::Test {
public:
    SecurityGuardReportTest() {}
    virtual ~SecurityGuardReportTest() {}

    static void SetUpTestCase() {}
    static void TearDownTestCase() {}

    void SetUp() override {}
    void TearDown() override {}
};

/**
 * @tc.name: SecurityGuardReportTest_001
 * @tc.desc: BuildEventContent builds the full compile event content (L0)
 * @tc.type: Func
 */
HWTEST_F(SecurityGuardReportTest, SecurityGuardReportTest_001, TestSize.Level0)
{
    AotCompilerArgs args = MakeTestArgs();
    std::string content = SecurityGuardReport::BuildEventContent(args, ERR_OK, SgEventType::COMPILE);
    ASSERT_FALSE(content.empty());
    nlohmann::json parsed = nlohmann::json::parse(content);
    EXPECT_EQ(parsed.at("type").get<std::string>(), "compile");
    EXPECT_EQ(parsed.at("compileResult").get<int32_t>(), ERR_OK);
    VerifyBaseContent(parsed, args);
    EXPECT_EQ(parsed.size(), static_cast<size_t>(15));
}

/**
 * @tc.name: SecurityGuardReportTest_002
 * @tc.desc: BuildEventContent passes through the compile failure code (L0)
 * @tc.type: Func
 */
HWTEST_F(SecurityGuardReportTest, SecurityGuardReportTest_002, TestSize.Level0)
{
    AotCompilerArgs args = MakeTestArgs();
    std::string content = SecurityGuardReport::BuildEventContent(args, ERR_AOT_COMPILER_CALL_FAILED,
        SgEventType::COMPILE);
    ASSERT_FALSE(content.empty());
    nlohmann::json parsed = nlohmann::json::parse(content);
    EXPECT_EQ(parsed.at("type").get<std::string>(), "compile");
    EXPECT_EQ(parsed.at("compileResult").get<int32_t>(), ERR_AOT_COMPILER_CALL_FAILED);
}

/**
 * @tc.name: SecurityGuardReportTest_003
 * @tc.desc: BuildEventContent builds the codesign event content (L0)
 * @tc.type: Func
 */
HWTEST_F(SecurityGuardReportTest, SecurityGuardReportTest_003, TestSize.Level0)
{
    AotCompilerArgs args = MakeTestArgs();
    std::string content = SecurityGuardReport::BuildEventContent(args, ERR_AOT_COMPILER_SIGNATURE_FAILED,
        SgEventType::CODE_SIGN);
    ASSERT_FALSE(content.empty());
    nlohmann::json parsed = nlohmann::json::parse(content);
    EXPECT_EQ(parsed.at("type").get<std::string>(), "codesign");
    EXPECT_EQ(parsed.at("compileResult").get<int32_t>(), ERR_AOT_COMPILER_SIGNATURE_FAILED);
    VerifyBaseContent(parsed, args);
}

/**
 * @tc.name: SecurityGuardReportTest_004
 * @tc.desc: BuildEventContent with default args keeps the schema valid
 * @tc.type: Func
 */
HWTEST_F(SecurityGuardReportTest, SecurityGuardReportTest_004, TestSize.Level0)
{
    AotCompilerArgs args; // all fields default/empty, must not crash
    std::string content = SecurityGuardReport::BuildEventContent(args, 0, SgEventType::COMPILE);
    ASSERT_FALSE(content.empty());
    nlohmann::json parsed = nlohmann::json::parse(content);
    EXPECT_EQ(parsed.size(), static_cast<size_t>(15));
    EXPECT_TRUE(parsed.at("bundleName").get<std::string>().empty());
}

/**
 * @tc.name: SecurityGuardReportTest_005
 * @tc.desc: ReportAotSecurityInfo tolerates the real SDK result (L1)
 * @tc.type: Func
 */
HWTEST_F(SecurityGuardReportTest, SecurityGuardReportTest_005, TestSize.Level0)
{
    // The test process holds no REPORT_SECURITY_EVENT permission, so the
    // real SDK call returns NO_PERMISSION; reporting is best-effort and
    // must never affect the caller (same pattern as the SG SDK own tests).
    AotCompilerArgs args = MakeTestArgs();
    SecurityGuardReport::ReportAotSecurityInfo(args, ERR_OK, SgEventType::COMPILE);
    SecurityGuardReport::ReportAotSecurityInfo(args, ERR_AOT_COMPILER_CALL_FAILED, SgEventType::COMPILE);
    SecurityGuardReport::ReportAotSecurityInfo(args, ERR_AOT_COMPILER_SIGNATURE_FAILED, SgEventType::CODE_SIGN);
    EXPECT_TRUE(true);
}
} // namespace OHOS::ArkCompiler

#endif // SECURITY_GUARD_ENABLE
