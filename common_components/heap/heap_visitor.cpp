/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "common_interfaces/heap/heap_visitor.h"

#include "common_components/base_runtime/hooks.h"
#include "common_components/mutator/mutator.h"
namespace common {
UnmarkAllXRefsHookFunc g_unmarkAllXRefsHook = nullptr;
SweepUnmarkedXRefsHookFunc g_sweepUnmarkedXRefsHook = nullptr;
AddXRefToStaticRootsHookFunc g_addXRefToStaticRootsHook = nullptr;
RemoveXRefFromStaticRootsHookFunc g_removeXRefFromStaticRootsHook = nullptr;

VisitStaticRootsHookFunc g_visitStaticRootsHook = nullptr;
UpdateStaticRootsHookFunc g_updateStaticRootsHook = nullptr;
SweepStaticRootsHookFunc g_sweepStaticRootsHook = nullptr;

void RegisterVisitStaticRootsHook(VisitStaticRootsHookFunc func)
{
    g_visitStaticRootsHook = func;
}

void RegisterUpdateStaticRootsHook(UpdateStaticRootsHookFunc func)
{
    g_updateStaticRootsHook = func;
}

void RegisterSweepStaticRootsHook(SweepStaticRootsHookFunc func)
{
    g_sweepStaticRootsHook = func;
}


void VisitRoots(const RefFieldVisitor &visitor, bool isMark)
{
    VisitDynamicGlobalRoots(visitor);
    VisitDynamicLocalRoots(visitor);
    VisitBaseRoots(visitor);
    if (isMark) {
        if (g_visitStaticRootsHook != nullptr) {
            g_visitStaticRootsHook(visitor);
        }
    } else {
        if (g_updateStaticRootsHook != nullptr) {
            g_updateStaticRootsHook(visitor);
        }
    }
}

void VisitWeakRoots(const WeakRefFieldVisitor &visitor)
{
    VisitDynamicWeakGlobalRoots(visitor);
    VisitDynamicWeakLocalRoots(visitor);
    if (g_updateStaticRootsHook != nullptr) {
        g_updateStaticRootsHook(visitor);
    }
    if (g_sweepStaticRootsHook != nullptr) {
        g_sweepStaticRootsHook(visitor);
    }
}

void VisitGlobalRoots(const RefFieldVisitor &visitor, bool isMark)
{
    VisitDynamicGlobalRoots(visitor);
    VisitBaseRoots(visitor);
    if (isMark) {
        if (g_visitStaticRootsHook != nullptr) {
            g_visitStaticRootsHook(visitor);
        }
    } else {
        if (g_updateStaticRootsHook != nullptr) {
            g_updateStaticRootsHook(visitor);
        }
    }
}

void VisitWeakGlobalRoots(const WeakRefFieldVisitor &visitor)
{
    VisitDynamicWeakGlobalRoots(visitor);
    if (g_updateStaticRootsHook != nullptr) {
        g_updateStaticRootsHook(visitor);
    }
    if (g_sweepStaticRootsHook != nullptr) {
        g_sweepStaticRootsHook(visitor);
    }
}

// Visit specific mutator's root.
void VisitMutatorRoot(const RefFieldVisitor &visitor, Mutator &mutator)
{
    if (mutator.GetEcmaVMPtr()) {
        VisitDynamicThreadRoot(visitor, mutator.GetEcmaVMPtr());
    }
}

void VisitWeakMutatorRoot(const WeakRefFieldVisitor &visitor, Mutator &mutator)
{
    if (mutator.GetEcmaVMPtr()) {
        VisitDynamicWeakThreadRoot(visitor, mutator.GetEcmaVMPtr());
    }
}

void RegisterUnmarkAllXRefsHook(UnmarkAllXRefsHookFunc func)
{
    g_unmarkAllXRefsHook = func;
}

void RegisterSweepUnmarkedXRefsHook(SweepUnmarkedXRefsHookFunc func)
{
    g_sweepUnmarkedXRefsHook = func;
}

void RegisterAddXRefToStaticRootsHook(AddXRefToStaticRootsHookFunc func)
{
    g_addXRefToStaticRootsHook = func;
}

void RegisterRemoveXRefFromStaticRootsHook(RemoveXRefFromStaticRootsHookFunc func)
{
    g_removeXRefFromStaticRootsHook = func;
}

void UnmarkAllXRefs()
{
    g_unmarkAllXRefsHook();
}

void SweepUnmarkedXRefs()
{
    g_sweepUnmarkedXRefsHook();
}

void AddXRefToRoots()
{
    AddXRefToDynamicRoots();
    g_addXRefToStaticRootsHook();
}

void RemoveXRefFromRoots()
{
    RemoveXRefFromDynamicRoots();
    g_removeXRefFromStaticRootsHook();
}
}  // namespace common
