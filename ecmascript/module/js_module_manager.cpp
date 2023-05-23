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

#include "ecmascript/base/path_helper.h"
#include "ecmascript/compiler/aot_file/aot_file_manager.h"
#include "ecmascript/global_env.h"
#include "ecmascript/interpreter/fast_runtime_stub-inl.h"
#include "ecmascript/interpreter/frame_handler.h"
#include "ecmascript/js_array.h"
#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/jspandafile/js_pandafile_executor.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/module/js_module_source_text.h"
#include "ecmascript/module/module_data_extractor.h"
#include "ecmascript/require/js_cjs_module.h"
#include "ecmascript/tagged_dictionary.h"
#ifdef PANDA_TARGET_WINDOWS
#include <algorithm>
#endif

namespace panda::ecmascript {
using PathHelper = base::PathHelper;
using StringHelper = base::StringHelper;
ModuleManager::ModuleManager(EcmaVM *vm) : vm_(vm)
{
    resolvedModules_ = NameDictionary::Create(vm_->GetJSThread(), DEAULT_DICTIONART_CAPACITY).GetTaggedValue();
}

JSTaggedValue ModuleManager::GetCurrentModule()
{
    FrameHandler frameHandler(vm_->GetJSThread());
    JSTaggedValue currentFunc = frameHandler.GetFunction();
    return JSFunction::Cast(currentFunc.GetTaggedObject())->GetModule();
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

JSTaggedValue ModuleManager::GetModuleValueOutter(int32_t index)
{
    JSTaggedValue currentModule = GetCurrentModule();
    return GetModuleValueOutterInternal(index, currentModule);
}

JSTaggedValue ModuleManager::GetModuleName(JSTaggedValue currentModule)
{
    SourceTextModule *module = SourceTextModule::Cast(currentModule.GetTaggedObject());
    JSTaggedValue recordName = module->GetEcmaModuleRecordName();
    if (recordName.IsUndefined()) {
        return module->GetEcmaModuleFilename();
    }

    return recordName;
}

JSTaggedValue ModuleManager::GetModuleValueOutter(int32_t index, JSTaggedValue jsFunc)
{
    JSTaggedValue currentModule = JSFunction::Cast(jsFunc.GetTaggedObject())->GetModule();
    return GetModuleValueOutterInternal(index, currentModule);
}

JSTaggedValue ModuleManager::GetModuleValueOutterInternal(int32_t index, JSTaggedValue currentModule)
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
    ASSERT(moduleEnvironment.IsTaggedArray());
    JSTaggedValue resolvedBinding = TaggedArray::Cast(moduleEnvironment.GetTaggedObject())->Get(index);
    if (resolvedBinding.IsResolvedIndexBinding()) {
        ResolvedIndexBinding *binding = ResolvedIndexBinding::Cast(resolvedBinding.GetTaggedObject());
        JSTaggedValue resolvedModule = binding->GetModule();
        ASSERT(resolvedModule.IsSourceTextModule());
        SourceTextModule *module = SourceTextModule::Cast(resolvedModule.GetTaggedObject());

        ModuleTypes moduleType = module->GetTypes();
        if (IsNativeModule(moduleType)) {
            return GetNativeModuleValue(thread, currentModule, resolvedModule, binding);
        }
        if (module->GetTypes() == ModuleTypes::CJS_MODULE) {
            return GetCJSModuleValue(thread, currentModule, resolvedModule, binding);
        }
        return SourceTextModule::Cast(
            resolvedModule.GetTaggedObject())->GetModuleValue(thread, binding->GetIndex(), false);
    }
    ResolvedBinding *binding = ResolvedBinding::Cast(resolvedBinding.GetTaggedObject());
    JSTaggedValue resolvedModule = binding->GetModule();
    SourceTextModule *module = SourceTextModule::Cast(resolvedModule.GetTaggedObject());
    if (module->GetTypes() == ModuleTypes::CJS_MODULE) {
        JSHandle<JSTaggedValue> cjsModuleName(thread, GetModuleName(JSTaggedValue(module)));
        return CjsModule::SearchFromModuleCache(thread, cjsModuleName).GetTaggedValue();
    }
    LOG_ECMA(FATAL) << "Get module value failed, mistaken ResolvedBinding";
    UNREACHABLE();
}

JSTaggedValue ModuleManager::GetNativeModuleValue(JSThread *thread, JSTaggedValue currentModule,
    JSTaggedValue resolvedModule, ResolvedIndexBinding *binding)
{
    JSHandle<JSTaggedValue> nativeModuleName(thread, GetModuleName(resolvedModule));
    JSHandle<JSTaggedValue> nativeExports = JSHandle<JSTaggedValue>(thread,
        SourceTextModule::Cast(resolvedModule.GetTaggedObject())->GetModuleValue(thread, 0, false));
    if (!nativeExports->IsJSObject()) {
        JSHandle<JSTaggedValue> curModuleName(thread, SourceTextModule::Cast(
            currentModule.GetTaggedObject())->GetEcmaModuleFilename());
        LOG_FULL(WARN) << "GetNativeModuleValue: currentModule " + ConvertToString(curModuleName.GetTaggedValue()) +
            ", find requireModule " + ConvertToString(nativeModuleName.GetTaggedValue()) + " failed";
        return nativeExports.GetTaggedValue();
    }
    return GetValueFromExportObject(nativeExports, binding->GetIndex());
}

JSTaggedValue ModuleManager::GetCJSModuleValue(JSThread *thread, JSTaggedValue currentModule,
    JSTaggedValue resolvedModule, ResolvedIndexBinding *binding)
{
    JSHandle<JSTaggedValue> cjsModuleName(thread, GetModuleName(resolvedModule));
    JSHandle<JSTaggedValue> cjsExports = CjsModule::SearchFromModuleCache(thread, cjsModuleName);
    // if cjsModule is not JSObject, means cjs uses default exports.
    if (!cjsExports->IsJSObject()) {
        if (cjsExports->IsHole()) {
            ObjectFactory *factory = vm_->GetFactory();
            JSHandle<JSTaggedValue> curModuleName(thread, SourceTextModule::Cast(
                currentModule.GetTaggedObject())->GetEcmaModuleFilename());
            CString errorMsg = "GetCJSModuleValue: currentModule" + ConvertToString(curModuleName.GetTaggedValue()) +
                                "find requireModule" + ConvertToString(cjsModuleName.GetTaggedValue()) + "failed";
            JSHandle<JSObject> syntaxError =
                factory->GetJSError(base::ErrorType::SYNTAX_ERROR, errorMsg.c_str());
            THROW_NEW_ERROR_AND_RETURN_VALUE(thread, syntaxError.GetTaggedValue(), JSTaggedValue::Exception());
        }
        return cjsExports.GetTaggedValue();
    }
    return GetValueFromExportObject(cjsExports, binding->GetIndex());
}

JSTaggedValue ModuleManager::GetValueFromExportObject(JSHandle<JSTaggedValue> &exportObject, int32_t index)
{
    if (index == SourceTextModule::UNDEFINED_INDEX) {
        return exportObject.GetTaggedValue();
    }
    JSObject *obj = JSObject::Cast(exportObject.GetTaggedValue());
    JSHClass *jsHclass = obj->GetJSHClass();
    LayoutInfo *layoutInfo = LayoutInfo::Cast(jsHclass->GetLayout().GetTaggedObject());
    PropertyAttributes attr = layoutInfo->GetAttr(index);
    JSTaggedValue value = obj->GetProperty(jsHclass, attr);
    if (UNLIKELY(value.IsAccessor())) {
        return FastRuntimeStub::CallGetter(vm_->GetJSThread(), JSTaggedValue(obj), JSTaggedValue(obj), value);
    }
    ASSERT(!value.IsAccessor());
    return value;
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
        JSHandle<JSTaggedValue> cjsModuleName(thread, GetModuleName(JSTaggedValue(module)));
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

JSHandle<SourceTextModule> ModuleManager::HostGetImportedModule(const CString &referencingModule)
{
    ObjectFactory *factory = vm_->GetFactory();
    JSHandle<EcmaString> referencingHandle = factory->NewFromUtf8(referencingModule);
    return HostGetImportedModule(referencingHandle.GetTaggedValue());
}

JSHandle<SourceTextModule> ModuleManager::HostGetImportedModule(JSTaggedValue referencing)
{
    NameDictionary *dict = NameDictionary::Cast(resolvedModules_.GetTaggedObject());
    int entry = dict->FindEntry(referencing);
    LOG_ECMA_IF(entry == -1, FATAL) << "Can not get module: "
                                    << ConvertToString(referencing);
    JSTaggedValue result = dict->GetValue(entry);
    return JSHandle<SourceTextModule>(vm_->GetJSThread(), result);
}

bool ModuleManager::IsImportedModuleLoaded(JSTaggedValue referencing)
{
    int entry = NameDictionary::Cast(resolvedModules_.GetTaggedObject())->FindEntry(referencing);
    return (entry != -1);
}

bool ModuleManager::SkipDefaultBundleFile(const CString &moduleFileName) const
{
    // relative file path like "../../xxxx" can't be loaded rightly in aot compilation phase
    const char relativeFilePath[] = "..";
    // just to skip misunderstanding error log in LoadJSPandaFile when we ignore Module Resolving Failure.
    return !vm_->EnableReportModuleResolvingFailure() &&
        (base::StringHelper::StringStartWith(moduleFileName, PathHelper::BUNDLE_INSTALL_PATH) ||
        base::StringHelper::StringStartWith(moduleFileName, relativeFilePath));
}

JSHandle<JSTaggedValue> ModuleManager::ResolveModuleInMergedABC(JSThread *thread, const JSPandaFile *jsPandaFile,
                                                                const CString &recordName)
{
    // In static parse Phase, due to lack of some parameters, we will create a empty SourceTextModule which will
    // be marked as INSTANTIATED to skip Dfs traversal of this import branch.
    if (!vm_->EnableReportModuleResolvingFailure() && (jsPandaFile == nullptr ||
        (jsPandaFile != nullptr && !jsPandaFile->HasRecord(recordName)))) {
        return CreateEmptyModule();
    } else {
        return ResolveModuleWithMerge(thread, jsPandaFile, recordName);
    }
}

JSHandle<JSTaggedValue> ModuleManager::HostResolveImportedModuleWithMerge(const CString &moduleFileName,
                                                                          const CString &recordName)
{
    JSThread *thread = vm_->GetJSThread();
    ObjectFactory *factory = vm_->GetFactory();

    JSHandle<EcmaString> recordNameHandle = factory->NewFromUtf8(recordName);
    NameDictionary *dict = NameDictionary::Cast(resolvedModules_.GetTaggedObject());
    int entry = dict->FindEntry(recordNameHandle.GetTaggedValue());
    if (entry != -1) {
        return JSHandle<JSTaggedValue>(thread, dict->GetValue(entry));
    }
    std::shared_ptr<JSPandaFile> jsPandaFile = SkipDefaultBundleFile(moduleFileName) ? nullptr :
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, moduleFileName, recordName, false);
    if (jsPandaFile == nullptr) {
        // In Aot Module Instantiate, we miss some runtime parameters from framework like bundleName or moduleName
        // which may cause wrong recordName parsing and we also can't load files not in this app hap. But in static
        // phase, these should not be an error, just skip it is ok.
        if (vm_->EnableReportModuleResolvingFailure()) {
            CString msg = "Load file with filename '" + moduleFileName + "' failed, recordName '" + recordName + "'";
            THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
        }
    }

    JSHandle<JSTaggedValue> moduleRecord = ResolveModuleInMergedABC(thread, jsPandaFile.get(), recordName);
    RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
    JSHandle<NameDictionary> handleDict(thread, resolvedModules_);
    resolvedModules_ = NameDictionary::Put(thread, handleDict, JSHandle<JSTaggedValue>(recordNameHandle),
                                           moduleRecord, PropertyAttributes::Default()).GetTaggedValue();

    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleManager::CreateEmptyModule()
{
    if (!cachedEmptyModule_.IsHole()) {
        return JSHandle<JSTaggedValue>(vm_->GetJSThread(), cachedEmptyModule_);
    }
    ObjectFactory *factory = vm_->GetFactory();
    JSHandle<SourceTextModule> tmpModuleRecord = factory->NewSourceTextModule();
    tmpModuleRecord->SetStatus(ModuleStatus::INSTANTIATED);
    tmpModuleRecord->SetTypes(ModuleTypes::ECMA_MODULE);
    tmpModuleRecord->SetIsNewBcVersion(true);
    cachedEmptyModule_ = tmpModuleRecord.GetTaggedValue();
    return JSHandle<JSTaggedValue>::Cast(tmpModuleRecord);
}

JSHandle<JSTaggedValue> ModuleManager::HostResolveImportedModule(const CString &referencingModule)
{
    JSThread *thread = vm_->GetJSThread();
    ObjectFactory *factory = vm_->GetFactory();

    JSHandle<EcmaString> referencingHandle = factory->NewFromUtf8(referencingModule);
    CString moduleFileName = referencingModule;
    if (vm_->IsBundlePack()) {
        if (AOTFileManager::GetAbsolutePath(referencingModule, moduleFileName)) {
            referencingHandle = factory->NewFromUtf8(moduleFileName);
        } else {
            CString msg = "Parse absolute " + referencingModule + " path failed";
            THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
        }
    }

    NameDictionary *dict = NameDictionary::Cast(resolvedModules_.GetTaggedObject());
    int entry = dict->FindEntry(referencingHandle.GetTaggedValue());
    if (entry != -1) {
        return JSHandle<JSTaggedValue>(thread, dict->GetValue(entry));
    }

    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, moduleFileName, JSPandaFile::ENTRY_MAIN_FUNCTION);
    if (jsPandaFile == nullptr) {
        CString msg = "Load file with filename '" + moduleFileName + "' failed";
        THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
    }

    return ResolveModule(thread, jsPandaFile.get());
}

// The security interface needs to be modified accordingly.
JSHandle<JSTaggedValue> ModuleManager::HostResolveImportedModule(const void *buffer, size_t size,
                                                                 const CString &filename)
{
    JSThread *thread = vm_->GetJSThread();
    ObjectFactory *factory = vm_->GetFactory();

    JSHandle<EcmaString> referencingHandle = factory->NewFromUtf8(filename);
    NameDictionary *dict = NameDictionary::Cast(resolvedModules_.GetTaggedObject());
    int entry = dict->FindEntry(referencingHandle.GetTaggedValue());
    if (entry != -1) {
        return JSHandle<JSTaggedValue>(thread, dict->GetValue(entry));
    }

    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, filename,
                                                           JSPandaFile::ENTRY_MAIN_FUNCTION, buffer, size);
    if (jsPandaFile == nullptr) {
        CString msg = "Load file with filename '" + filename + "' failed";
        THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
    }

