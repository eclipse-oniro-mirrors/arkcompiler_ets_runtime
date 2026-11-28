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

#include <fstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#include "ecmascript/jspandafile/js_pandafile_snapshot.h"
#include "ecmascript/module/module_snapshot.h"
#include "ecmascript/platform/filesystem.h"
#include "ecmascript/snapshot/common/modules_snapshot_helper.h"

#include "gtest/gtest.h"

#include "ecmascript/napi/include/jsnapi.h"
#include "ecmascript/ohos/js_pandafile_snapshot_interfaces.h"
#include "ecmascript/ohos/enable_aot_list_helper.h"
#include "ecmascript/ohos/tests/mock/mock_enable_aot_list_helper.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda;
using namespace panda::ecmascript;
using namespace panda::ecmascript::ohos;

namespace panda::test {
class JSPandaFileSnapshotInterfacesTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        GTEST_LOG_(INFO) << "SetUpTestCase";
    }

    static void TearDownTestCase()
    {
        GTEST_LOG_(INFO) << "TearDownTestCase";
    }

    void SetUp() override
    {
        vm_ = JSNApi::CreateEcmaVM(runtimeOptions_);
        ASSERT(vm_ != nullptr);
        vm_->GetJSThread()->ManagedCodeBegin();
    }

    void TearDown() override
    {
        if (vm_ != nullptr) {
            vm_->GetJSThread()->ManagedCodeEnd();
            ohos::EnableAotJitListHelper::GetInstance()->Clear();
            JSNApi::DestroyJSVM(vm_);
            vm_ = nullptr;
        }
    }

protected:
    JSRuntimeOptions runtimeOptions_;
    EcmaVM *vm_ {nullptr};
};

// Test Serialize function - non-existent file path
HWTEST_F_L0(JSPandaFileSnapshotInterfacesTest, SerializeNonExistentPathTest)
{
    const char* nonExistentPath = "/tmp/non_existent_file_12345.txt";
    JSPandaFileSnapshotInterfaces::Serialize(vm_, nonExistentPath);
    ASSERT_TRUE(true);
}

// Test Serialize function - empty path
HWTEST_F_L0(JSPandaFileSnapshotInterfacesTest, SerializeEmptyPathTest)
{
    const char* emptyPath = "";
    JSPandaFileSnapshotInterfaces::Serialize(vm_, emptyPath);
    ASSERT_TRUE(true);
}

// Test Serialize function - existing file
HWTEST_F_L0(JSPandaFileSnapshotInterfacesTest, SerializeExistingFileTest)
{
    const char* tempFile = "/tmp/test_snapshot_file.txt";
    std::ofstream outfile(tempFile);
    if (outfile.is_open()) {
        outfile << "test data" << std::endl;
        outfile.close();
        JSPandaFileSnapshotInterfaces::Serialize(vm_, tempFile);
        std::remove(tempFile);
        ASSERT_TRUE(true);
    } else {
        GTEST_LOG_(INFO) << "Cannot create temp file, skip this test";
        ASSERT_TRUE(true);
    }
}

// Test Serialize function - multiple path scenarios
HWTEST_F_L0(JSPandaFileSnapshotInterfacesTest, SerializeMultiplePathsTest)
{
    const char* paths[] = {
        "/tmp/snapshot1.bin",
        "/tmp/snapshot2.bin",
        nullptr
    };

    for (int i = 0; paths[i] != nullptr; i++) {
        JSPandaFileSnapshotInterfaces::Serialize(vm_, paths[i]);
    }

    ASSERT_TRUE(true);
}

// Test Serialize function - special character path
HWTEST_F_L0(JSPandaFileSnapshotInterfacesTest, SerializeSpecialCharPathTest)
{
    const char* specialPath = "/tmp/snapshot-file_with-special.chars.bin";
    JSPandaFileSnapshotInterfaces::Serialize(vm_, specialPath);
    ASSERT_TRUE(true);
}
namespace {
void CreateDummyFile(const std::string &path, const std::string &content = "dummy")
{
    std::ofstream file(path, std::ios::out | std::ios::binary);
    file << content;
}
} // namespace

