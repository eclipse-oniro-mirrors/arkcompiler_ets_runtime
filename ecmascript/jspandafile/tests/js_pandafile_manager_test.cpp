/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "assembler/assembly-emitter.h"
#include "assembler/assembly-parser.h"
#include "libpandafile/class_data_accessor-inl.h"
#include "libziparchive/zip_archive.h"

#include "ecmascript/global_env.h"
#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;
using namespace panda::panda_file;
using namespace panda::pandasm;

namespace panda::test {
class JSPandaFileManagerTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        GTEST_LOG_(INFO) << "SetUpTestCase";
    }

    static void TearDownTestCase()
    {
        GTEST_LOG_(INFO) << "TearDownCase";
    }

    void SetUp() override
    {
        TestHelper::CreateEcmaVMWithScope(instance, thread, scope);
    }

    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(instance, scope);
    }

    EcmaVM *instance {nullptr};
    EcmaHandleScope *scope {nullptr};
    JSThread *thread {nullptr};
};

HWTEST_F_L0(JSPandaFileManagerTest, GetInstance)
{
    JSPandaFileManager *manager = JSPandaFileManager::GetInstance();
    EXPECT_TRUE(manager != nullptr);
}

HWTEST_F_L0(JSPandaFileManagerTest, NewJSPandaFile)
{
    Parser parser;
    std::string fileName = "__JSPandaFileManagerTest.pa";
    const char *source = R"(
        .function void foo() {}
    )";
    auto res = parser.Parse(source, fileName);
    EXPECT_EQ(parser.ShowError().err, Error::ErrorType::ERR_NONE);

    std::unique_ptr<const File> pfPtr = pandasm::AsmEmitter::Emit(res.Value());
    JSPandaFileManager *pfManager = JSPandaFileManager::GetInstance();
    std::shared_ptr<JSPandaFile> pf = pfManager->NewJSPandaFile(pfPtr.release(), CString(fileName.c_str()));
    EXPECT_TRUE(pf != nullptr);

    auto expectFileName = pf->GetJSPandaFileDesc();
    EXPECT_STREQ(expectFileName.c_str(), "__JSPandaFileManagerTest.pa");
    remove(fileName.c_str());
    pfManager->RemoveJSPandaFileVm(instance, pf.get());
}

HWTEST_F_L0(JSPandaFileManagerTest, OpenJSPandaFile)
{
    const char *filename = "__JSPandaFileManagerTest.pa";
    const char *data = R"(
        .function void foo() {}
    )";
    JSPandaFileManager *pfManager = JSPandaFileManager::GetInstance();
    std::shared_ptr<JSPandaFile> ojspf = pfManager->OpenJSPandaFile(filename);
    EXPECT_TRUE(ojspf == nullptr);

    Parser parser;
    auto res = parser.Parse(data);
    EXPECT_TRUE(pandasm::AsmEmitter::Emit(filename, res.Value()));

    ojspf = pfManager->OpenJSPandaFile(filename);
    EXPECT_TRUE(ojspf != nullptr);
    EXPECT_STREQ(ojspf->GetJSPandaFileDesc().c_str(), "__JSPandaFileManagerTest.pa");
    pfManager->RemoveJSPandaFileVm(instance, ojspf.get());

    remove(filename);
    ojspf = pfManager->OpenJSPandaFile(filename);
    EXPECT_TRUE(ojspf == nullptr);
}

HWTEST_F_L0(JSPandaFileManagerTest, Add_Find_Remove_JSPandaFile)
{
    const char *filename1 = "__JSPandaFileManagerTest1.pa";
    const char *filename2 = "__JSPandaFileManagerTest2.pa";
    const char *data = R"(
        .function void foo() {}
    )";
    JSPandaFileManager *pfManager = JSPandaFileManager::GetInstance();
    Parser parser;
    auto res = parser.Parse(data);
    std::unique_ptr<const File> pfPtr1 = pandasm::AsmEmitter::Emit(res.Value());
    std::unique_ptr<const File> pfPtr2 = pandasm::AsmEmitter::Emit(res.Value());
    std::shared_ptr<JSPandaFile> pf1 = pfManager->NewJSPandaFile(pfPtr1.release(), CString(filename1));
    std::shared_ptr<JSPandaFile> pf2 = pfManager->NewJSPandaFile(pfPtr2.release(), CString(filename2));
    pfManager->AddJSPandaFileVm(instance, pf1);
    pfManager->AddJSPandaFileVm(instance, pf2);
    std::shared_ptr<JSPandaFile> foundPf1 = pfManager->FindJSPandaFile(filename1);
    std::shared_ptr<JSPandaFile> foundPf2 = pfManager->FindJSPandaFile(filename2);
    EXPECT_STREQ(foundPf1->GetJSPandaFileDesc().c_str(), "__JSPandaFileManagerTest1.pa");
    EXPECT_STREQ(foundPf2->GetJSPandaFileDesc().c_str(), "__JSPandaFileManagerTest2.pa");

    pfManager->RemoveJSPandaFileVm(instance, pf1.get());
    pfManager->RemoveJSPandaFileVm(instance, pf2.get());
    std::shared_ptr<JSPandaFile> afterRemovePf1 = pfManager->FindJSPandaFile(filename1);
    std::shared_ptr<JSPandaFile> afterRemovePf2 = pfManager->FindJSPandaFile(filename2);
    EXPECT_EQ(afterRemovePf1, nullptr);
    EXPECT_EQ(afterRemovePf2, nullptr);
}

