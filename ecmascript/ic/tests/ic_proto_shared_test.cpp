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

#include "ecmascript/ic/ic_handler.h"
#include "ecmascript/ic/proto_change_details.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_object-inl.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/object_operator.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {

class ICProtoSharedTest : public BaseTestWithScope<true> {
};

// Helper: create a shared hclass with a shared-heap prototype, mirroring the
// runtime's real shared-object construction (shared_builtins / SendableClassDefiner):
// proto is a shared-heap JSObject, IsJSShared is true, the chain is sealed.
static JSHandle<JSHClass> CreateSharedHClassWithProto(JSThread *thread)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    // Proto hclass: shared, no inlined props.
    JSHandle<JSHClass> protoHClass = factory->NewSEcmaHClass(JSObject::SIZE, JSType::JS_SHARED_OBJECT, 0);
    // Proto object: allocated in the shared heap.
    JSHandle<JSObject> protoObj = factory->NewSharedOldSpaceJSObject(protoHClass);
    // Receiver hclass: shared, proto set to the shared proto object.
    JSHandle<JSHClass> receiverHClass = factory->NewSEcmaHClass(JSObject::SIZE, JSType::JS_SHARED_OBJECT, 0);
    JSHClass::SetPrototype(thread, receiverHClass, protoObj.GetTaggedValue());
    return receiverHClass;
}

// Helper: build a found op (same pattern as ic_handler_test.cpp).
static ObjectOperator CreateFoundOp(JSThread *thread, const char *keyStr)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSTaggedValue> key(factory->NewFromASCII(keyStr));
    ObjectOperator op(thread, key, OperatorType::OWN);
    op.SetFastMode(true);
    op.SetIndex(0);
    return op;
}

