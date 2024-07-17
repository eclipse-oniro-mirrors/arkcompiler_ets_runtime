/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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
#include "ecmascript/module/js_module_manager.h"

#include "ecmascript/compiler/aot_file/aot_file_manager.h"
#include "ecmascript/global_env.h"
#include "ecmascript/interpreter/fast_runtime_stub-inl.h"
#include "ecmascript/interpreter/slow_runtime_stub.h"
#include "ecmascript/interpreter/frame_handler.h"
#include "ecmascript/js_array.h"
#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/jspandafile/js_pandafile_executor.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/module/js_module_deregister.h"
#include "ecmascript/module/js_module_source_text.h"
#include "ecmascript/module/js_shared_module.h"
#include "ecmascript/module/js_shared_module_manager.h"
#include "ecmascript/module/module_data_extractor.h"
#include "ecmascript/module/module_manager_helper.h"
#include "ecmascript/module/module_path_helper.h"
#include "ecmascript/require/js_cjs_module.h"
#include "ecmascript/tagged_dictionary.h"
#ifdef PANDA_TARGET_WINDOWS
#include <algorithm>
#endif

namespace panda::ecmascript {
using StringHelper = base::StringHelper;
using JSPandaFile = ecmascript::JSPandaFile;
using JSRecordInfo = ecmascript::JSPandaFile::JSRecordInfo;

ModuleManager::ModuleManager(EcmaVM *vm) : vm_(vm) {}

JSTaggedValue ModuleManager::GetCurrentModule()
{
    FrameHandler frameHandler(vm_->GetJSThread());
    JSTaggedValue currentFunc = frameHandler.GetFunction();
    JSTaggedValue module = JSFunction::Cast(currentFunc.GetTaggedObject())->GetModule();
    if (SourceTextModule::IsSendableFunctionModule(module)) {
        JSTaggedValue recordName = SourceTextModule::GetModuleName(module);
        CString recordNameStr = ModulePathHelper::Utf8ConvertToString(recordName);
        return HostGetImportedModule(recordNameStr).GetTaggedValue();
    }
    return module;
}

JSHandle<JSTaggedValue> ModuleManager::GenerateSendableFuncModule(const JSHandle<JSTaggedValue> &module)
{
    // Clone isolate module at shared-heap to mark sendable class.
    return SendableClassModule::GenerateSendableFuncModule(vm_->GetJSThread(), module);
}

JSTaggedValue ModuleManager::GetModuleValueInner(int32_t index)
{
    JSTaggedValue currentModule = GetCurrentModule();
    if (currentModule.IsUndefined()) {
        LOG_FULL(FATAL) << "GetModuleValueInner currentModule failed";
    }
    return SourceTextModule::Cast(currentModule.GetTaggedObject())->GetModuleValue(vm_->GetJSThread(), index, false);
}

JSTaggedValue ModuleManager::GetModuleValueInner(int32_t index, JSTaggedValue jsFunc)
{
    JSTaggedValue currentModule = JSFunction::Cast(jsFunc.GetTaggedObject())->GetModule();
    if (currentModule.IsUndefined()) {
        LOG_FULL(FATAL) << "GetModuleValueInner currentModule failed";
    }
    return SourceTextModule::Cast(currentModule.GetTaggedObject())->GetModuleValue(vm_->GetJSThread(), index, false);
}

JSTaggedValue ModuleManager::GetModuleValueInner(int32_t index, JSHandle<JSTaggedValue> currentModule)
{
    if (currentModule->IsUndefined()) {
        LOG_FULL(FATAL) << "GetModuleValueInner currentModule failed";
    }
    return SourceTextModule::Cast(currentModule->GetTaggedObject())->GetModuleValue(vm_->GetJSThread(), index, false);
}

JSTaggedValue ModuleManager::GetModuleValueOutter(int32_t index)
{
    JSTaggedValue currentModule = GetCurrentModule();
    return GetModuleValueOutterInternal(index, currentModule);
}

JSTaggedValue ModuleManager::GetModuleValueOutter(int32_t index, JSTaggedValue jsFunc)
{
    JSTaggedValue currentModule = JSFunction::Cast(jsFunc.GetTaggedObject())->GetModule();
    return GetModuleValueOutterInternal(index, currentModule);
}

JSTaggedValue ModuleManager::GetModuleValueOutter(int32_t index, JSHandle<JSTaggedValue> currentModule)
{
    return GetModuleValueOutterInternal(index, currentModule.GetTaggedValue());
}

JSTaggedValue ModuleManager::GetModuleValueOutterInternal(int32_t index, JSTaggedValue currentModule)
{
    JSThread *thread = vm_->GetJSThread();
    if (currentModule.IsUndefined()) {
        LOG_FULL(FATAL) << "GetModuleValueOutter currentModule failed";
        UNREACHABLE();
    }
    JSHandle<SourceTextModule> currentModuleHdl(thread, currentModule);
    JSTaggedValue moduleEnvironment = currentModuleHdl->GetEnvironment();
    if (moduleEnvironment.IsUndefined()) {
        return thread->GlobalConstants()->GetUndefined();
    }
    ASSERT(moduleEnvironment.IsTaggedArray());
    JSTaggedValue resolvedBinding = TaggedArray::Cast(moduleEnvironment.GetTaggedObject())->Get(index);
    if (resolvedBinding.IsResolvedIndexBinding()) {
        ResolvedIndexBinding *binding = ResolvedIndexBinding::Cast(resolvedBinding.GetTaggedObject());
        JSTaggedValue resolvedModule = binding->GetModule();
        JSHandle<SourceTextModule> module(thread, resolvedModule);
        ASSERT(resolvedModule.IsSourceTextModule());

        // Support for only modifying var value of HotReload.
        // Cause patchFile exclude the record of importing modifying var. Can't reresolve moduleRecord.
        EcmaContext *context = thread->GetCurrentEcmaContext();
        if (context->GetStageOfHotReload() == StageOfHotReload::LOAD_END_EXECUTE_PATCHMAIN) {
            const JSHandle<JSTaggedValue> resolvedModuleOfHotReload =
                context->FindPatchModule(ConvertToString(module->GetEcmaModuleRecordName()));
            if (!resolvedModuleOfHotReload->IsHole()) {
                resolvedModule = resolvedModuleOfHotReload.GetTaggedValue();
                JSHandle<SourceTextModule> moduleOfHotReload(thread, resolvedModule);
                return ModuleManagerHelper::GetModuleValue(thread, moduleOfHotReload, binding->GetIndex());
            }
        }
        return ModuleManagerHelper::GetModuleValue(thread, module, binding->GetIndex());
    }
    if (resolvedBinding.IsResolvedBinding()) {
        ResolvedBinding *binding = ResolvedBinding::Cast(resolvedBinding.GetTaggedObject());
        JSTaggedValue resolvedModule = binding->GetModule();
        JSHandle<SourceTextModule> module(thread, resolvedModule);
        if (SourceTextModule::IsNativeModule(module->GetTypes())) {
            return ModuleManagerHelper::GetNativeModuleValue(thread, resolvedModule, binding->GetBindingName());
        }
        if (module->GetTypes() == ModuleTypes::CJS_MODULE) {
            JSHandle<JSTaggedValue> cjsModuleName(thread, SourceTextModule::GetModuleName(module.GetTaggedValue()));
            return CjsModule::SearchFromModuleCache(thread, cjsModuleName).GetTaggedValue();
        }
    }
    if (resolvedBinding.IsResolvedRecordIndexBinding()) {
        return ModuleManagerHelper::GetModuleValueFromIndexBinding(thread, currentModuleHdl, resolvedBinding);
    }
    if (resolvedBinding.IsResolvedRecordBinding()) {
        return ModuleManagerHelper::GetModuleValueFromRecordBinding(thread, currentModuleHdl, resolvedBinding);
    }
    LOG_ECMA(FATAL) << "Get module value failed, mistaken ResolvedBinding";
    UNREACHABLE();
}

JSTaggedValue ModuleManager::GetLazyModuleValueOutter(int32_t index, JSTaggedValue jsFunc)
{
    JSTaggedValue currentModule = JSFunction::Cast(jsFunc.GetTaggedObject())->GetModule();
    return GetLazyModuleValueOutterInternal(index, currentModule);
}

JSTaggedValue ModuleManager::GetLazyModuleValueOutterInternal(int32_t index, JSTaggedValue currentModule)
{
    JSThread *thread = vm_->GetJSThread();
    if (currentModule.IsUndefined()) {
        LOG_FULL(FATAL) << "GetLazyModuleValueOutter currentModule failed";
        UNREACHABLE();
    }
    JSHandle<SourceTextModule> currentModuleHdl(thread, currentModule);
    JSTaggedValue moduleEnvironment = currentModuleHdl->GetEnvironment();
    if (moduleEnvironment.IsUndefined()) {
        return thread->GlobalConstants()->GetUndefined();
    }
    ASSERT(moduleEnvironment.IsTaggedArray());
    JSTaggedValue resolvedBinding = TaggedArray::Cast(moduleEnvironment.GetTaggedObject())->Get(index);
    if (resolvedBinding.IsResolvedIndexBinding()) {
        JSHandle<ResolvedIndexBinding> binding(thread, resolvedBinding);
        JSTaggedValue resolvedModule = binding->GetModule();
        JSHandle<SourceTextModule> module(thread, resolvedModule);
        ASSERT(resolvedModule.IsSourceTextModule());
        SourceTextModule::Evaluate(thread, module, nullptr);
        if (thread->HasPendingException()) {
            return JSTaggedValue::Undefined();
        }
        // Support for only modifying var value of HotReload.
        // Cause patchFile exclude the record of importing modifying var. Can't reresolve moduleRecord.
        EcmaContext *context = thread->GetCurrentEcmaContext();
        if (context->GetStageOfHotReload() == StageOfHotReload::LOAD_END_EXECUTE_PATCHMAIN) {
            const JSHandle<JSTaggedValue> resolvedModuleOfHotReload =
                context->FindPatchModule(ConvertToString(module->GetEcmaModuleRecordName()));
            if (!resolvedModuleOfHotReload->IsHole()) {
                resolvedModule = resolvedModuleOfHotReload.GetTaggedValue();
                JSHandle<SourceTextModule> moduleOfHotReload(thread, resolvedModule);
                return ModuleManagerHelper::GetModuleValue(thread, moduleOfHotReload, binding->GetIndex());
            }
        }
        return ModuleManagerHelper::GetModuleValue(thread, module, binding->GetIndex());
    }
    if (resolvedBinding.IsResolvedBinding()) {
        JSHandle<ResolvedBinding> binding(thread, resolvedBinding);
        JSTaggedValue resolvedModule = binding->GetModule();
        JSHandle<SourceTextModule> module(thread, resolvedModule);
        ModuleStatus status = module->GetStatus();
        ModuleTypes moduleType = module->GetTypes();
        if (SourceTextModule::IsNativeModule(moduleType)) {
            SourceTextModule::InstantiateNativeModule(thread, currentModuleHdl, module, moduleType);
            module->SetStatus(ModuleStatus::EVALUATED);
            return ModuleManagerHelper::GetNativeModuleValue(thread, resolvedModule, binding->GetBindingName());
        }
        if (moduleType == ModuleTypes::CJS_MODULE) {
            if (status != ModuleStatus::EVALUATED) {
                SourceTextModule::ModuleExecution(thread, module, nullptr, 0);
                module->SetStatus(ModuleStatus::EVALUATED);
            }
            SourceTextModule::InstantiateCJS(thread, currentModuleHdl, module);
            JSHandle<JSTaggedValue> cjsModuleName(thread, SourceTextModule::GetModuleName(module.GetTaggedValue()));
            return CjsModule::SearchFromModuleCache(thread, cjsModuleName).GetTaggedValue();
        }
    }
    if (resolvedBinding.IsResolvedRecordIndexBinding()) {
        return ModuleManagerHelper::GetLazyModuleValueFromIndexBinding(thread, currentModuleHdl, resolvedBinding);
    }
    if (resolvedBinding.IsResolvedRecordBinding()) {
        return ModuleManagerHelper::GetLazyModuleValueFromRecordBinding(thread, currentModuleHdl, resolvedBinding);
    }
    LOG_ECMA(FATAL) << "Get module value failed, mistaken ResolvedBinding";
    UNREACHABLE();
}

void ModuleManager::StoreModuleValue(int32_t index, JSTaggedValue value)
{
    JSThread *thread = vm_->GetJSThread();
    JSHandle<SourceTextModule> currentModule(thread, GetCurrentModule());
    StoreModuleValueInternal(currentModule, index, value);
}

void ModuleManager::StoreModuleValue(int32_t index, JSTaggedValue value, JSTaggedValue jsFunc)
{
    JSThread *thread = vm_->GetJSThread();
    JSHandle<SourceTextModule> currentModule(thread, JSFunction::Cast(jsFunc.GetTaggedObject())->GetModule());
    StoreModuleValueInternal(currentModule, index, value);
}

void ModuleManager::StoreModuleValueInternal(JSHandle<SourceTextModule> &currentModule,
                                             int32_t index, JSTaggedValue value)
{
    if (currentModule.GetTaggedValue().IsUndefined()) {
        LOG_FULL(FATAL) << "StoreModuleValue currentModule failed";
        UNREACHABLE();
    }
    JSThread *thread = vm_->GetJSThread();
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    currentModule->StoreModuleValue(thread, index, valueHandle);
}

JSTaggedValue ModuleManager::GetModuleValueInner(JSTaggedValue key)
{
    JSTaggedValue currentModule = GetCurrentModule();
    if (currentModule.IsUndefined()) {
        LOG_FULL(FATAL) << "GetModuleValueInner currentModule failed";
        UNREACHABLE();
    }
    return SourceTextModule::Cast(currentModule.GetTaggedObject())->GetModuleValue(vm_->GetJSThread(), key, false);
}

JSTaggedValue ModuleManager::GetModuleValueInner(JSTaggedValue key, JSTaggedValue jsFunc)
{
    JSTaggedValue currentModule = JSFunction::Cast(jsFunc.GetTaggedObject())->GetModule();
    if (currentModule.IsUndefined()) {
        LOG_FULL(FATAL) << "GetModuleValueInner currentModule failed";
        UNREACHABLE();
    }
    return SourceTextModule::Cast(currentModule.GetTaggedObject())->GetModuleValue(vm_->GetJSThread(), key, false);
}

JSTaggedValue ModuleManager::GetModuleValueOutter(JSTaggedValue key)
{
    JSTaggedValue currentModule = GetCurrentModule();
    return GetModuleValueOutterInternal(key, currentModule);
}

JSTaggedValue ModuleManager::GetModuleValueOutter(JSTaggedValue key, JSTaggedValue jsFunc)
{
    JSTaggedValue currentModule = JSFunction::Cast(jsFunc.GetTaggedObject())->GetModule();
    return GetModuleValueOutterInternal(key, currentModule);
}

JSTaggedValue ModuleManager::GetModuleValueOutterInternal(JSTaggedValue key, JSTaggedValue currentModule)
{
    JSThread *thread = vm_->GetJSThread();
    if (currentModule.IsUndefined()) {
        LOG_FULL(FATAL) << "GetModuleValueOutter currentModule failed";
        UNREACHABLE();
    }
    JSTaggedValue moduleEnvironment = SourceTextModule::Cast(currentModule.GetTaggedObject())->GetEnvironment();
    if (moduleEnvironment.IsUndefined()) {
        return thread->GlobalConstants()->GetUndefined();
    }
    int entry = NameDictionary::Cast(moduleEnvironment.GetTaggedObject())->FindEntry(key);
    if (entry == -1) {
        return thread->GlobalConstants()->GetUndefined();
    }
    JSTaggedValue resolvedBinding = NameDictionary::Cast(moduleEnvironment.GetTaggedObject())->GetValue(entry);
    ASSERT(resolvedBinding.IsResolvedBinding());
    ResolvedBinding *binding = ResolvedBinding::Cast(resolvedBinding.GetTaggedObject());
    JSTaggedValue resolvedModule = binding->GetModule();
    ASSERT(resolvedModule.IsSourceTextModule());
    SourceTextModule *module = SourceTextModule::Cast(resolvedModule.GetTaggedObject());
    if (module->GetTypes() == ModuleTypes::CJS_MODULE) {
        JSHandle<JSTaggedValue> cjsModuleName(thread, SourceTextModule::GetModuleName(JSTaggedValue(module)));
        return CjsModule::SearchFromModuleCache(thread, cjsModuleName).GetTaggedValue();
    }
    return module->GetModuleValue(thread, binding->GetBindingName(), false);
}

void ModuleManager::StoreModuleValue(JSTaggedValue key, JSTaggedValue value)
{
    JSThread *thread = vm_->GetJSThread();
    JSHandle<SourceTextModule> currentModule(thread, GetCurrentModule());
    StoreModuleValueInternal(currentModule, key, value);
}

void ModuleManager::StoreModuleValue(JSTaggedValue key, JSTaggedValue value, JSTaggedValue jsFunc)
{
    JSThread *thread = vm_->GetJSThread();
    JSHandle<SourceTextModule> currentModule(thread, JSFunction::Cast(jsFunc.GetTaggedObject())->GetModule());
    StoreModuleValueInternal(currentModule, key, value);
}

void ModuleManager::StoreModuleValueInternal(JSHandle<SourceTextModule> &currentModule,
                                             JSTaggedValue key, JSTaggedValue value)
{
    if (currentModule.GetTaggedValue().IsUndefined()) {
        LOG_FULL(FATAL) << "StoreModuleValue currentModule failed";
        UNREACHABLE();
    }
    JSThread *thread = vm_->GetJSThread();
    JSHandle<JSTaggedValue> keyHandle(thread, key);
    JSHandle<JSTaggedValue> valueHandle(thread, value);
    currentModule->StoreModuleValue(thread, keyHandle, valueHandle);
}
JSHandle<SourceTextModule> ModuleManager::GetImportedModule(const CString &referencing)
{
    auto thread = vm_->GetJSThread();
    if (!IsLocalModuleLoaded(referencing)) {
        return SharedModuleManager::GetInstance()->GetSModule(thread, referencing);
    } else {
        return HostGetImportedModule(referencing);
    }
}

JSHandle<SourceTextModule> ModuleManager::HostGetImportedModule(const CString &referencing)
{
    auto entry = resolvedModules_.find(referencing);
    if (entry == resolvedModules_.end()) {
        LOG_ECMA(FATAL) << "Can not get module: " << referencing;
    }
    return JSHandle<SourceTextModule>(vm_->GetJSThread(), entry->second);
}

JSTaggedValue ModuleManager::HostGetImportedModule(void *src)
{
    const char *str = reinterpret_cast<char *>(src);
    CString referencing(str, strlen(str));
    LOG_FULL(INFO) << "current str during module deregister process : " << referencing;
    auto entry = resolvedModules_.find(referencing);
    if (entry == resolvedModules_.end()) {
        LOG_FULL(INFO) << "The module has been unloaded, " << referencing;
        return JSTaggedValue::Undefined();
    }
    JSTaggedValue result = entry->second;
    return result;
}

bool ModuleManager::IsLocalModuleLoaded(const CString& referencing)
{
    auto entry = resolvedModules_.find(referencing);
    if (entry == resolvedModules_.end()) {
        return false;
    }
    return true;
}

bool ModuleManager::IsSharedModuleLoaded(const CString &referencing)
{
    SharedModuleManager* sharedModuleManager = SharedModuleManager::GetInstance();
    return sharedModuleManager->SearchInSModuleManager(vm_->GetJSThread(), referencing);
}

bool ModuleManager::IsModuleLoaded(const CString &referencing)
{
    if (IsLocalModuleLoaded(referencing)) {
        return true;
    }
    SharedModuleManager* sharedModuleManager = SharedModuleManager::GetInstance();
    return sharedModuleManager->SearchInSModuleManager(vm_->GetJSThread(), referencing);
}

bool ModuleManager::IsEvaluatedModule(const CString &referencing)
{
    auto entry = resolvedModules_.find(referencing);
    if (entry == resolvedModules_.end()) {
        return false;
    }
    JSTaggedValue result = entry->second;
    if (SourceTextModule::Cast(result.GetTaggedObject())->GetStatus() ==
        ModuleStatus::EVALUATED) {
            return true;
    }
    return false;
}

JSHandle<JSTaggedValue> ModuleManager::HostResolveImportedModuleWithMerge(const CString &moduleFileName,
    const CString &recordName, bool executeFromJob)
{
    auto entry = resolvedModules_.find(recordName);
    if (entry != resolvedModules_.end()) {
        return JSHandle<JSTaggedValue>(vm_->GetJSThread(), entry->second);
    }
    return CommonResolveImportedModuleWithMerge(moduleFileName, recordName, executeFromJob);
}

JSHandle<JSTaggedValue> ModuleManager::HostResolveImportedModuleWithMergeForHotReload(const CString &moduleFileName,
    const CString &recordName, bool executeFromJob)
{
    JSThread *thread = vm_->GetJSThread();
    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, moduleFileName, recordName, false);
    if (jsPandaFile == nullptr) {
        LOG_FULL(FATAL) << "Load current file's panda file failed. Current file is " << moduleFileName;
    }
    JSHandle<JSTaggedValue> moduleRecord = ResolveModuleWithMerge(thread,
        jsPandaFile.get(), recordName, executeFromJob);
    RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
    UpdateResolveImportedModule(recordName, moduleRecord.GetTaggedValue());
    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleManager::CommonResolveImportedModuleWithMerge(const CString &moduleFileName,
    const CString &recordName, bool executeFromJob)
{
    JSThread *thread = vm_->GetJSThread();
    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, moduleFileName, recordName, false);
    if (jsPandaFile == nullptr) {
        LOG_FULL(FATAL) << "Load current file's panda file failed. Current file is " << moduleFileName;
    }
    JSHandle<JSTaggedValue> moduleRecord = ResolveModuleWithMerge(thread,
        jsPandaFile.get(), recordName, executeFromJob);
    RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
    AddResolveImportedModule(recordName, moduleRecord.GetTaggedValue());
    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleManager::HostResolveImportedModule(const CString &referencingModule, bool executeFromJob)
{
    JSThread *thread = vm_->GetJSThread();

    CString moduleFileName = referencingModule;
    if (vm_->IsBundlePack()) {
        if (!AOTFileManager::GetAbsolutePath(referencingModule, moduleFileName)) {
            CString msg = "Parse absolute " + referencingModule + " path failed";
            THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
        }
    }

    auto entry = resolvedModules_.find(moduleFileName);
    if (entry != resolvedModules_.end()) {
        return JSHandle<JSTaggedValue>(thread, entry->second);
    }

    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, moduleFileName, JSPandaFile::ENTRY_MAIN_FUNCTION);
    if (jsPandaFile == nullptr) {
        LOG_FULL(FATAL) << "Load current file's panda file failed. Current file is " << moduleFileName;
    }