HWTEST_F_L0(JSPandaFileManagerTest, MultiEcmaVM_Add_Find_Remove_JSPandaFile)
{
    const char *filename1 = "__JSPandaFileManagerTest1.pa";
    const char *filename2 = "__JSPandaFileManagerTest2.pa";
    const char *data = R"(
        .function void foo() {}
    )";
    JSPandaFileManager *pfManager = JSPandaFileManager::GetInstance();
    Parser parser;
    auto res = parser.Parse(data);
    std::unique_ptr<const File> pfPtr1 = pandasm::AsmEmitter::Emit(res.Value());
    std::unique_ptr<const File> pfPtr2 = pandasm::AsmEmitter::Emit(res.Value());
    std::shared_ptr<JSPandaFile> pf1 = pfManager->NewJSPandaFile(pfPtr1.release(), CString(filename1));
    std::shared_ptr<JSPandaFile> pf2 = pfManager->NewJSPandaFile(pfPtr2.release(), CString(filename2));
    pfManager->AddJSPandaFileVm(instance, pf1);
    pfManager->AddJSPandaFileVm(instance, pf2);

    EcmaContext *context = instance->GetJSThread()->GetCurrentEcmaContext();
    JSHandle<ConstantPool> constpool1 = instance->GetFactory()->NewSConstantPool(1);
    constpool1->SetUnsharedConstpoolIndex(JSTaggedValue(context->GetAndIncreaseUnsharedConstpoolCount()));
    JSHandle<ConstantPool> constpool2 = instance->GetFactory()->NewSConstantPool(2);
    constpool2->SetUnsharedConstpoolIndex(JSTaggedValue(context->GetAndIncreaseUnsharedConstpoolCount()));
    context->AddConstpool(pf1.get(), constpool1.GetTaggedValue(), 0);
    context->AddConstpool(pf2.get(), constpool2.GetTaggedValue(), 0);

    std::thread t1([&]() {
        EcmaVM *instance1;
        EcmaHandleScope *scope1;
        JSThread *thread1;
        TestHelper::CreateEcmaVMWithScope(instance1, thread1, scope1);
        std::shared_ptr<JSPandaFile> loadedPf1 =
            pfManager->LoadJSPandaFile(thread1, filename1, JSPandaFile::ENTRY_MAIN_FUNCTION);
        instance1->GetJSThread()->GetCurrentEcmaContext()->AddConstpool(pf1.get(), constpool1.GetTaggedValue(), 0);
        EXPECT_TRUE(pf1 == loadedPf1);
        TestHelper::DestroyEcmaVMWithScope(instance1, scope1); // Remove 'instance1' when ecmaVM destruct.
    });
    t1.join();

    std::shared_ptr<JSPandaFile> foundPf1 = pfManager->FindJSPandaFile(filename1);
    EXPECT_TRUE(foundPf1 != nullptr);

    pfManager->RemoveJSPandaFileVm(instance, pf1.get());
    pfManager->RemoveJSPandaFileVm(instance, pf2.get());
    std::shared_ptr<JSPandaFile> afterRemovePf1 = pfManager->FindJSPandaFile(filename1);
    std::shared_ptr<JSPandaFile> afterRemovePf2 = pfManager->FindJSPandaFile(filename2);
    EXPECT_EQ(afterRemovePf1, nullptr);
    EXPECT_EQ(afterRemovePf2, nullptr);
}