// Snapshot disabled by the state file: no snapshot file is deleted.
HWTEST_F_L0(JSPandaFileSnapshotInterfacesTest, ReadCrossBundleHspInfoDisabledSilentTest)
{
    // Trailing '/' matters: dir and file name are concatenated directly.
    const std::string dir = "./crossbundle_hsp_disabled_dir/";
    const std::string moduleFile = dir + std::string(ModuleSnapshot::MODULE_SNAPSHOT_FILE_NAME);
    const std::string hspFile = dir + std::string(JSPandaFileSnapshot::CROSS_BUNDLE_HSP_FILE_NAME);
    // Same name as the private ModulesSnapshotHelper::MODULE_SNAPSHOT_STATE_FILE_NAME.
    const std::string stateFile = dir + "ArkModuleSnapshot.state";
    Unlink(moduleFile.c_str());
    Unlink(hspFile.c_str());
    Unlink(stateFile.c_str());
    rmdir(dir.c_str());
    ASSERT_EQ(mkdir(dir.c_str(), 0755), 0);
    CreateDummyFile(moduleFile);
    CreateDummyFile(hspFile);
    // '0' = STATE_WORD_ALL_SNAPSHOT_DISABLED
    CreateDummyFile(stateFile, "0:unit-test\n");
    ModulesSnapshotHelper::ResetStateForTest();
    JSNApi::SetIsMainProcess(true);
    JSPandaFileSnapshotInterfaces::ReadCrossBundleHspInfo(vm_, CString(dir));
    EXPECT_TRUE(FileExist(moduleFile.c_str()));
    EXPECT_TRUE(FileExist(hspFile.c_str()));
    EXPECT_EQ(ModulesSnapshotHelper::GetFeatureLoaded(),
        static_cast<int>(SnapshotFeatureState::DEFAULT));
    // A missing snapshot directory is also treated as "not enabled".
    ModulesSnapshotHelper::ResetStateForTest();
    JSPandaFileSnapshotInterfaces::ReadCrossBundleHspInfo(vm_, CString("./crossbundle_hsp_no_such_dir/"));
    EXPECT_EQ(ModulesSnapshotHelper::GetFeatureLoaded(),
        static_cast<int>(SnapshotFeatureState::DEFAULT));
    JSNApi::SetIsMainProcess(false);
    Unlink(moduleFile.c_str());
    Unlink(hspFile.c_str());
    Unlink(stateFile.c_str());
    rmdir(dir.c_str());
}

// First start: the snapshot directory exists but holds no snapshot file.
HWTEST_F_L0(JSPandaFileSnapshotInterfacesTest, ReadCrossBundleHspInfoFirstStartTest)
{
    const std::string dir = "./crossbundle_hsp_first_start_dir/";
    rmdir(dir.c_str());
    ASSERT_EQ(mkdir(dir.c_str(), 0755), 0);
    ModulesSnapshotHelper::ResetStateForTest();
    JSNApi::SetIsMainProcess(true);
    JSPandaFileSnapshotInterfaces::ReadCrossBundleHspInfo(vm_, CString(dir));
    JSNApi::SetIsMainProcess(false);
    EXPECT_EQ(ModulesSnapshotHelper::GetFeatureLoaded(),
        static_cast<int>(SnapshotFeatureState::DEFAULT));
    // Succeeds only while the directory is still empty.
    EXPECT_EQ(rmdir(dir.c_str()), 0);
}

// The cross-bundle HSP snapshot file is missing while other snapshot files
// exist: the other snapshot files are removed.
HWTEST_F_L0(JSPandaFileSnapshotInterfacesTest, ReadCrossBundleHspInfoFileNotExistRemovesSnapshotFilesTest)
{
    const std::string dir = "./crossbundle_hsp_no_file_dir/";
    const std::string moduleFile = dir + std::string(ModuleSnapshot::MODULE_SNAPSHOT_FILE_NAME);
    const std::string pandaFileFile = dir + "entry" + std::string(JSPandaFileSnapshot::JSPANDAFILE_FILE_NAME);
    Unlink(moduleFile.c_str());
    Unlink(pandaFileFile.c_str());
    rmdir(dir.c_str());
    ASSERT_EQ(mkdir(dir.c_str(), 0755), 0);
    CreateDummyFile(moduleFile);
    CreateDummyFile(pandaFileFile);
    ModulesSnapshotHelper::ResetStateForTest();
    JSNApi::SetIsMainProcess(true);
    JSPandaFileSnapshotInterfaces::ReadCrossBundleHspInfo(vm_, CString(dir));
    JSNApi::SetIsMainProcess(false);
    EXPECT_FALSE(FileExist(moduleFile.c_str()));
    EXPECT_FALSE(FileExist(pandaFileFile.c_str()));
    EXPECT_EQ(ModulesSnapshotHelper::GetFeatureLoaded(),
        static_cast<int>(SnapshotFeatureState::DEFAULT));
    rmdir(dir.c_str());
}

// A corrupted cross-bundle HSP snapshot file is a real error: snapshot files
// are removed. The stored checksum is zero while adler32(0, "AAAAAAAA") is
// not, so the checksum check fails.
HWTEST_F_L0(JSPandaFileSnapshotInterfacesTest, ReadCrossBundleHspInfoCorruptFileRecoveryTest)
{
    const std::string dir = "./crossbundle_hsp_corrupt_dir/";
    const std::string moduleFile = dir + std::string(ModuleSnapshot::MODULE_SNAPSHOT_FILE_NAME);
    const std::string hspFile = dir + std::string(JSPandaFileSnapshot::CROSS_BUNDLE_HSP_FILE_NAME);
    Unlink(moduleFile.c_str());
    Unlink(hspFile.c_str());
    rmdir(dir.c_str());
    ASSERT_EQ(mkdir(dir.c_str(), 0755), 0);
    CreateDummyFile(moduleFile);
    CreateDummyFile(hspFile, "AAAAAAAA" + std::string(4, '\0'));
    ModulesSnapshotHelper::ResetStateForTest();
    JSNApi::SetIsMainProcess(true);
    JSPandaFileSnapshotInterfaces::ReadCrossBundleHspInfo(vm_, CString(dir));
    JSNApi::SetIsMainProcess(false);
    EXPECT_FALSE(FileExist(moduleFile.c_str()));
    EXPECT_FALSE(FileExist(hspFile.c_str()));
    rmdir(dir.c_str());
}
}  // namespace panda::test
