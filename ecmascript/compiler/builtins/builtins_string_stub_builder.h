/*
 * Copyright (c) 2022-2024 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_COMPILER_BUILTINS_STRING_STUB_BUILDER_H
#define ECMASCRIPT_COMPILER_BUILTINS_STRING_STUB_BUILDER_H
#include "ecmascript/compiler/gate.h"
#include "ecmascript/compiler/stub_builder-inl.h"
#include "ecmascript/compiler/builtins/builtins_stubs.h"

namespace panda::ecmascript::kungfu {
class FlatStringStubBuilder;
struct StringInfoGateRef;

class BuiltinsStringStubBuilder : public BuiltinsStubBuilder {
public:
    explicit BuiltinsStringStubBuilder(StubBuilder *parent, GateRef globalEnv)
        : BuiltinsStubBuilder(parent, globalEnv) {}
    BuiltinsStringStubBuilder(CallSignature *callSignature, Environment *env, GateRef globalEnv)
        : BuiltinsStubBuilder(callSignature, env, globalEnv) {}
    explicit BuiltinsStringStubBuilder(Environment* env, GateRef globalEnv): BuiltinsStubBuilder(env, globalEnv) {}
    ~BuiltinsStringStubBuilder() override = default;
    NO_MOVE_SEMANTIC(BuiltinsStringStubBuilder);
    NO_COPY_SEMANTIC(BuiltinsStringStubBuilder);
    void GenerateCircuit() override {}

#define DECLARE_BUILTINS_SRRING_STUB_BUILDER(method, ...)           \
    void method(GateRef glue, GateRef thisValue, GateRef numArgs, Variable* res, Label *exit, Label *slowPath);
BUILTINS_WITH_STRING_STUB_BUILDER(DECLARE_BUILTINS_SRRING_STUB_BUILDER)
#undef DECLARE_BUILTINS_SRRING_STUB_BUILDER

    void LocaleCompare(GateRef glue, GateRef thisValue, GateRef numArgs, Variable *res, Label *exit, Label *slowPath);
    void StringIteratorNext(GateRef glue, GateRef thisValue, GateRef numArgs,
                            Variable *res, Label *exit, Label *slowPath);

    GateRef ConvertAndClampRelativeIndex(GateRef index, GateRef length);
    GateRef StringAt(GateRef glue, const StringInfoGateRef &stringInfoGate, GateRef index);
    GateRef FastSubString(GateRef glue, GateRef thisValue, GateRef from, GateRef len,
        const StringInfoGateRef &stringInfoGate);
    GateRef FastSubUtf8String(GateRef glue, GateRef from, GateRef len, const StringInfoGateRef &stringInfoGate);
    GateRef FastSubUtf16String(GateRef glue, GateRef from, GateRef len, const StringInfoGateRef &stringInfoGate);
    GateRef FastStringCharCodeAt(GateRef glue, GateRef thisValue, GateRef pos);
    GateRef GetSubstitution(GateRef glue, GateRef searchString, GateRef thisString,
        GateRef pos, GateRef replaceString);
    void CopyChars(GateRef glue, GateRef dst, GateRef source, GateRef sourceLength, GateRef size, VariableType type);
    void CopyUtf16AsUtf8(GateRef glue, GateRef dst, GateRef src, GateRef sourceLength);
    void CopyUtf8AsUtf16(GateRef glue, GateRef dst, GateRef src, GateRef sourceLength);
    GateRef StringIndexOf(GateRef lhsData, bool lhsIsUtf8, GateRef rhsData, bool rhsIsUtf8,
                          GateRef pos, GateRef max, GateRef rhsCount);
    GateRef StringIndexOf(GateRef glue, const StringInfoGateRef &lStringInfoGate,
        const StringInfoGateRef &rStringInfoGate, GateRef pos);
    GateRef GetSingleCharCodeByIndex(GateRef glue, GateRef str, GateRef index);
    GateRef CreateStringBySingleCharCode(GateRef glue, GateRef charCode);
    GateRef CreateFromEcmaString(GateRef glue, GateRef index, const StringInfoGateRef &stringInfoGate);
    GateRef IsFirstConcatInStringAdd(GateRef init, GateRef status);
    GateRef ConcatIsInStringAdd(GateRef init, GateRef status);
    GateRef StringAdd(GateRef glue, GateRef leftString, GateRef rightString, GateRef status);
    GateRef AllocateLineString(GateRef glue, GateRef length, GateRef canBeCompressed);
    GateRef AllocateSlicedString(GateRef glue, GateRef flatString, GateRef length, GateRef canBeCompressed);
    GateRef IsSpecialSlicedString(GateRef glue, GateRef obj);
    GateRef StringConcat(GateRef glue, GateRef leftString, GateRef rightString);
    GateRef EcmaStringTrim(GateRef glue, GateRef srcString, GateRef trimMode);
    GateRef EcmaStringTrimBody(GateRef glue, GateRef thisValue, StringInfoGateRef srcStringInfoGate,
        GateRef trimMode, GateRef isUtf8);
    void StoreParent(GateRef glue, GateRef object, GateRef parent);
    void StoreStartIndexAndBackingStore(GateRef glue, GateRef object, GateRef startIndex, GateRef hasBackingStore);
    GateRef LoadStartIndex(GateRef object);
    GateRef LoadHasBackingStore(GateRef object);
    GateRef IsSubStringAt(GateRef lhsData, bool lhsIsUtf8, GateRef rhsData, bool rhsIsUtf8,
        GateRef pos, GateRef rhsCount);
    GateRef IsSubStringAt(GateRef glue, const StringInfoGateRef &lStringInfoGate,
        const StringInfoGateRef &rStringInfoGate, GateRef pos);
    GateRef GetSubString(GateRef glue, GateRef thisValue, GateRef from, GateRef len);
    GateRef GetFastSubString(GateRef glue, GateRef thisValue, GateRef start, GateRef len);
    GateRef StringToUint(GateRef glue, GateRef string, uint64_t maxValue);
    GateRef StringDataToUint(GateRef dataUtf8, GateRef len, uint64_t maxValue);
private:
    GateRef ChangeStringTaggedPointerToInt64(GateRef x)
    {
        return GetEnvironment()->GetBuilder()->ChangeTaggedPointerToInt64(x);
    }
    GateRef CanBeCompressed(GateRef utf16Data, GateRef utf16Len, bool isUtf16);
    GateRef GetUtf16Data(GateRef stringData, GateRef index);
    GateRef IsASCIICharacter(GateRef data);
    GateRef GetUtf8Data(GateRef stringData, GateRef index);
    GateRef GetSingleCharCodeFromLineString(GateRef str, GateRef index);
    GateRef GetSingleCharCodeFromSlicedString(GateRef glue, GateRef str, GateRef index);
    void CheckParamsAndGetPosition(GateRef glue, GateRef thisValue, GateRef numArgs,
        Variable* pos, Label *exit, Label *slowPath, Label *posIsValid);
};

class FlatStringStubBuilder : public StubBuilder {
public:
    explicit FlatStringStubBuilder(StubBuilder *parent)
        : StubBuilder(parent) {}
    ~FlatStringStubBuilder() override = default;
    NO_MOVE_SEMANTIC(FlatStringStubBuilder);
    NO_COPY_SEMANTIC(FlatStringStubBuilder);
    void GenerateCircuit() override {}

    void FlattenString(GateRef glue, GateRef str, Label *exit);
    void FlattenStringWithIndex(GateRef glue, GateRef str, Variable *index, Label *exit);
    GateRef GetParentFromSlicedString(GateRef glue, GateRef string)
    {
        GateRef offset = IntPtr(SlicedString::PARENT_OFFSET);
        return Load(VariableType::JS_POINTER(), glue, string, offset);
    }

    GateRef GetStartIndexFromSlicedString(GateRef string) {
        GateRef offset = IntPtr(SlicedString::STARTINDEX_AND_FLAGS_OFFSET);
        GateRef mixStartIndex = LoadPrimitive(VariableType::INT32(), string, offset);
        return Int32LSR(mixStartIndex, Int32(SlicedString::StartIndexBits::START_BIT));
    }

    GateRef GetHasBackingStoreFromSlicedString(GateRef string) {
        GateRef offset = IntPtr(SlicedString::STARTINDEX_AND_FLAGS_OFFSET);
        GateRef mixStartIndex = LoadPrimitive(VariableType::INT32(), string, offset);
        return TruncInt32ToInt1(Int32And(mixStartIndex, Int32((1 << SlicedString::HasBackingStoreBit::SIZE) - 1)));
    }

    GateRef GetFlatString()
    {
        return flatString_.ReadVariable();
    }

    GateRef GetStartIndex()
    {
        return startIndex_.ReadVariable();
    }

    GateRef GetLength()
    {
        return length_;
    }

private:
    void FlattenStringImpl(GateRef glue, GateRef str, Label *handleSlicedString, Label *exit);
    Variable flatString_ { GetEnvironment(), VariableType::JS_POINTER(), NextVariableId(), Undefined() };
    Variable startIndex_ { GetEnvironment(), VariableType::INT32(), NextVariableId(), Int32(0) };
    GateRef length_ { Circuit::NullGate() };
};

struct StringInfoGateRef {
    GateRef string_ { Circuit::NullGate() };
    GateRef startIndex_ { Circuit::NullGate() };
    GateRef length_ { Circuit::NullGate() };
    explicit StringInfoGateRef(FlatStringStubBuilder* flatString) : string_(flatString->GetFlatString()),
                                                                    startIndex_(flatString->GetStartIndex()),
                                                                    length_(flatString->GetLength()) {}
    GateRef GetString() const
    {
        return string_;
    }

    GateRef GetStartIndex() const
    {
        return startIndex_;
    }

    GateRef GetLength() const
    {
        return length_;
    }
};
}  // namespace panda::ecmascript::kungfu
#endif  // ECMASCRIPT_COMPILER_BUILTINS_STRING_STUB_BUILDER_H
