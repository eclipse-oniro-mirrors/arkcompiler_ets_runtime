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

#ifndef ECMASCRIPT_ECMA_GLOBAL_STORAGE_H
#define ECMASCRIPT_ECMA_GLOBAL_STORAGE_H

#include "ecmascript/mem/native_area_allocator.h"
#ifdef ECMASCRIPT_ENABLE_GLOBAL_LEAK_CHECK
#include "ecmascript/dfx/native_dfx/backtrace.h"
#endif
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/js_thread.h"

namespace panda::ecmascript {
class EcmaGlobalStorage {
public:
    static const int32_t GLOBAL_BLOCK_SIZE = 256;
    using WeakClearCallback = void (*)(void *);

    explicit EcmaGlobalStorage(JSThread *thread, NativeAreaAllocator *allocator)
        : thread_(thread), allocator_(allocator)
    {
        topGlobalNodes_ = lastGlobalNodes_ = allocator_->New<NodeList<Node>>();
        topWeakGlobalNodes_ = lastWeakGlobalNodes_ = allocator_->New<NodeList<WeakNode>>();
    }

    ~EcmaGlobalStorage()
    {
        auto *next = topGlobalNodes_;
        NodeList<Node> *current = nullptr;
        while (next != nullptr) {
            current = next;
            next = current->GetNext();
            current->IterateUsageGlobal([] (Node *node) {
                node->SetUsing(false);
                node->SetObject(JSTaggedValue::Undefined().GetRawData());
            });
            allocator_->Delete(current);
        }

        auto *weakNext = topWeakGlobalNodes_;
        NodeList<WeakNode> *weakCurrent = nullptr;
        while (weakNext != nullptr) {
            weakCurrent = weakNext;
            weakNext = weakCurrent->GetNext();
            weakCurrent->IterateUsageGlobal([] (Node *node) {
                node->SetUsing(false);
                node->SetObject(JSTaggedValue::Undefined().GetRawData());
                reinterpret_cast<WeakNode *>(node)->CallFirstPassCallback();
                reinterpret_cast<WeakNode *>(node)->CallSecondPassCallback();
            });
            allocator_->Delete(weakCurrent);
        }
    }

    class Node {
    public:
        JSTaggedType GetObject() const
        {
            return obj_;
        }

        void SetObject(JSTaggedType obj)
        {
            obj_ = obj;
        }

        Node *GetNext() const
        {
            return next_;
        }

        void SetNext(Node *node)
        {
            next_ = node;
        }

        Node *GetPrev() const
        {
            return prev_;
        }

        void SetPrev(Node *node)
        {
            prev_ = node;
        }

        int32_t GetIndex()
        {
            return index_;
        }

        void SetIndex(int32_t index)
        {
            index_ = index;
        }

        void SetUsing(bool isUsing)
        {
            isUsing_ = isUsing;
        }

        void SetWeak(bool isWeak)
        {
            isWeak_ = isWeak;
        }

        bool IsUsing() const
        {
            return isUsing_;
        }

        bool IsWeak() const
        {
            return isWeak_;
        }

        uintptr_t GetObjectAddress() const
        {
            return reinterpret_cast<uintptr_t>(&obj_);
        }

        // If isUsing is true, it means that the node is being used, otherwise it means that node is be freed
        void Reset(Node *next, JSTaggedType value, bool isUsing)
        {
            SetPrev(nullptr);
            SetNext(next);
            SetObject(value);
            SetUsing(isUsing);
#ifdef ECMASCRIPT_ENABLE_GLOBAL_LEAK_CHECK
            ResetMarkCount();
            if (isUsing) {
                IncGlobalNumber();
                // This value needs to be adjusted according to the specific scene.
                static const int START_GLOBAL_NUMBER = 110000;
                static const int GLOBAL_NUMBER_COUNT = 10000;
                if (globalNumber_ > START_GLOBAL_NUMBER && globalNumber_ < START_GLOBAL_NUMBER + GLOBAL_NUMBER_COUNT
                    && JSTaggedValue(value).IsHeapObject()) {
                    LOG_ECMA(INFO) << "---------------Global Number:" << globalNumber_ << "-------------------";
                    PrintBacktrace(value);
                }
            }
#endif
        }

#ifdef ECMASCRIPT_ENABLE_GLOBAL_LEAK_CHECK
        int32_t GetMarkCount() const
        {
            return markCount_;
        }

