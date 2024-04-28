/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#include "ecmascript/compiler/pgo_type/pgo_hclass_generator.h"
#include "ecmascript/ecma_handle_scope.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_hclass-inl.h"
#include "ecmascript/pgo_profiler/pgo_profiler_layout.h"
#include "ecmascript/property_attributes.h"
#include "ecmascript/ts_types/global_type_info.h"

namespace panda::ecmascript::kungfu {
bool PGOHClassGenerator::FindHClassLayoutDesc(PGOSampleType type) const
{
    PGOHClassTreeDesc *desc;
    typeRecorder_.GetHClassTreeDesc(type, &desc);
    if (desc == nullptr) {
        return false;
    }
    if (desc->GetHClassLayoutDesc(type.GetProfileType()) == nullptr) {
        return false;
    }
    return true;
}

bool PGOHClassGenerator::GenerateHClass(PGOSampleType type) const
{
    auto thread = ptManager_->GetJSThread();
    JSHandle<JSTaggedValue> result(thread, JSTaggedValue::Undefined());
    PGOHClassTreeDesc *desc;
    if (!typeRecorder_.GetHClassTreeDesc(type, &desc)) {
        return false;
    }
    auto rootType = type.GetProfileType();
    auto rootHClassDesc = desc->GetHClassLayoutDesc(rootType);
    if (rootHClassDesc == nullptr) {
        return false;
    }

    uint32_t rootNumOfProps = reinterpret_cast<RootHClassLayoutDesc *>(rootHClassDesc)->NumOfProps();
    uint32_t maxNumOfProps = rootNumOfProps;
    CaculateMaxNumOfObj(desc, rootHClassDesc, rootNumOfProps, maxNumOfProps);
    if (maxNumOfProps > PropertyAttributes::MAX_FAST_PROPS_CAPACITY) {
        return false;
    }

    JSHandle<JSHClass> rootHClass;
    if (rootType.IsPrototype()) {
        rootHClass = CreateRootPHClass(rootType, rootHClassDesc, maxNumOfProps);
    } else if (rootType.IsConstructor()) {
        rootHClass = CreateRootCHClass(rootType, rootHClassDesc, maxNumOfProps);
    } else {
        rootHClass = CreateRootHClass(rootType, rootHClassDesc, maxNumOfProps);
    }

    CreateChildHClass(rootType, desc, rootHClass, rootHClassDesc);
    return true;
}

bool PGOHClassGenerator::GenerateIHClass(PGOSampleType type, const JSHandle<JSObject> &prototype) const
{
    PGOHClassTreeDesc *desc;
    if (!typeRecorder_.GetHClassTreeDesc(type, &desc)) {
        return false;
    }
    auto rootType = type.GetProfileType();
    auto rootHClassDesc = desc->GetHClassLayoutDesc(rootType);
    if (rootHClassDesc == nullptr) {
        ptManager_->RecordHClass(rootType, rootType, prototype.GetTaggedType());
        return false;
    }

    uint32_t rootNumOfProps = reinterpret_cast<RootHClassLayoutDesc *>(rootHClassDesc)->NumOfProps();
    uint32_t maxNumOfProps = rootNumOfProps;
    CaculateMaxNumOfObj(desc, rootHClassDesc, rootNumOfProps, maxNumOfProps);
    if (maxNumOfProps > PropertyAttributes::MAX_FAST_PROPS_CAPACITY) {
        return false;
    }

    JSHandle<JSHClass> rootHClass = CreateRootHClass(rootType, rootHClassDesc, maxNumOfProps);
    auto thread = ptManager_->GetJSThread();
    rootHClass->SetProto(thread, prototype);

    CreateChildHClass(rootType, desc, rootHClass, rootHClassDesc);
    return true;
}

void PGOHClassGenerator::CaculateMaxNumOfObj(
    const PGOHClassTreeDesc *desc, const HClassLayoutDesc *parent, uint32_t lastNum, uint32_t &maxNum) const
{
    if (lastNum > maxNum) {
        maxNum = lastNum;
    }
    parent->IterateChilds([this, desc, lastNum, &maxNum] (const ProfileType &childType) -> bool {
        auto layoutDesc = desc->GetHClassLayoutDesc(childType);
        if (layoutDesc == nullptr) {
            return true;
        }
        CaculateMaxNumOfObj(desc, layoutDesc, lastNum + 1, maxNum);
        return true;
    });
}

JSHandle<JSHClass> PGOHClassGenerator::CreateRootPHClass(
    ProfileType rootType, const HClassLayoutDesc *layoutDesc, uint32_t maxNum) const
{
    JSHandle<JSHClass> rootHClass = CreateRootHClass(rootType, layoutDesc, maxNum);
    rootHClass->SetClassPrototype(true);
    rootHClass->SetIsPrototype(true);
    return rootHClass;
}

JSHandle<JSHClass> PGOHClassGenerator::CreateRootCHClass(
    ProfileType rootType, const HClassLayoutDesc *layoutDesc, uint32_t maxNum) const
{
    JSHandle<JSHClass> rootHClass = CreateRootHClass(rootType, layoutDesc, maxNum);
    rootHClass->SetClassConstructor(true);
    rootHClass->SetConstructor(true);
    rootHClass->SetCallable(true);
    return rootHClass;
}

JSHandle<JSHClass> PGOHClassGenerator::CreateRootHClass(
    ProfileType rootType, const HClassLayoutDesc *layoutDesc, uint32_t maxNum) const
{
    auto thread = ptManager_->GetJSThread();
    auto hclassValue = ptManager_->QueryHClass(rootType, rootType);
    JSHandle<JSHClass> rootHClass(thread, hclassValue);
    if (hclassValue.IsUndefined()) {
        rootHClass = JSHClass::CreateRootHClassFromPGO(thread, layoutDesc, maxNum);
        ptManager_->RecordHClass(rootType, rootType, rootHClass.GetTaggedType());
    }
    return rootHClass;
}

void PGOHClassGenerator::CreateChildHClass(ProfileType rootType, const PGOHClassTreeDesc *desc,
    const JSHandle<JSHClass> &parent, const HClassLayoutDesc *parentLayoutDesc) const
{
    parentLayoutDesc->IterateChilds([this, rootType, desc, parent] (const ProfileType &childType) -> bool {
        auto layoutDesc = desc->GetHClassLayoutDesc(childType);
        if (layoutDesc == nullptr) {
            return true;
        }
        auto hclassValue = ptManager_->QueryHClass(rootType, childType);
        auto thread = ptManager_->GetJSThread();
        JSHandle<JSHClass> childHClass(thread, hclassValue);
        if (hclassValue.IsUndefined()) {
            childHClass = JSHClass::CreateChildHClassFromPGO(thread, parent, layoutDesc);
            ptManager_->RecordHClass(rootType, childType, childHClass.GetTaggedType());
        }
        CreateChildHClass(rootType, desc, childHClass, layoutDesc);
        return true;
    });
}
}  // namespace panda::ecmascript
