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

#include "ecmascript/tests/test_helper.h"
#include "ecmascript/waiter_list.h"

using namespace panda;
using namespace panda::ecmascript;

namespace panda::test {
class WaiterListTest : public BaseTestWithScope<false> {
};

static WaiterListNode *CreateListNode(void *buffer, size_t index)
{
    WaiterListNode *listNode = new WaiterListNode();
    EXPECT_TRUE(buffer != nullptr && listNode != nullptr);
    listNode->date_ = buffer;
    listNode->index_ = index;
    listNode->waitPointer_ = reinterpret_cast<int8_t *>(buffer) + index;
    listNode->waiting_ = true;
    return listNode;
}

static void DeleteListNode(WaiterListNode *listNode)
{
    if (listNode != nullptr) {
        delete listNode;
        listNode = nullptr;
    }
    return;
}

HWTEST_F_L0(WaiterListTest, CreateWaiterList)
{
    WaiterList *waitLists = Singleton<WaiterList>::GetInstance();
    EXPECT_TRUE(waitLists != nullptr);
}

HWTEST_F_L0(WaiterListTest, AddAndDeleteNodesAtSameLocation)
{
    constexpr size_t bufferLength = 8;
    constexpr size_t index = 4;
    WaiterList *waitLists = Singleton<WaiterList>::GetInstance();
    void *buffer = thread->GetEcmaVM()->GetNativeAreaAllocator()->AllocateBuffer(bufferLength);
    WaiterListNode *first = CreateListNode(buffer, index);
    WaiterListNode *second = CreateListNode(buffer, index);

    waitLists->AddNode(first);
    waitLists->AddNode(second);
    auto iter = waitLists->locationListMap_.find(first->waitPointer_);
    ASSERT_NE(iter, waitLists->locationListMap_.end());
    EXPECT_EQ(iter->second.pHead, first);
    EXPECT_EQ(iter->second.pTail, second);
    EXPECT_EQ(first->prev_, nullptr);
    EXPECT_EQ(first->next_, second);
    EXPECT_EQ(second->prev_, first);
    EXPECT_EQ(second->next_, nullptr);

    waitLists->DeleteNode(first);
    iter = waitLists->locationListMap_.find(second->waitPointer_);
    ASSERT_NE(iter, waitLists->locationListMap_.end());
    EXPECT_EQ(iter->second.pHead, second);
    EXPECT_EQ(iter->second.pTail, second);
    EXPECT_EQ(first->prev_, nullptr);
    EXPECT_EQ(first->next_, nullptr);
    EXPECT_EQ(second->prev_, nullptr);
    EXPECT_EQ(second->next_, nullptr);

    waitLists->DeleteNode(second);
    EXPECT_EQ(waitLists->locationListMap_.find(second->waitPointer_), waitLists->locationListMap_.end());
    DeleteListNode(first);
    DeleteListNode(second);
    thread->GetEcmaVM()->GetNativeAreaAllocator()->Delete(buffer);
}
}  // namespace panda::test