        void MarkCount()
        {
            markCount_++;
        }

        void ResetMarkCount()
        {
            markCount_ = 0;
        }

        void IncGlobalNumber()
        {
            static int numberCount = 0;
            globalNumber_ = numberCount++;
        }

        int32_t GetGlobalNumber()
        {
            return globalNumber_;
        }
#endif

    private:
        JSTaggedType obj_;
        Node *next_ {nullptr};
        Node *prev_ {nullptr};
        int32_t index_ {-1};
        bool isUsing_ {false};
        bool isWeak_ {false};
#ifdef ECMASCRIPT_ENABLE_GLOBAL_LEAK_CHECK
        int32_t markCount_ {0};
        // A number generated in the order of distribution.It Used to help locate global memory leaks.
        int32_t globalNumber_ {0};
#endif
    };

    class WeakNode : public Node {
    public:
        void SetReference(void *ref)
        {
            reference_ = ref;
        }

        void* GetReference() const
        {
            return reference_;
        }

        void SetFirstPassCallback(WeakClearCallback callback)
        {
            firstPassCallback_ = callback;
        }

        void SetSecondPassCallback(WeakClearCallback callback)
        {
            secondPassCallback_ = callback;
        }

        WeakClearCallback GetSecondPassCallback() const
        {
            return secondPassCallback_;
        }

        void CallFirstPassCallback()
        {
            if (firstPassCallback_ != nullptr) {
                firstPassCallback_(reference_);
            }
        }

        void CallSecondPassCallback()
        {
            if (secondPassCallback_ != nullptr) {
                secondPassCallback_(reference_);
            }
        }
    private:
        void *reference_ {nullptr};
        WeakClearCallback firstPassCallback_ {nullptr};
        WeakClearCallback secondPassCallback_ {nullptr};
    };

    template<typename T>
    class NodeList {
    public:
        explicit NodeList()
        {
            bool isWeak = std::is_same<T, EcmaGlobalStorage::WeakNode>::value;
            for (int i = 0; i < GLOBAL_BLOCK_SIZE; i++) {
                nodeList_[i].SetIndex(i);
                nodeList_[i].SetWeak(isWeak);
            }
        }
        ~NodeList() = default;

        inline static NodeList<T> *NodeToNodeList(T *node)
        {
            uintptr_t ptr = ToUintPtr(node) - node->GetIndex() * sizeof(T);
            return reinterpret_cast<NodeList<T> *>(ptr);
        }

        inline T *NewNode(JSTaggedType value)
        {
            if (IsFull()) {
                return nullptr;
            }
            T *node = &nodeList_[index_++];
            node->Reset(usedList_, value, true);
            if (usedList_ != nullptr) {
                usedList_->SetPrev(node);
            }
            usedList_ = node;
            return node;
        }

        inline T *GetFreeNode(JSTaggedType value)
        {
            T *node = freeList_;
            if (node != nullptr) {
                freeList_ = reinterpret_cast<T *>(node->GetNext());
                node->Reset(usedList_, value, true);
                if (usedList_ != nullptr) {
                    usedList_->SetPrev(node);
                }
                usedList_ = node;
            }
            return node;
        }

        inline void FreeNode(T *node)
        {
            if (node->GetPrev() != nullptr) {
                node->GetPrev()->SetNext(node->GetNext());
            }
            if (node->GetNext() != nullptr) {
                node->GetNext()->SetPrev(node->GetPrev());
            }
            if (node == usedList_) {
                usedList_ = reinterpret_cast<T *>(node->GetNext());
            }
            node->Reset(freeList_, JSTaggedValue::Undefined().GetRawData(), false);
            if (node->IsWeak()) {
                reinterpret_cast<WeakNode *>(node)->SetReference(nullptr);
                reinterpret_cast<WeakNode *>(node)->SetFirstPassCallback(nullptr);
                reinterpret_cast<WeakNode *>(node)->SetSecondPassCallback(nullptr);
            }
            if (freeList_ != nullptr) {
                freeList_->SetPrev(node);
            }
            freeList_ = node;
        }