    return ResolveModule(thread, jsPandaFile.get(), executeFromJob);
}

// The security interface needs to be modified accordingly.
JSHandle<JSTaggedValue> ModuleManager::HostResolveImportedModule(const void *buffer, size_t size,
                                                                 const CString &filename)
{
    JSThread *thread = vm_->GetJSThread();

    auto entry = resolvedModules_.find(filename);
    if (entry != resolvedModules_.end()) {
        return JSHandle<JSTaggedValue>(thread, entry->second);
    }

    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, filename,
                                                           JSPandaFile::ENTRY_MAIN_FUNCTION, buffer, size);
    if (jsPandaFile == nullptr) {
        LOG_FULL(FATAL) << "Load current file's panda file failed. Current file is " << filename;
    }

    return ResolveModule(thread, jsPandaFile.get());
}

JSHandle<JSTaggedValue> ModuleManager::ResolveModule(JSThread *thread, const JSPandaFile *jsPandaFile,
    bool executeFromJob)
{
    CString moduleFileName = jsPandaFile->GetJSPandaFileDesc();
    JSHandle<JSTaggedValue> moduleRecord = thread->GlobalConstants()->GetHandledUndefined();
    JSRecordInfo recordInfo = const_cast<JSPandaFile *>(jsPandaFile)->FindRecordInfo(JSPandaFile::ENTRY_FUNCTION_NAME);
    if (jsPandaFile->IsModule(&recordInfo)) {
        moduleRecord = ModuleDataExtractor::ParseModule(
            thread, jsPandaFile, moduleFileName, moduleFileName, &recordInfo);
    } else {
        ASSERT(jsPandaFile->IsCjs(&recordInfo));
        moduleRecord = ModuleDataExtractor::ParseCjsModule(thread, jsPandaFile);
    }
    // json file can not be compiled into isolate abc.
    ASSERT(!jsPandaFile->IsJson(&recordInfo));
    ModuleDeregister::InitForDeregisterModule(moduleRecord, executeFromJob);
    AddResolveImportedModule(moduleFileName, moduleRecord.GetTaggedValue());
    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleManager::ResolveNativeModule(const CString &moduleRequest,
    const CString &baseFileName, ModuleTypes moduleType)
{
    JSThread *thread = vm_->GetJSThread();
    JSHandle<JSTaggedValue> moduleRecord = ModuleDataExtractor::ParseNativeModule(thread,
        moduleRequest, baseFileName, moduleType);
    AddResolveImportedModule(moduleRequest, moduleRecord.GetTaggedValue());
    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleManager::ResolveModuleWithMerge(
    JSThread *thread, const JSPandaFile *jsPandaFile, const CString &recordName, bool executeFromJob)
{
    CString moduleFileName = jsPandaFile->GetJSPandaFileDesc();
    JSHandle<JSTaggedValue> moduleRecord = thread->GlobalConstants()->GetHandledUndefined();
    JSRecordInfo *recordInfo = nullptr;
    bool hasRecord = jsPandaFile->CheckAndGetRecordInfo(recordName, &recordInfo);
    if (!hasRecord) {
        JSHandle<JSTaggedValue> exp(thread, JSTaggedValue::Exception());
        THROW_MODULE_NOT_FOUND_ERROR_WITH_RETURN_VALUE(thread, recordName, moduleFileName, exp);
    }
    if (jsPandaFile->IsModule(recordInfo)) {
        RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
        moduleRecord = ModuleDataExtractor::ParseModule(thread, jsPandaFile, recordName, moduleFileName, recordInfo);
    } else if (jsPandaFile->IsJson(recordInfo)) {
        moduleRecord = ModuleDataExtractor::ParseJsonModule(thread, jsPandaFile, moduleFileName, recordName);
    } else {
        ASSERT(jsPandaFile->IsCjs(recordInfo));
        RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
        moduleRecord = ModuleDataExtractor::ParseCjsModule(thread, jsPandaFile);
    }

    JSHandle<EcmaString> recordNameHdl = vm_->GetFactory()->NewFromUtf8(recordName);
    JSHandle<SourceTextModule>::Cast(moduleRecord)->SetEcmaModuleRecordName(thread, recordNameHdl.GetTaggedValue());
    ModuleDeregister::InitForDeregisterModule(moduleRecord, executeFromJob);
    return moduleRecord;
}

void ModuleManager::AddToInstantiatingSModuleList(const CString &record)
{
    InstantiatingSModuleList_.push_back(record);
}

CVector<CString> ModuleManager::GetInstantiatingSModuleList()
{
    return InstantiatingSModuleList_;
}

void ModuleManager::ClearInstantiatingSModuleList()
{
    InstantiatingSModuleList_.clear();
}

JSTaggedValue ModuleManager::GetModuleNamespace(int32_t index)
{
    JSTaggedValue currentModule = GetCurrentModule();
    return GetModuleNamespaceInternal(index, currentModule);
}

JSTaggedValue ModuleManager::GetModuleNamespace(int32_t index, JSTaggedValue currentFunc)
{
    JSTaggedValue currentModule = JSFunction::Cast(currentFunc.GetTaggedObject())->GetModule();
    return GetModuleNamespaceInternal(index, currentModule);
}

JSTaggedValue ModuleManager::GetModuleNamespaceInternal(int32_t index, JSTaggedValue currentModule)
{
    if (currentModule.IsUndefined()) {
        LOG_FULL(FATAL) << "GetModuleNamespace currentModule failed";
        UNREACHABLE();
    }
    JSThread *thread = vm_->GetJSThread();
    SourceTextModule *module = SourceTextModule::Cast(currentModule.GetTaggedObject());
    JSTaggedValue requestedModule = module->GetRequestedModules();
    JSTaggedValue moduleName = TaggedArray::Cast(requestedModule.GetTaggedObject())->Get(index);
    JSTaggedValue moduleRecordName = module->GetEcmaModuleRecordName();
    JSHandle<JSTaggedValue> requiredModule;
    if (moduleRecordName.IsUndefined()) {
        requiredModule = SourceTextModule::HostResolveImportedModule(thread,
            JSHandle<SourceTextModule>(thread, module), JSHandle<JSTaggedValue>(thread, moduleName));
    } else {
        ASSERT(moduleRecordName.IsString());
        requiredModule = SourceTextModule::HostResolveImportedModuleWithMerge(thread,
            JSHandle<SourceTextModule>(thread, module), JSHandle<JSTaggedValue>(thread, moduleName));
    }
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, JSTaggedValue::Exception());
    JSHandle<SourceTextModule> requiredModuleST = JSHandle<SourceTextModule>::Cast(requiredModule);
    ModuleTypes moduleType = requiredModuleST->GetTypes();
    // if requiredModuleST is Native module
    if (SourceTextModule::IsNativeModule(moduleType)) {
        return SourceTextModule::Cast(requiredModuleST.GetTaggedValue())->GetModuleValue(thread, 0, false);
    }
    // if requiredModuleST is CommonJS
    if (moduleType == ModuleTypes::CJS_MODULE) {
        JSHandle<JSTaggedValue> cjsModuleName(thread,
            SourceTextModule::GetModuleName(requiredModuleST.GetTaggedValue()));
        return CjsModule::SearchFromModuleCache(thread, cjsModuleName).GetTaggedValue();
    }
    // if requiredModuleST is ESM
    JSHandle<JSTaggedValue> moduleNamespace = SourceTextModule::GetModuleNamespace(thread, requiredModuleST);
    ASSERT(moduleNamespace->IsModuleNamespace());
    return moduleNamespace.GetTaggedValue();
}

JSTaggedValue ModuleManager::GetModuleNamespace(JSTaggedValue localName)
{
    JSTaggedValue currentModule = GetCurrentModule();
    return GetModuleNamespaceInternal(localName, currentModule);
}

JSTaggedValue ModuleManager::GetModuleNamespace(JSTaggedValue localName, JSTaggedValue currentFunc)
{
    JSTaggedValue currentModule = JSFunction::Cast(currentFunc.GetTaggedObject())->GetModule();
    return GetModuleNamespaceInternal(localName, currentModule);
}

JSTaggedValue ModuleManager::GetModuleNamespaceInternal(JSTaggedValue localName, JSTaggedValue currentModule)
{
    if (currentModule.IsUndefined()) {
        LOG_FULL(FATAL) << "GetModuleNamespace currentModule failed";
        UNREACHABLE();
    }
    JSTaggedValue moduleEnvironment = SourceTextModule::Cast(currentModule.GetTaggedObject())->GetEnvironment();
    if (moduleEnvironment.IsUndefined()) {
        return vm_->GetJSThread()->GlobalConstants()->GetUndefined();
    }
    int entry = NameDictionary::Cast(moduleEnvironment.GetTaggedObject())->FindEntry(localName);
    if (entry == -1) {
        return vm_->GetJSThread()->GlobalConstants()->GetUndefined();
    }
    JSTaggedValue moduleNamespace = NameDictionary::Cast(moduleEnvironment.GetTaggedObject())->GetValue(entry);
    ASSERT(moduleNamespace.IsModuleNamespace());
    return moduleNamespace;
}

void ModuleManager::Iterate(const RootVisitor &v)
{
    for (auto &it : resolvedModules_) {
        ObjectSlot slot(reinterpret_cast<uintptr_t>(&it.second));
        v(Root::ROOT_VM, slot);
        ASSERT(slot.GetTaggedValue() == it.second);
    }
}

CString ModuleManager::GetRecordName(JSTaggedValue module)
{
    CString entry = "";
    if (module.IsString()) {
        entry = ModulePathHelper::Utf8ConvertToString(module);
    }
    if (module.IsSourceTextModule()) {
        SourceTextModule *sourceTextModule = SourceTextModule::Cast(module.GetTaggedObject());
        JSTaggedValue recordName = sourceTextModule->GetEcmaModuleRecordName();
        if (recordName.IsString()) {
            entry = ModulePathHelper::Utf8ConvertToString(recordName);
        }
    }
    return entry;
}

int ModuleManager::GetExportObjectIndex(EcmaVM *vm, JSHandle<SourceTextModule> ecmaModule,
                                        const CString &key)
{
    JSThread *thread = vm->GetJSThread();
    if (ecmaModule->GetLocalExportEntries().IsUndefined()) {
        CString msg = "No export named '" + key;
        if (!ecmaModule->GetEcmaModuleRecordName().IsUndefined()) {
            msg += "' which exported by '" + ConvertToString(ecmaModule->GetEcmaModuleRecordName()) + "'";
        } else {
            msg += "' which exported by '" + ConvertToString(ecmaModule->GetEcmaModuleFilename()) + "'";
        }
        ObjectFactory *factory = vm->GetFactory();
        JSTaggedValue error = factory->GetJSError(ErrorType::SYNTAX_ERROR, msg.c_str(),
                                                  StackCheck::NO).GetTaggedValue();
        THROW_NEW_ERROR_AND_RETURN_VALUE(thread, error, 0);
    }
    JSHandle<TaggedArray> localExportEntries(thread, ecmaModule->GetLocalExportEntries());
    size_t exportEntriesLen = localExportEntries->GetLength();
    // 0: There's only one export value "default"
    int index = 0;
    JSMutableHandle<LocalExportEntry> ee(thread, thread->GlobalConstants()->GetUndefined());
    if (exportEntriesLen > 1) { // 1:  The number of export objects exceeds 1
        for (size_t idx = 0; idx < exportEntriesLen; idx++) {
            ee.Update(localExportEntries->Get(idx));
            if (EcmaStringAccessor(ee->GetExportName()).Utf8ConvertToString() == key) {
                ASSERT(idx <= static_cast<size_t>(INT_MAX));
                index = static_cast<int>(ee->GetLocalIndex());
                break;
            }
        }
    }
    return index;
}

JSHandle<JSTaggedValue> ModuleManager::HostResolveImportedModule(const JSPandaFile *jsPandaFile,
                                                                 const CString &filename)
{
    if (jsPandaFile == nullptr) {
        LOG_FULL(FATAL) << "Load current file's panda file failed. Current file is " << filename;
    }
    JSThread *thread = vm_->GetJSThread();
    auto entry = resolvedModules_.find(filename);
    if (entry == resolvedModules_.end()) {
        return ResolveModule(thread, jsPandaFile);
    }
    return JSHandle<JSTaggedValue>(thread, entry->second);
}

JSHandle<JSTaggedValue> ModuleManager::LoadNativeModule(JSThread *thread, const CString &key)
{
    JSHandle<SourceTextModule> ecmaModule = JSHandle<SourceTextModule>::Cast(ExecuteNativeModule(thread, key));
    ASSERT(ecmaModule->GetIsNewBcVersion());
    int index = GetExportObjectIndex(vm_, ecmaModule, key);
    JSTaggedValue result = ecmaModule->GetModuleValue(thread, index, false);
    return JSHandle<JSTaggedValue>(thread, result);
}

JSHandle<JSTaggedValue> ModuleManager::ExecuteNativeModuleMayThrowError(JSThread *thread, const CString &recordName)
{
    JSMutableHandle<JSTaggedValue> requiredModule(thread, thread->GlobalConstants()->GetUndefined());
    if (IsEvaluatedModule(recordName)) {
        JSHandle<SourceTextModule> moduleRecord = HostGetImportedModule(recordName);
        return JSHandle<JSTaggedValue>(thread, moduleRecord->GetModuleValue(thread, 0, false));
    }

    auto [isNative, moduleType] = SourceTextModule::CheckNativeModule(recordName);
    JSHandle<JSTaggedValue> moduleRecord = ModuleDataExtractor::ParseNativeModule(thread,
        recordName, "", moduleType);
    JSHandle<SourceTextModule> nativeModule =
        JSHandle<SourceTextModule>::Cast(moduleRecord);
    auto exportObject = SourceTextModule::LoadNativeModuleMayThrowError(thread, nativeModule, moduleType);
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread,
        JSHandle<JSTaggedValue>(thread, JSTaggedValue::Undefined()));
    nativeModule->SetStatus(ModuleStatus::EVALUATED);
    nativeModule->SetLoadingTypes(LoadingTypes::STABLE_MODULE);
    nativeModule->StoreModuleValue(thread, 0, JSNApiHelper::ToJSHandle(exportObject));
    AddResolveImportedModule(recordName, moduleRecord.GetTaggedValue());
    return JSNApiHelper::ToJSHandle(exportObject);
}