void CreateJSPandaFileAndConstpool(EcmaVM *vm)
{
    const char *filename = "__JSPandaFileManagerTest1.pa";
    const char *data = R"(
        .function void foo() {}
    )";
    JSPandaFileManager *pfManager = JSPandaFileManager::GetInstance();
    Parser parser;
    auto res = parser.Parse(data);
    std::unique_ptr<const File> pfPtr = pandasm::AsmEmitter::Emit(res.Value());
    std::shared_ptr<JSPandaFile> pf = pfManager->NewJSPandaFile(pfPtr.release(), CString(filename));
    pfManager->AddJSPandaFileVm(vm, pf);

    [[maybe_unused]] EcmaHandleScope handleScope(vm->GetJSThread());
    JSHandle<ConstantPool> constpool = vm->GetFactory()->NewSConstantPool(1);
    auto context = vm->GetJSThread()->GetCurrentEcmaContext();
    auto unsharedConstpoolIndex = context->GetAndIncreaseUnsharedConstpoolCount();
    JSHandle<ConstantPool> newConstpool = vm->GetFactory()->NewConstantPool(1);
    constpool->SetUnsharedConstpoolIndex(JSTaggedValue(unsharedConstpoolIndex));
    context->SetUnsharedConstpool(unsharedConstpoolIndex, newConstpool.GetTaggedValue());
    context->AddConstpool(pf.get(), constpool.GetTaggedValue(), 0);
}

HWTEST_F_L0(JSPandaFileManagerTest, GC_Add_Find_Remove_JSPandaFile)
{
    const char *filename = "__JSPandaFileManagerTest1.pa";
    JSPandaFileManager *pfManager = JSPandaFileManager::GetInstance();

    CreateJSPandaFileAndConstpool(instance);
    // Remove 'instance' and JSPandafile when trigger GC.
    SharedHeap::GetInstance()->CollectGarbage(instance->GetJSThread(), ecmascript::TriggerGCType::SHARED_GC,
                                              GCReason::OTHER);
    std::shared_ptr<JSPandaFile> afterRemovePf = pfManager->FindJSPandaFile(filename);
    EXPECT_EQ(afterRemovePf, nullptr);
}

HWTEST_F_L0(JSPandaFileManagerTest, LoadJSPandaFile)
{
    const char *filename1 = "__JSPandaFileManagerTest1.pa";
    const char *filename2 = "__JSPandaFileManagerTest2.pa";
    const char *filename3 = "__JSPandaFileManagerTest3.abc";
    const char *data = R"(
        .function void foo() {}
    )";
    JSPandaFileManager *pfManager = JSPandaFileManager::GetInstance();
    Parser parser;
    auto res = parser.Parse(data);
    std::unique_ptr<const File> pfPtr1 = pandasm::AsmEmitter::Emit(res.Value());
    std::unique_ptr<const File> pfPtr2 = pandasm::AsmEmitter::Emit(res.Value());
    std::unique_ptr<const File> pfPtr3 = pandasm::AsmEmitter::Emit(res.Value());
    std::shared_ptr<JSPandaFile> pf1 = pfManager->NewJSPandaFile(pfPtr1.release(), CString(filename1));
    std::shared_ptr<JSPandaFile> pf2 = pfManager->NewJSPandaFile(pfPtr2.release(), CString(filename2));
    std::shared_ptr<JSPandaFile> pf3 = pfManager->NewJSPandaFile(pfPtr3.release(), CString(filename3));
    pfManager->AddJSPandaFileVm(instance, pf1);
    pfManager->AddJSPandaFileVm(instance, pf2);
    pfManager->AddJSPandaFileVm(instance, pf3);
    std::shared_ptr<JSPandaFile> loadedPf1 =
        pfManager->LoadJSPandaFile(thread, filename1, JSPandaFile::ENTRY_MAIN_FUNCTION);
    std::shared_ptr<JSPandaFile> loadedPf2 =
        pfManager->LoadJSPandaFile(thread, filename2, JSPandaFile::ENTRY_MAIN_FUNCTION, (void *)data, sizeof(data));
    std::shared_ptr<JSPandaFile> loadedPf3 =
        pfManager->LoadJSPandaFile(thread, filename3, JSPandaFile::ENTRY_MAIN_FUNCTION, (void *)data, sizeof(data));
    EXPECT_TRUE(loadedPf1 != nullptr);
    EXPECT_TRUE(loadedPf2 != nullptr);
    EXPECT_TRUE(loadedPf3 != nullptr);
    EXPECT_TRUE(pf1 == loadedPf1);
    EXPECT_TRUE(pf2 == loadedPf2);
    EXPECT_TRUE(pf3 == loadedPf3);
    EXPECT_STREQ(loadedPf1->GetJSPandaFileDesc().c_str(), "__JSPandaFileManagerTest1.pa");
    EXPECT_STREQ(loadedPf2->GetJSPandaFileDesc().c_str(), "__JSPandaFileManagerTest2.pa");
    EXPECT_STREQ(loadedPf3->GetJSPandaFileDesc().c_str(), "__JSPandaFileManagerTest3.abc");

    pfManager->RemoveJSPandaFileVm(instance, pf1.get());
    pfManager->RemoveJSPandaFileVm(instance, pf2.get());
    pfManager->RemoveJSPandaFileVm(instance, pf3.get());
}