        inline void LinkTo(NodeList<T> *prev)
        {
            next_ = nullptr;
            prev_ = prev;
            prev_->next_ = this;
        }
        inline void RemoveList()
        {
            if (next_ != nullptr) {
                next_->SetPrev(prev_);
            }
            if (prev_ != nullptr) {
                prev_->SetNext(next_);
            }
            if (freeNext_ != nullptr) {
                freeNext_->SetFreePrev(freePrev_);
            }
            if (freePrev_ != nullptr) {
                freePrev_->SetFreeNext(freeNext_);
            }
        }

        inline bool IsFull()
        {
            return index_ >= GLOBAL_BLOCK_SIZE;
        }

        inline bool HasFreeNode()
        {
            return freeList_ != nullptr;
        }

        inline bool HasUsagedNode()
        {
            return !IsFull() || usedList_ != nullptr;
        }

        inline void SetNext(NodeList *next)
        {
            next_ = next;
        }
        inline NodeList<T> *GetNext() const
        {
            return next_;
        }

        inline void SetPrev(NodeList<T> *prev)
        {
            prev_ = prev;
        }
        inline NodeList<T> *GetPrev() const
        {
            return prev_;
        }

        inline void SetFreeNext(NodeList<T> *next)
        {
            freeNext_ = next;
        }
        inline NodeList<T> *GetFreeNext() const
        {
            return freeNext_;
        }

        inline void SetFreePrev(NodeList<T> *prev)
        {
            freePrev_ = prev;
        }
        inline NodeList<T> *GetFreePrev() const
        {
            return freePrev_;
        }

        template<class Callback>
        inline void IterateUsageGlobal(Callback callback)
        {
            Node *next = usedList_;
            Node *current = nullptr;
            while (next != nullptr) {
                current = next;
                next = current->GetNext();
                ASSERT(current != next);
                callback(current);
            }
        }

    private:
        T nodeList_[GLOBAL_BLOCK_SIZE];  // all
        T *freeList_ {nullptr};  // dispose node
        T *usedList_ {nullptr};  // usage node
        int32_t index_ {0};
        NodeList<T> *next_ {nullptr};
        NodeList<T> *prev_ {nullptr};
        NodeList<T> *freeNext_ {nullptr};
        NodeList<T> *freePrev_ {nullptr};
    };

    inline uintptr_t NewGlobalHandle(JSTaggedType value)
    {
        uintptr_t ret = NewGlobalHandleImplement(&lastGlobalNodes_, &freeListNodes_, value);
        return ret;
    }

    inline void DisposeGlobalHandle(uintptr_t nodeAddr)
    {
        Node *node = reinterpret_cast<Node *>(nodeAddr);
        if (!node->IsUsing()) {
            return;
        }
        if (node->IsWeak()) {
            DisposeGlobalHandle(reinterpret_cast<WeakNode *>(node), &weakFreeListNodes_, &topWeakGlobalNodes_,
                                &lastWeakGlobalNodes_);
        } else {
            DisposeGlobalHandle(node, &freeListNodes_, &topGlobalNodes_, &lastGlobalNodes_);
        }
    }

    inline uintptr_t SetWeak(uintptr_t nodeAddr, void *ref = nullptr, WeakClearCallback firstCallback = nullptr,
                             WeakClearCallback secondCallback = nullptr)
    {
        auto value = reinterpret_cast<Node *>(nodeAddr)->GetObject();
        DisposeGlobalHandle(nodeAddr);
        uintptr_t addr = NewGlobalHandleImplement<WeakNode>(&lastWeakGlobalNodes_, &weakFreeListNodes_, value);
        WeakNode *node = reinterpret_cast<WeakNode *>(addr);
        node->SetReference(ref);
        node->SetFirstPassCallback(firstCallback);
        node->SetSecondPassCallback(secondCallback);
        return addr;
    }