JSHandle<JSTaggedValue> ModuleManager::ExecuteNativeModule(JSThread *thread, const CString &recordName)
{
    JSMutableHandle<JSTaggedValue> requiredModule(thread, thread->GlobalConstants()->GetUndefined());
    if (IsEvaluatedModule(recordName)) {
        JSHandle<SourceTextModule> moduleRecord = HostGetImportedModule(recordName);
        requiredModule.Update(moduleRecord);
    } else {
        auto [isNative, moduleType] = SourceTextModule::CheckNativeModule(recordName);
        JSHandle<JSTaggedValue> nativeModuleHandle =
            ResolveNativeModule(recordName, "", moduleType);
        JSHandle<SourceTextModule> nativeModule =
            JSHandle<SourceTextModule>::Cast(nativeModuleHandle);
        if (!SourceTextModule::LoadNativeModule(thread, nativeModule, moduleType)) {
            LOG_FULL(ERROR) << "loading native module " << recordName << " failed";
        }
        nativeModule->SetStatus(ModuleStatus::EVALUATED);
        nativeModule->SetLoadingTypes(LoadingTypes::STABLE_MODULE);
        requiredModule.Update(nativeModule);
    }
    return requiredModule;
}

JSHandle<JSTaggedValue> ModuleManager::ExecuteJsonModule(JSThread *thread, const CString &recordName,
    const CString &filename, const JSPandaFile *jsPandaFile)
{
    JSMutableHandle<JSTaggedValue> requiredModule(thread, thread->GlobalConstants()->GetUndefined());
    if (IsEvaluatedModule(recordName)) {
        JSHandle<SourceTextModule> moduleRecord = HostGetImportedModule(recordName);
        requiredModule.Update(moduleRecord);
    } else {
        JSHandle<SourceTextModule> moduleRecord =
            JSHandle<SourceTextModule>::Cast(ModuleDataExtractor::ParseJsonModule(thread, jsPandaFile, filename,
                                                                                  recordName));
        moduleRecord->SetStatus(ModuleStatus::EVALUATED);
        requiredModule.Update(moduleRecord);
    }
    return requiredModule;
}