    return ResolveModule(thread, jsPandaFile.get());
}

JSHandle<JSTaggedValue> ModuleManager::ResolveModule(JSThread *thread, const JSPandaFile *jsPandaFile)
{
    ObjectFactory *factory = vm_->GetFactory();
    CString moduleFileName = jsPandaFile->GetJSPandaFileDesc();
    JSHandle<JSTaggedValue> moduleRecord = thread->GlobalConstants()->GetHandledUndefined();
    if (jsPandaFile->IsModule(thread)) {
        moduleRecord = ModuleDataExtractor::ParseModule(thread, jsPandaFile, moduleFileName, moduleFileName);
    } else if (jsPandaFile->IsJson(thread)) {
        moduleRecord = ModuleDataExtractor::ParseJsonModule(thread, jsPandaFile, moduleFileName);
    } else {
        ASSERT(jsPandaFile->IsCjs(thread));
        moduleRecord = ModuleDataExtractor::ParseCjsModule(thread, jsPandaFile);
    }

    JSHandle<NameDictionary> dict(thread, resolvedModules_);
    JSHandle<JSTaggedValue> referencingHandle = JSHandle<JSTaggedValue>::Cast(factory->NewFromUtf8(moduleFileName));
    resolvedModules_ =
        NameDictionary::Put(thread, dict, referencingHandle, moduleRecord, PropertyAttributes::Default())
        .GetTaggedValue();
    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleManager::ResolveNativeModule(const CString &moduleRequestName, ModuleTypes moduleType)
{
    ObjectFactory *factory = vm_->GetFactory();
    JSThread *thread = vm_->GetJSThread();

    JSHandle<JSTaggedValue> referencingModule(factory->NewFromUtf8(moduleRequestName));
    JSHandle<JSTaggedValue> moduleRecord = ModuleDataExtractor::ParseNativeModule(thread,
        moduleRequestName, moduleType);
    JSHandle<NameDictionary> dict(thread, resolvedModules_);
    resolvedModules_ = NameDictionary::Put(thread, dict, referencingModule, moduleRecord,
        PropertyAttributes::Default()).GetTaggedValue();
    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleManager::ResolveModuleWithMerge(
    JSThread *thread, const JSPandaFile *jsPandaFile, const CString &recordName)
{
    ObjectFactory *factory = vm_->GetFactory();
    CString moduleFileName = jsPandaFile->GetJSPandaFileDesc();
    JSHandle<JSTaggedValue> moduleRecord = thread->GlobalConstants()->GetHandledUndefined();
    if (jsPandaFile->IsModule(thread, recordName)) {
        RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
        moduleRecord = ModuleDataExtractor::ParseModule(thread, jsPandaFile, recordName, moduleFileName);
    } else if (jsPandaFile->IsJson(thread, recordName)) {
        moduleRecord = ModuleDataExtractor::ParseJsonModule(thread, jsPandaFile, moduleFileName, recordName);
    } else {
        ASSERT(jsPandaFile->IsCjs(thread, recordName));
        RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
        moduleRecord = ModuleDataExtractor::ParseCjsModule(thread, jsPandaFile);
    }

    JSHandle<JSTaggedValue> recordNameHandle = JSHandle<JSTaggedValue>::Cast(factory->NewFromUtf8(recordName));
    JSHandle<SourceTextModule>::Cast(moduleRecord)->SetEcmaModuleRecordName(thread, recordNameHandle);
    return moduleRecord;
}

void ModuleManager::AddResolveImportedModule(const JSPandaFile *jsPandaFile, const CString &referencingModule)
{
    JSThread *thread = vm_->GetJSThread();
    JSHandle<JSTaggedValue> moduleRecord =
        ModuleDataExtractor::ParseModule(thread, jsPandaFile, referencingModule, referencingModule);
    AddResolveImportedModule(referencingModule, moduleRecord);
}

void ModuleManager::AddResolveImportedModule(const CString &referencingModule, JSHandle<JSTaggedValue> moduleRecord)
{
    JSThread *thread = vm_->GetJSThread();
    ObjectFactory *factory = vm_->GetFactory();
    JSHandle<JSTaggedValue> referencingHandle(factory->NewFromUtf8(referencingModule));
    JSHandle<NameDictionary> dict(thread, resolvedModules_);
    resolvedModules_ =
        NameDictionary::Put(thread, dict, referencingHandle, moduleRecord, PropertyAttributes::Default())
        .GetTaggedValue();
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
    if (ModuleManager::IsNativeModule(moduleType)) {
        return SourceTextModule::Cast(requiredModuleST.GetTaggedValue())->GetModuleValue(thread, 0, false);
    }
    // if requiredModuleST is CommonJS
    if (moduleType == ModuleTypes::CJS_MODULE) {
        JSHandle<JSTaggedValue> cjsModuleName(thread, GetModuleName(requiredModuleST.GetTaggedValue()));
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
    v(Root::ROOT_VM, ObjectSlot(reinterpret_cast<uintptr_t>(&resolvedModules_)));
    v(Root::ROOT_VM, ObjectSlot(reinterpret_cast<uintptr_t>(&cachedEmptyModule_)));
}

CString ModuleManager::GetRecordName(JSTaggedValue module)
{
    CString entry = "";
    if (module.IsString()) {
        entry = ConvertToString(module);
    }
    if (module.IsSourceTextModule()) {
        SourceTextModule *sourceTextModule = SourceTextModule::Cast(module.GetTaggedObject());
        if (sourceTextModule->GetEcmaModuleRecordName().IsString()) {
            entry = ConvertToString(sourceTextModule->GetEcmaModuleRecordName());
        }
    }
    return entry;
}

int ModuleManager::GetExportObjectIndex(EcmaVM *vm, JSHandle<SourceTextModule> ecmaModule,
                                        const std::string &key)
{
    JSThread *thread = vm->GetJSThread();
    JSHandle<TaggedArray> localExportEntries(thread, ecmaModule->GetLocalExportEntries());
    size_t exportEntriesLen = localExportEntries->GetLength();
    // 0: There's only one export value "default"
    int index = 0;
    JSMutableHandle<LocalExportEntry> ee(thread, thread->GlobalConstants()->GetUndefined());
    if (exportEntriesLen > 1) { // 1:  The number of export objects exceeds 1
        for (size_t idx = 0; idx < exportEntriesLen; idx++) {
            ee.Update(localExportEntries->Get(idx));
            if (EcmaStringAccessor(ee->GetExportName()).ToStdString() == key) {
                ASSERT(idx <= static_cast<size_t>(INT_MAX));
                index = static_cast<int>(ee->GetLocalIndex());
                break;
            }
        }
    }
    return index;
}

std::pair<bool, ModuleTypes> ModuleManager::CheckNativeModule(const CString &moduleRequestName)
{
    if (moduleRequestName[0] != '@' ||
        StringHelper::StringStartWith(moduleRequestName, PathHelper::PREFIX_BUNDLE) ||
        StringHelper::StringStartWith(moduleRequestName, PathHelper::PREFIX_PACKAGE)||
        moduleRequestName.find(':') == CString::npos) {
        return {false, ModuleTypes::UNKNOWN};
    }
    if (StringHelper::StringStartWith(moduleRequestName, PathHelper::REQUIRE_NAPI_OHOS_PREFIX)) {
        return {true, ModuleTypes::OHOS_MODULE};
    } else if (StringHelper::StringStartWith(moduleRequestName, PathHelper::REQUIRE_NAPI_APP_PREFIX)) {
        return {true, ModuleTypes::APP_MODULE};
    } else if (StringHelper::StringStartWith(moduleRequestName, PathHelper::REQUIRE_NAITVE_MODULE_PREFIX)) {
        return {true, ModuleTypes::NATIVE_MODULE};
    } else {
        return {true, ModuleTypes::INTERNAL_MODULE};
    }
}

CString ModuleManager::GetStrippedModuleName(const CString &moduleRequestName)
{
    size_t pos = moduleRequestName.find(':');
    if (pos == CString::npos) {
        LOG_FULL(FATAL) << "Unknown format " << moduleRequestName;
    }
    return moduleRequestName.substr(pos + 1);
}

CString ModuleManager::GetInternalModuleDir(const CString &moduleRequestName)
{
    // @xxx:* -> xxx
    size_t pos = moduleRequestName.find(':');
    if (pos == CString::npos) {
        LOG_FULL(FATAL) << "Unknown format " << moduleRequestName;
    }
    return moduleRequestName.substr(1, pos - 1);
}

Local<JSValueRef> ModuleManager::GetRequireNativeModuleFunc(EcmaVM *vm, ModuleTypes moduleType)
{
    Local<ObjectRef> globalObject = JSNApi::GetGlobalObject(vm);
    auto globalConstants = vm->GetJSThread()->GlobalConstants();
    auto funcName = (moduleType == ModuleTypes::NATIVE_MODULE) ?
        globalConstants->GetHandledRequireNativeModuleString() :
        globalConstants->GetHandledRequireNapiString();
    return globalObject->Get(vm, JSNApiHelper::ToLocal<StringRef>(funcName));
}

void ModuleManager::MakeAppArgs(const EcmaVM *vm, std::vector<Local<JSValueRef>> &arguments,
                                const CString &moduleName)
{
    size_t pos = moduleName.find_last_of('/');
    if (pos == CString::npos) {
        LOG_FULL(FATAL) << "Invalid native module " << moduleName;
        UNREACHABLE();
    }
    CString soName = moduleName.substr(pos + 1);
    CString path = moduleName.substr(0, pos);
    // use module name as so name
    arguments[0] = StringRef::NewFromUtf8(vm, soName.c_str());
    arguments.emplace_back(BooleanRef::New(vm, true));
    arguments.emplace_back(StringRef::NewFromUtf8(vm, path.c_str()));
}

void ModuleManager::MakeInternalArgs(const EcmaVM *vm, std::vector<Local<JSValueRef>> &arguments,
                                     const CString &moduleRequestName)
{
    arguments.emplace_back(BooleanRef::New(vm, false));
    arguments.emplace_back(StringRef::NewFromUtf8(vm, ""));
    CString moduleDir = GetInternalModuleDir(moduleRequestName);
    arguments.emplace_back(StringRef::NewFromUtf8(vm, moduleDir.c_str()));
}

bool ModuleManager::LoadNativeModule(JSThread *thread, JSHandle<SourceTextModule> &requiredModule,
    const JSHandle<JSTaggedValue> &moduleRequest, ModuleTypes moduleType)
{
    EcmaVM *vm = thread->GetEcmaVM();
    LocalScope scope(vm);

    CString moduleRequestName = ConvertToString(EcmaString::Cast(moduleRequest->GetTaggedObject()));
    CString moduleName = GetStrippedModuleName(moduleRequestName);
    std::vector<Local<JSValueRef>> arguments;
    LOG_FULL(DEBUG) << "Request module is " << moduleRequestName;

    arguments.emplace_back(StringRef::NewFromUtf8(vm, moduleName.c_str()));
    if (moduleType == ModuleTypes::APP_MODULE) {
        MakeAppArgs(vm, arguments, moduleName);
    } else if (moduleType == ModuleTypes::INTERNAL_MODULE) {
        MakeInternalArgs(vm, arguments, moduleRequestName);
    }
    auto maybeFuncRef = GetRequireNativeModuleFunc(vm, moduleType);
    // some function(s) may not registered in global object for non-main thread
    if (!maybeFuncRef->IsFunction()) {
        LOG_FULL(WARN) << "Not found require func";
        return false;
    }

    Local<FunctionRef> funcRef = maybeFuncRef;
    auto exportObject = funcRef->Call(vm, JSValueRef::Undefined(vm), arguments.data(), arguments.size());
    if (UNLIKELY(thread->HasPendingException())) {
        thread->ClearException();
        LOG_FULL(ERROR) << "LoadNativeModule has exception";
        return false;
    }
    requiredModule->StoreModuleValue(thread, 0, JSNApiHelper::ToJSHandle(exportObject));
    return true;
}

JSHandle<JSTaggedValue> ModuleManager::HostResolveImportedModule(const JSPandaFile *jsPandaFile,
                                                                 const CString &filename)
{
    JSThread *thread = vm_->GetJSThread();
    JSHandle<EcmaString> referencingHandle = vm_->GetFactory()->NewFromUtf8(filename);
    NameDictionary *dict = NameDictionary::Cast(resolvedModules_.GetTaggedObject());
    int entry = dict->FindEntry(referencingHandle.GetTaggedValue());
    if (entry != -1) {
        return JSHandle<JSTaggedValue>(thread, dict->GetValue(entry));
    }

    if (jsPandaFile == nullptr) {
        CString msg = "Faild to resolve file '" + filename + "', please check the request path.";
        THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
    }
    return ResolveModule(thread, jsPandaFile);
}
} // namespace panda::ecmascript