    inline uintptr_t ClearWeak(uintptr_t nodeAddr)
    {
        auto value = reinterpret_cast<Node *>(nodeAddr)->GetObject();
        DisposeGlobalHandle(nodeAddr);
        uintptr_t ret = NewGlobalHandleImplement<Node>(&lastGlobalNodes_, &freeListNodes_, value);
        return ret;
    }
    inline bool IsWeak(uintptr_t addr) const
    {
        Node *node = reinterpret_cast<Node *>(addr);
        return node->IsWeak();
    }

    template<class Callback>
    void IterateUsageGlobal(Callback callback)
    {
        NodeList<Node> *next = topGlobalNodes_;
        NodeList<Node> *current = nullptr;
        while (next != nullptr) {
            current = next;
            next = current->GetNext();
            ASSERT(current != next);
            current->IterateUsageGlobal(callback);
        }
    }

    template<class Callback>
    void IterateWeakUsageGlobal(Callback callback)
    {
        NodeList<WeakNode> *next = topWeakGlobalNodes_;
        NodeList<WeakNode> *current = nullptr;
        while (next != nullptr) {
            current = next;
            next = current->GetNext();
            ASSERT(current != next);
            current->IterateUsageGlobal(callback);
        }
    }

private:
    NO_COPY_SEMANTIC(EcmaGlobalStorage);
    NO_MOVE_SEMANTIC(EcmaGlobalStorage);

    template<typename T>
    inline void DisposeGlobalHandle(T *node, NodeList<T> **freeList, NodeList<T> **topNodes,
                                    NodeList<T> **lastNodes)
    {
        NodeList<T> *list = NodeList<T>::NodeToNodeList(node);
        list->FreeNode(node);

        // If NodeList has no usage node, then delete NodeList
        if (!list->HasUsagedNode() && (*topNodes != *lastNodes)) {
            list->RemoveList();
            if (*freeList == list) {
                *freeList = list->GetNext();
            }
            if (*topNodes == list) {
                *topNodes = list->GetNext();
            }
            if (*lastNodes == list) {
                *lastNodes = list->GetPrev();
            }
            allocator_->Delete(list);
        } else {
            // Add to freeList
            if (list != *freeList && list->GetFreeNext() == nullptr && list->GetFreePrev() == nullptr) {
                list->SetFreeNext(*freeList);
                if (*freeList != nullptr) {
                    (*freeList)->SetFreePrev(list);
                }
                *freeList = list;
            }
        }
    }

    template<typename T>
    inline uintptr_t NewGlobalHandleImplement(NodeList<T> **storage, NodeList<T> **freeList, JSTaggedType value)
    {
#if ECMASCRIPT_ENABLE_NEW_HANDLE_CHECK
        thread_->CheckJSTaggedType(value);
#endif
        if (!(*storage)->IsFull()) {
            // alloc new block
            T *node = (*storage)->NewNode(value);
            ASSERT(node != nullptr);
            return node->GetObjectAddress();
        }
        if (*freeList != nullptr) {
            // use free_list node
            Node *node = (*freeList)->GetFreeNode(value);
            ASSERT(node != nullptr);
            if (!(*freeList)->HasFreeNode()) {
                auto next = (*freeList)->GetFreeNext();
                (*freeList)->SetFreeNext(nullptr);
                (*freeList)->SetFreePrev(nullptr);
                if (next != nullptr) {
                    next->SetFreePrev(nullptr);
                }
                *freeList = next;
            }
            return node->GetObjectAddress();
        }
        auto block = allocator_->New<NodeList<T>>();
        block->LinkTo(*storage);
        *storage = block;

        // use node in block finally
        T *node = (*storage)->NewNode(value);
        ASSERT(node != nullptr);
        return node->GetObjectAddress();
    }

    [[maybe_unused]] JSThread *thread_ {nullptr};
    NativeAreaAllocator *allocator_ {nullptr};
    NodeList<Node> *topGlobalNodes_ {nullptr};
    NodeList<Node> *lastGlobalNodes_ {nullptr};
    NodeList<Node> *freeListNodes_ {nullptr};

    NodeList<WeakNode> *topWeakGlobalNodes_ {nullptr};
    NodeList<WeakNode> *lastWeakGlobalNodes_ {nullptr};
    NodeList<WeakNode> *weakFreeListNodes_ {nullptr};
};
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_ECMA_GLOBAL_STORAGE_H