JSHandle<JSTaggedValue> ModuleManager::ExecuteCjsModule(JSThread *thread, const CString &recordName,
    const JSPandaFile *jsPandaFile)
{
    ObjectFactory *factory = vm_->GetFactory();
    CString entryPoint = JSPandaFile::ENTRY_FUNCTION_NAME;
    CString moduleRecord = jsPandaFile->GetJSPandaFileDesc();
    if (!jsPandaFile->IsBundlePack()) {
        entryPoint = recordName;
        moduleRecord = recordName;
    }

    JSMutableHandle<JSTaggedValue> requiredModule(thread, thread->GlobalConstants()->GetUndefined());
    if (IsEvaluatedModule(moduleRecord)) {
        requiredModule.Update(HostGetImportedModule(moduleRecord));
    } else {
        JSHandle<SourceTextModule> module =
            JSHandle<SourceTextModule>::Cast(ModuleDataExtractor::ParseCjsModule(thread, jsPandaFile));
        JSHandle<EcmaString> record = factory->NewFromASCII(moduleRecord);
        module->SetEcmaModuleRecordName(thread, record);
        requiredModule.Update(module);
        JSPandaFileExecutor::Execute(thread, jsPandaFile, entryPoint);
        RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, requiredModule);
        module->SetStatus(ModuleStatus::EVALUATED);
    }
    return requiredModule;
}