HWTEST_F_L0(JSPandaFileManagerTest, GenerateProgram)
{
    Parser parser;
    auto vm = thread->GetEcmaVM();
    const char *filename = "__JSPandaFileManagerTest.pa";
    const uint8_t *typeDesc = utf::CStringAsMutf8("L_GLOBAL;");
    const char *data = R"(
        .function void foo() {}
    )";
    auto res = parser.Parse(data);
    JSPandaFileManager *pfManager = JSPandaFileManager::GetInstance();
    std::unique_ptr<const File> pfPtr = pandasm::AsmEmitter::Emit(res.Value());
    std::shared_ptr<JSPandaFile> pf = pfManager->NewJSPandaFile(pfPtr.release(), CString(filename));
    const File *file = pf->GetPandaFile();
    File::EntityId classId = file->GetClassId(typeDesc);
    ClassDataAccessor cda(*file, classId);
    std::vector<File::EntityId> methodId {};
    cda.EnumerateMethods([&](panda_file::MethodDataAccessor &mda) {
        methodId.push_back(mda.GetMethodId());
    });
    pf->UpdateMainMethodIndex(methodId[0].GetOffset());
    MethodLiteral *method = new MethodLiteral(methodId[0]);
    pf->SetMethodLiteralToMap(method);
    pfManager->AddJSPandaFileVm(instance, pf);

    JSHandle<ecmascript::Program> program = pfManager->GenerateProgram(vm, pf.get(), JSPandaFile::ENTRY_FUNCTION_NAME);
    JSHandle<JSFunction> mainFunc(thread, program->GetMainFunction());
    JSHandle<JSTaggedValue> funcName = JSFunction::GetFunctionName(thread, JSHandle<JSFunctionBase>(mainFunc));
    EXPECT_STREQ(EcmaStringAccessor(JSHandle<EcmaString>::Cast(funcName)).ToCString().c_str(), "foo");

    pfManager->RemoveJSPandaFileVm(instance, pf.get());
}

HWTEST_F_L0(JSPandaFileManagerTest, GetJSPtExtractor)
{
    const char *filename = "__JSPandaFileManagerTest.pa";
    const char *data = R"(
        .function void foo() {}
    )";
    Parser parser;
    auto res = parser.Parse(data);
    JSPandaFileManager *pfManager = JSPandaFileManager::GetInstance();
    std::unique_ptr<const File> pfPtr = pandasm::AsmEmitter::Emit(res.Value());
    std::shared_ptr<JSPandaFile> pf = pfManager->NewJSPandaFile(pfPtr.release(), CString(filename));
    pfManager->AddJSPandaFileVm(instance, pf);
    DebugInfoExtractor *extractor = pfManager->GetJSPtExtractor(pf.get());
    EXPECT_TRUE(extractor != nullptr);

    pfManager->RemoveJSPandaFileVm(instance, pf.get());
}

HWTEST_F_L0(JSPandaFileManagerTest, EnumerateJSPandaFiles)
{
    const char *filename1 = "__JSPandaFileManagerTest3.pa";
    const char *filename2 = "__JSPandaFileManagerTest4.pa";
    const char *data = R"(
        .function void foo() {}
    )";
    Parser parser;
    auto res = parser.Parse(data);
    JSPandaFileManager *pfManager = JSPandaFileManager::GetInstance();
    std::unique_ptr<const File> pfPtr1 = pandasm::AsmEmitter::Emit(res.Value());
    std::unique_ptr<const File> pfPtr2 = pandasm::AsmEmitter::Emit(res.Value());
    std::shared_ptr<JSPandaFile> pf1 = pfManager->NewJSPandaFile(pfPtr1.release(), CString(filename1));
    std::shared_ptr<JSPandaFile> pf2 = pfManager->NewJSPandaFile(pfPtr2.release(), CString(filename2));
    pfManager->AddJSPandaFileVm(instance, pf1);
    pfManager->AddJSPandaFileVm(instance, pf2);
    std::vector<CString> descList{};
    int count = 0;
    pfManager->EnumerateJSPandaFiles([&](const std::shared_ptr<JSPandaFile> &file) -> bool {
        auto desc = file->GetJSPandaFileDesc();
        std::cout << "desc:" << desc << std::endl;
        descList.push_back(desc);
        count++;
        return true;
    });
    EXPECT_EQ(count, 2); // 2 : test number of files
    // Sort by the hash value of the element, the output is unordered
    EXPECT_STREQ(descList[0].c_str(), "__JSPandaFileManagerTest4.pa");
    EXPECT_STREQ(descList[1].c_str(), "__JSPandaFileManagerTest3.pa");

    pfManager->RemoveJSPandaFileVm(instance, pf1.get());
    pfManager->RemoveJSPandaFileVm(instance, pf2.get());
}
}  // namespace panda::test