/**
 * @tc.name: LoadPrototypeSharedUndefinedCell
 * @tc.desc: For a shared hclass, PrototypeHandler::LoadPrototype skips
 *           EnableProtoChangeMarker — the ProtoCell stays Undefined.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ICProtoSharedTest, LoadPrototypeSharedUndefinedCell)
{
    JSHandle<JSHClass> sharedHClass = CreateSharedHClassWithProto(thread);
    ASSERT_TRUE(sharedHClass->IsJSShared());
    ASSERT_TRUE(sharedHClass->GetPrototype(thread).IsInSharedHeap());

    ObjectOperator op = CreateFoundOp(thread, "protoKey");

    JSHandle<JSTaggedValue> handlerValue =
        PrototypeHandler::LoadPrototype(thread, op, sharedHClass);
    ASSERT_TRUE(handlerValue->IsPrototypeHandler());

    JSHandle<PrototypeHandler> handler = JSHandle<PrototypeHandler>::Cast(handlerValue);
    EXPECT_TRUE(handler->GetProtoCell(thread).IsUndefined());
}

/**
 * @tc.name: LoadPrototypeNonSharedHasMarker
 * @tc.desc: For a non-shared hclass, PrototypeHandler::LoadPrototype calls
 *           EnableProtoChangeMarker — the ProtoCell is a ProtoChangeMarker.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ICProtoSharedTest, LoadPrototypeNonSharedHasMarker)
{
    JSHandle<JSObject> nullHandle(thread, JSTaggedValue::Null());
    JSHandle<JSObject> protoObj = JSObject::ObjectCreate(thread, nullHandle);
    JSHandle<JSObject> receiverObj = JSObject::ObjectCreate(thread, protoObj);
    JSHandle<JSHClass> nonSharedHClass(thread, receiverObj->GetJSHClass());
    ASSERT_FALSE(nonSharedHClass->IsJSShared());

    ObjectOperator op = CreateFoundOp(thread, "protoKey");

    JSHandle<JSTaggedValue> handlerValue =
        PrototypeHandler::LoadPrototype(thread, op, nonSharedHClass);
    ASSERT_TRUE(handlerValue->IsPrototypeHandler());

    JSHandle<PrototypeHandler> handler = JSHandle<PrototypeHandler>::Cast(handlerValue);
    EXPECT_TRUE(handler->GetProtoCell(thread).IsProtoChangeMarker());
}

/**
 * @tc.name: StorePrototypeSharedUndefinedCell
 * @tc.desc: For a shared hclass, PrototypeHandler::StorePrototype skips
 *           EnableProtoChangeMarker — the ProtoCell stays Undefined.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ICProtoSharedTest, StorePrototypeSharedUndefinedCell)
{
    JSHandle<JSHClass> sharedHClass = CreateSharedHClassWithProto(thread);
    ASSERT_TRUE(sharedHClass->IsJSShared());
    ASSERT_TRUE(sharedHClass->GetPrototype(thread).IsInSharedHeap());

    ObjectOperator op = CreateFoundOp(thread, "protoKey");

    JSHandle<JSTaggedValue> handlerValue =
        PrototypeHandler::StorePrototype(thread, op, sharedHClass);
    ASSERT_TRUE(handlerValue->IsPrototypeHandler());

    JSHandle<PrototypeHandler> handler = JSHandle<PrototypeHandler>::Cast(handlerValue);
    EXPECT_TRUE(handler->GetProtoCell(thread).IsUndefined());
}

/**
 * @tc.name: StorePrototypeNonSharedHasMarker
 * @tc.desc: For a non-shared hclass, PrototypeHandler::StorePrototype calls
 *           EnableProtoChangeMarker — the ProtoCell is a ProtoChangeMarker.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ICProtoSharedTest, StorePrototypeNonSharedHasMarker)
{
    JSHandle<JSObject> nullHandle(thread, JSTaggedValue::Null());
    JSHandle<JSObject> protoObj = JSObject::ObjectCreate(thread, nullHandle);
    JSHandle<JSObject> receiverObj = JSObject::ObjectCreate(thread, protoObj);
    JSHandle<JSHClass> nonSharedHClass(thread, receiverObj->GetJSHClass());
    ASSERT_FALSE(nonSharedHClass->IsJSShared());

    ObjectOperator op = CreateFoundOp(thread, "protoKey");

    JSHandle<JSTaggedValue> handlerValue =
        PrototypeHandler::StorePrototype(thread, op, nonSharedHClass);
    ASSERT_TRUE(handlerValue->IsPrototypeHandler());

    JSHandle<PrototypeHandler> handler = JSHandle<PrototypeHandler>::Cast(handlerValue);
    EXPECT_TRUE(handler->GetProtoCell(thread).IsProtoChangeMarker());
}

/**
 * @tc.name: AddFieldTypeToHClassGuardPreconditions
 * @tc.desc: Verify the guard condition flags: a sealed shared hclass has
 *           (IsJSShared && !IsExtensible); an extensible shared hclass has
 *           (IsJSShared && IsExtensible). The guard fires only on the former.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ICProtoSharedTest, AddFieldTypeToHClassGuardPreconditions)
{
    // Sealed shared hclass: guard condition fires (IsJSShared && !IsExtensible).
    JSHandle<JSHClass> sealedSharedHClass = CreateSharedHClassWithProto(thread);
    sealedSharedHClass->SetExtensible(false);
    EXPECT_TRUE(sealedSharedHClass->IsJSShared());
    EXPECT_FALSE(sealedSharedHClass->IsExtensible());

    // Extensible shared hclass: guard condition does not fire (IsJSShared && IsExtensible).
    JSHandle<JSHClass> extensibleSharedHClass = CreateSharedHClassWithProto(thread);
    EXPECT_TRUE(extensibleSharedHClass->IsJSShared());
    EXPECT_TRUE(extensibleSharedHClass->IsExtensible());

    // Non-shared hclass: guard condition does not fire (!IsJSShared).
    JSHandle<JSObject> nullHandle(thread, JSTaggedValue::Null());
    JSHandle<JSObject> plainObj = JSObject::ObjectCreate(thread, nullHandle);
    JSHandle<JSHClass> nonSharedHClass(thread, plainObj->GetJSHClass());
    EXPECT_FALSE(nonSharedHClass->IsJSShared());
}
} // namespace panda::test