JSHandle<JSTaggedValue> ModuleManager::GetModuleNameSpaceFromFile(
    JSThread *thread, const CString &recordName, const CString &baseFileName)
{
    if (!IsLocalModuleLoaded(recordName)) {
        if (!ecmascript::JSPandaFileExecutor::ExecuteFromAbcFile(
            thread, baseFileName, recordName, false, true)) {
            LOG_ECMA(ERROR) << "LoadModuleNameSpaceFromFile:Cannot execute module: "<< baseFileName <<
                ", recordName: " << recordName;
            return thread->GlobalConstants()->GetHandledUndefinedString();
        }
    }
    JSHandle<ecmascript::SourceTextModule> moduleRecord = GetImportedModule(recordName);
    moduleRecord->SetLoadingTypes(ecmascript::LoadingTypes::STABLE_MODULE);
    return ecmascript::SourceTextModule::GetModuleNamespace(
        thread, JSHandle<ecmascript::SourceTextModule>(moduleRecord));
}

JSHandle<JSTaggedValue> ModuleManager::TryGetImportedModule(const CString& referencing)
{
    JSThread *thread = vm_->GetJSThread();
    auto entry = resolvedModules_.find(referencing);
    if (entry == resolvedModules_.end()) {
        return thread->GlobalConstants()->GetHandledUndefined();
    }
    return JSHandle<JSTaggedValue>(thread, entry->second);
}

void ModuleManager::RemoveModuleFromCache(const CString& recordName)
{
    auto entry = resolvedModules_.find(recordName);
    if (entry == resolvedModules_.end()) {
        LOG_ECMA(FATAL) << "Can not get module: " << recordName <<
            ", when try to remove the module";
    }
    JSTaggedValue result = entry->second;
    SourceTextModule::Cast(result)->DestoryLazyImportArray();
    resolvedModules_.erase(recordName);
}

// this function only remove module's name from resolvedModules List, it's content still needed by sharedmodule
void ModuleManager::RemoveModuleNameFromList(const CString& recordName)
{
    auto entry = resolvedModules_.find(recordName);
    if (entry == resolvedModules_.end()) {
        LOG_ECMA(FATAL) << "Can not get module: " << recordName <<
            ", when try to remove the module";
    }
    resolvedModules_.erase(recordName);
}
} // namespace panda::ecmascript
