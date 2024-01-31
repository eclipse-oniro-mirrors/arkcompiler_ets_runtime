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

#include "ecmascript/builtins/builtins_regexp.h"

#include <cmath>

#include "ecmascript/ecma_string-inl.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/interpreter/interpreter.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_object-inl.h"
#include "ecmascript/js_regexp.h"
#include "ecmascript/js_regexp_iterator.h"
#include "ecmascript/js_tagged_value-inl.h"
#include "ecmascript/mem/assert_scope.h"
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/object_factory.h"
#include "ecmascript/object_fast_operator-inl.h"
#include "ecmascript/regexp/regexp_parser_cache.h"
#include "ecmascript/tagged_array-inl.h"

namespace panda::ecmascript::builtins {
// 21.2.3.1
JSTaggedValue BuiltinsRegExp::RegExpConstructor(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), RegExp, Constructor);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> newTargetTemp = GetNewTarget(argv);
    JSHandle<JSTaggedValue> pattern = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> flags = GetCallArg(argv, 1);
    // 1. Let patternIsRegExp be IsRegExp(pattern).
    bool patternIsRegExp = JSObject::IsRegExp(thread, pattern);
    // 2. ReturnIfAbrupt(patternIsRegExp).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 3. If NewTarget is not undefined, let newTarget be NewTarget.
    JSHandle<JSTaggedValue> newTarget;
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    if (!newTargetTemp->IsUndefined()) {
        newTarget = newTargetTemp;
    } else {
        auto ecmaVm = thread->GetEcmaVM();
        JSHandle<GlobalEnv> env = ecmaVm->GetGlobalEnv();
        // disable gc
        [[maybe_unused]] DisallowGarbageCollection noGc;
        // 4.a Let newTarget be the active function object.
        newTarget = env->GetRegExpFunction();
        JSHandle<JSTaggedValue> constructorString = globalConst->GetHandledConstructorString();
        // 4.b If patternIsRegExp is true and flags is undefined
        if (patternIsRegExp && flags->IsUndefined()) {
            // 4.b.i Let patternConstructor be Get(pattern, "constructor").
            JSTaggedValue patternConstructor = ObjectFastOperator::FastGetPropertyByValue(
                thread, pattern.GetTaggedValue(), constructorString.GetTaggedValue());
            // 4.b.ii ReturnIfAbrupt(patternConstructor).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            // 4.b.iii If SameValue(newTarget, patternConstructor) is true, return pattern.
            if (JSTaggedValue::SameValue(newTarget.GetTaggedValue(), patternConstructor)) {
                return pattern.GetTaggedValue();
            }
        }
    }
    // 5. If Type(pattern) is Object and pattern has a [[RegExpMatcher]] internal slot
    bool isJsReg = false;
    if (pattern->IsECMAObject()) {
        JSHandle<JSObject> patternObj = JSHandle<JSObject>::Cast(pattern);
        isJsReg = patternObj->IsJSRegExp();
    }
    JSHandle<JSTaggedValue> patternTemp;
    JSHandle<JSTaggedValue> flagsTemp;
    if (isJsReg) {
        JSHandle<JSRegExp> patternReg(thread, JSRegExp::Cast(pattern->GetTaggedObject()));
        // 5.a Let P be the value of pattern’s [[OriginalSource]] internal slot.
        patternTemp = JSHandle<JSTaggedValue>(thread, patternReg->GetOriginalSource());
        if (flags->IsUndefined()) {
            // 5.b If flags is undefined, let F be the value of pattern’s [[OriginalFlags]] internal slot.
            flagsTemp = JSHandle<JSTaggedValue>(thread, patternReg->GetOriginalFlags());
        } else {
            // 5.c Else, let F be flags.
            flagsTemp = JSHandle<JSTaggedValue>(thread, *JSTaggedValue::ToString(thread, flags));
        }
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        // 6. Else if patternIsRegExp is true
    } else if (patternIsRegExp) {
        JSHandle<JSTaggedValue> sourceString(globalConst->GetHandledSourceString());
        JSHandle<JSTaggedValue> flagsString(globalConst->GetHandledFlagsString());
        // disable gc
        [[maybe_unused]] DisallowGarbageCollection noGc;
        // 6.a Let P be Get(pattern, "source").
        patternTemp = JSObject::GetProperty(thread, pattern, sourceString).GetValue();
        // 6.b ReturnIfAbrupt(P).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        // 6.c If flags is undefined
        if (flags->IsUndefined()) {
            // 6.c.i Let F be Get(pattern, "flags").
            flagsTemp = JSObject::GetProperty(thread, pattern, flagsString).GetValue();
            // 6.c.ii ReturnIfAbrupt(F).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        } else {
            // 6.d Else, let F be flags.
            flagsTemp = JSHandle<JSTaggedValue>(thread, *JSTaggedValue::ToString(thread, flags));
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
    } else {
        // 7.a Let P be pattern.
        patternTemp = pattern;
        // 7.b Let F be flags.
        if (flags->IsUndefined()) {
            flagsTemp = flags;
        } else {
            flagsTemp = JSHandle<JSTaggedValue>(thread, *JSTaggedValue::ToString(thread, flags));
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
    }
    // 8. Let O be RegExpAlloc(newTarget).
    JSHandle<JSTaggedValue> object(thread, RegExpAlloc(thread, newTarget));
    // 9. ReturnIfAbrupt(O).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 10. Return RegExpInitialize(O, P, F).
    JSTaggedValue result = RegExpInitialize(thread, object, patternTemp, flagsTemp);
    return JSTaggedValue(result);
}

// prototype
// 20.2.5.2
JSTaggedValue BuiltinsRegExp::Exec(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), RegExp, Exec);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    // 1. Let R be the this value.
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    // 4. Let S be ToString(string).
    JSHandle<JSTaggedValue> inputStr = GetCallArg(argv, 0);
    JSHandle<EcmaString> stringHandle = JSTaggedValue::ToString(thread, inputStr);
    // 5. ReturnIfAbrupt(S).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSTaggedValue> string = JSHandle<JSTaggedValue>::Cast(stringHandle);
    // 2. If Type(R) is not Object, throw a TypeError exception.
    if (!thisObj->IsECMAObject()) {
        // throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue::Exception());
    }
    // 3. If R does not have a [[RegExpMatcher]] internal slot, throw a TypeError exception.
    if (!thisObj->IsJSRegExp()) {
        // throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this does not have [[RegExpMatcher]]", JSTaggedValue::Exception());
    }
    if (BuiltinsRegExp::IsValidRegularExpression(thread, thisObj) == JSTaggedValue::False()) {
        THROW_SYNTAX_ERROR_AND_RETURN(thread, "Regular expression too large", JSTaggedValue::Exception());
    }

    bool useCache = true;
    JSHandle<RegExpExecResultCache> cacheTable(thread->GetCurrentEcmaContext()->GetRegExpCache());
    if (cacheTable->GetLargeStrCount() == 0 || cacheTable->GetConflictCount() == 0) {
        useCache = false;
    }

    // 6. Return RegExpBuiltinExec(R, S).
    JSTaggedValue result = RegExpBuiltinExec(thread, thisObj, string, useCache);
    return JSTaggedValue(result);
}

// 20.2.5.13
JSTaggedValue BuiltinsRegExp::Test(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), RegExp, Test);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    // 1. Let R be the this value.
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    JSHandle<JSTaggedValue> inputStr = GetCallArg(argv, 0);
    // 2. If Type(R) is not Object, throw a TypeError exception.
    if (!thisObj->IsECMAObject()) {
        // throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue::Exception());
    }
    // 3. Let string be ToString(S).
    // 4. ReturnIfAbrupt(string).
    JSHandle<EcmaString> stringHandle = JSTaggedValue::ToString(thread, inputStr);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSTaggedValue> string = JSHandle<JSTaggedValue>::Cast(stringHandle);
    // test fast path
    if (IsFastRegExp(thread, thisObj)) {
        return RegExpTestFast(thread, thisObj, string, true);
    }

    // 5. Let match be RegExpExec(R, string).
    JSTaggedValue matchResult = RegExpExec(thread, thisObj, string, false);
    // 6. ReturnIfAbrupt(match).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 7. If match is not null, return true; else return false.
    return GetTaggedBoolean(!matchResult.IsNull());
}

bool BuiltinsRegExp::IsFastRegExp(JSThread *thread, JSHandle<JSTaggedValue> &regexp)
{
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    JSHClass *hclass = JSHandle<JSObject>::Cast(regexp)->GetJSHClass();
    JSHClass *originHClass = JSHClass::Cast(globalConst->GetJSRegExpClass().GetTaggedObject());
    // regexp instance hclass
    if (hclass != originHClass) {
        return false;
    }
    // RegExp.prototype hclass
    JSTaggedValue proto = hclass->GetPrototype();
    JSHClass *regexpHclass = proto.GetTaggedObject()->GetClass();
    JSHandle<JSTaggedValue> originRegexpClassValue = env->GetRegExpPrototypeClass();
    JSHClass *originRegexpHclass = JSHClass::Cast(originRegexpClassValue.GetTaggedValue().GetTaggedObject());
    if (regexpHclass != originRegexpHclass) {
        return false;
    }
    // RegExp.prototype.exec
    auto execVal = JSObject::Cast(proto)->GetPropertyInlinedProps(JSRegExp::EXEC_INLINE_PROPERTY_INDEX);
    if (execVal != env->GetTaggedRegExpExecFunction()) {
        return false;
    }
    return true;
}

JSTaggedValue BuiltinsRegExp::RegExpTestFast(JSThread *thread, JSHandle<JSTaggedValue> &regexp,
                                             const JSHandle<JSTaggedValue> &inputStr, bool useCache)
{
    // 1. Assert: Type(S) is String.
    ASSERT(inputStr->IsString());
    // 2. If R does not have a [[RegExpMatcher]] internal slot, throw a TypeError exception.
    if (!regexp->IsJSRegExp()) {
        // throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this does not have a [[RegExpMatcher]]", JSTaggedValue::Exception());
    }
    if (BuiltinsRegExp::IsValidRegularExpression(thread, regexp) == JSTaggedValue::False()) {
        THROW_SYNTAX_ERROR_AND_RETURN(thread, "Regular expression too large", JSTaggedValue::Exception());
    }
    return RegExpExecForTestFast(thread, regexp, inputStr, useCache);
}

// 20.2.5.14
JSTaggedValue BuiltinsRegExp::ToString(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), RegExp, ToString);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    // 1. Let R be the this value.
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    auto ecmaVm = thread->GetEcmaVM();
    // 2. If Type(R) is not Object, throw a TypeError exception.
    if (!thisObj->IsECMAObject()) {
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue::Exception());
    }
    ObjectFactory *factory = ecmaVm->GetFactory();
    const GlobalEnvConstants *globalConstants = thread->GlobalConstants();
    JSHandle<JSTaggedValue> sourceString(globalConstants->GetHandledSourceString());
    JSHandle<JSTaggedValue> flagsString(globalConstants->GetHandledFlagsString());
    // 3. Let pattern be ToString(Get(R, "source")).
    JSHandle<JSTaggedValue> getSource(JSObject::GetProperty(thread, thisObj, sourceString).GetValue());
    JSHandle<JSTaggedValue> getFlags(JSObject::GetProperty(thread, thisObj, flagsString).GetValue());
    JSHandle<EcmaString> sourceStrHandle = JSTaggedValue::ToString(thread, getSource);
    // 4. ReturnIfAbrupt(pattern).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 5. Let flags be ToString(Get(R, "flags")).
    JSHandle<EcmaString> flagsStrHandle = JSTaggedValue::ToString(thread, getFlags);
    // 4. ReturnIfAbrupt(flags).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<EcmaString> slashStr = JSHandle<EcmaString>::Cast(globalConstants->GetHandledBackslashString());
    // 7. Let result be the String value formed by concatenating "/", pattern, and "/", and flags.
    JSHandle<EcmaString> tempStr = factory->ConcatFromString(slashStr, sourceStrHandle);
    JSHandle<EcmaString> resultTemp = factory->ConcatFromString(tempStr, slashStr);
    return factory->ConcatFromString(resultTemp, flagsStrHandle).GetTaggedValue();
}

// 20.2.5.3
JSTaggedValue BuiltinsRegExp::GetFlags(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), RegExp, GetFlags);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    // 1. Let R be the this value.
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    // 2. If Type(R) is not Object, throw a TypeError exception.
    if (!thisObj->IsECMAObject()) {
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue::Exception());
    }
    // 3. Let result be the empty String.
    // 4. ~ 19.
    if (!JSHandle<JSObject>::Cast(thisObj)->IsJSRegExp()) {
        return GetAllFlagsInternal(thread, thisObj);
    }
    uint8_t flagsBits = static_cast<uint8_t>(JSRegExp::Cast(thisObj->GetTaggedObject())->GetOriginalFlags().GetInt());
    return FlagsBitsToString(thread, flagsBits);
}

JSTaggedValue BuiltinsRegExp::GetAllFlagsInternal(JSThread *thread, JSHandle<JSTaggedValue> &thisObj)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    const GlobalEnvConstants *globalConstants = thread->GlobalConstants();
    uint8_t *flagsStr = new uint8_t[RegExpParser::FLAG_NUM + 1];  // FLAG_NUM flags + '\0'
    size_t flagsLen = 0;
    JSHandle<EcmaString> emptyString = factory->GetEmptyString();
    JSHandle<JSTaggedValue> hasIndicesKey(factory->NewFromASCII("hasIndices"));
    JSHandle<JSTaggedValue> hasIndicesResult = JSObject::GetProperty(thread, thisObj, hasIndicesKey).GetValue();
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, emptyString.GetTaggedValue());
    if (hasIndicesResult->ToBoolean()) {
        flagsStr[flagsLen] = 'd';
        flagsLen++;
    }
    JSHandle<JSTaggedValue> globalKey(globalConstants->GetHandledGlobalString());
    JSHandle<JSTaggedValue> globalResult = JSObject::GetProperty(thread, thisObj, globalKey).GetValue();
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, emptyString.GetTaggedValue());
    if (globalResult->ToBoolean()) {
        flagsStr[flagsLen] = 'g';
        flagsLen++;
    }
    JSHandle<JSTaggedValue> ignoreCaseKey(factory->NewFromASCII("ignoreCase"));
    JSHandle<JSTaggedValue> ignoreCaseResult = JSObject::GetProperty(thread, thisObj, ignoreCaseKey).GetValue();
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, emptyString.GetTaggedValue());
    if (ignoreCaseResult->ToBoolean()) {
        flagsStr[flagsLen] = 'i';
        flagsLen++;
    }
    JSHandle<JSTaggedValue> multilineKey(factory->NewFromASCII("multiline"));
    JSHandle<JSTaggedValue> multilineResult = JSObject::GetProperty(thread, thisObj, multilineKey).GetValue();
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, emptyString.GetTaggedValue());
    if (multilineResult->ToBoolean()) {
        flagsStr[flagsLen] = 'm';
        flagsLen++;
    }
    JSHandle<JSTaggedValue> dotAllKey(factory->NewFromASCII("dotAll"));
    JSHandle<JSTaggedValue> dotAllResult = JSObject::GetProperty(thread, thisObj, dotAllKey).GetValue();
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, emptyString.GetTaggedValue());
    if (dotAllResult->ToBoolean()) {
        flagsStr[flagsLen] = 's';
        flagsLen++;
    }
    JSHandle<JSTaggedValue> unicodeKey(globalConstants->GetHandledUnicodeString());
    JSHandle<JSTaggedValue> unicodeResult = JSObject::GetProperty(thread, thisObj, unicodeKey).GetValue();
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, emptyString.GetTaggedValue());
    if (unicodeResult->ToBoolean()) {
        flagsStr[flagsLen] = 'u';
        flagsLen++;
    }
    JSHandle<JSTaggedValue> stickyKey(globalConstants->GetHandledStickyString());
    JSHandle<JSTaggedValue> stickyResult = JSObject::GetProperty(thread, thisObj, stickyKey).GetValue();
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, emptyString.GetTaggedValue());
    if (stickyResult->ToBoolean()) {
        flagsStr[flagsLen] = 'y';
        flagsLen++;
    }
    flagsStr[flagsLen] = '\0';
    JSHandle<EcmaString> flagsString = factory->NewFromUtf8(flagsStr, flagsLen);
    delete[] flagsStr;

    return flagsString.GetTaggedValue();
}

// 20.2.5.4
JSTaggedValue BuiltinsRegExp::GetGlobal(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, RegExp, GetGlobal);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    JSHandle<JSTaggedValue> constructor = GetConstructor(argv);
    return GetFlagsInternal(thread, thisObj, constructor, RegExpParser::FLAG_GLOBAL);
}

// 22.2.6.6
JSTaggedValue BuiltinsRegExp::GetHasIndices(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, RegExp, GetHasIndices);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    JSHandle<JSTaggedValue> constructor = GetConstructor(argv);
    return GetFlagsInternal(thread, thisObj, constructor, RegExpParser::FLAG_HASINDICES);
}

// 20.2.5.5
JSTaggedValue BuiltinsRegExp::GetIgnoreCase(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, RegExp, GetIgnoreCase);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    JSHandle<JSTaggedValue> constructor = GetConstructor(argv);
    return GetFlagsInternal(thread, thisObj, constructor, RegExpParser::FLAG_IGNORECASE);
}

// 20.2.5.7
JSTaggedValue BuiltinsRegExp::GetMultiline(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, RegExp, GetMultiline);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    JSHandle<JSTaggedValue> constructor = GetConstructor(argv);
    return GetFlagsInternal(thread, thisObj, constructor, RegExpParser::FLAG_MULTILINE);
}

JSTaggedValue BuiltinsRegExp::GetDotAll(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, RegExp, GetDotAll);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    JSHandle<JSTaggedValue> constructor = GetConstructor(argv);
    return GetFlagsInternal(thread, thisObj, constructor, RegExpParser::FLAG_DOTALL);
}

// 20.2.5.10
JSTaggedValue BuiltinsRegExp::GetSource(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, RegExp, GetSource);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    // 1. Let R be the this value.
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    // 2. If Type(R) is not Object, throw a TypeError exception.
    // 3. If R does not have an [[OriginalSource]] internal slot, throw a TypeError exception.
    // 4. If R does not have an [[OriginalFlags]] internal slot, throw a TypeError exception.
    if (!thisObj->IsECMAObject()) {
        // throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue::Exception());
    }
    if (!thisObj->IsJSRegExp()) {
        // a. If SameValue(R, %RegExp.prototype%) is true, return "(?:)".
        const GlobalEnvConstants *globalConst = thread->GlobalConstants();
        JSHandle<JSTaggedValue> constructorKey = globalConst->GetHandledConstructorString();
        JSHandle<JSTaggedValue> objConstructor = JSTaggedValue::GetProperty(thread, thisObj, constructorKey).GetValue();
        RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, JSTaggedValue(false));
        JSHandle<JSTaggedValue> constructor = GetConstructor(argv);
        if (objConstructor->IsJSFunction() && constructor->IsJSFunction()) {
            JSHandle<GlobalEnv> objRealm = JSObject::GetFunctionRealm(thread, objConstructor);
            JSHandle<GlobalEnv> ctorRealm = JSObject::GetFunctionRealm(thread, constructor);
            if (objRealm->GetRegExpPrototype() == thisObj && *objRealm == *ctorRealm) {
                JSHandle<EcmaString> result = thread->GetEcmaVM()->GetFactory()->NewFromASCII("(?:)");
                return result.GetTaggedValue();
            }
        }
        // b. throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this does not have [[OriginalSource]]", JSTaggedValue::Exception());
    }
    // 5. Let src be the value of R’s [[OriginalSource]] internal slot.
    JSHandle<JSRegExp> regexpObj(thread, JSRegExp::Cast(thisObj->GetTaggedObject()));
    JSHandle<JSTaggedValue> source(thread, regexpObj->GetOriginalSource());
    // 6. Let flags be the value of R’s [[OriginalFlags]] internal slot.
    uint8_t flagsBits = static_cast<uint8_t>(regexpObj->GetOriginalFlags().GetInt());
    JSHandle<JSTaggedValue> flags(thread, FlagsBitsToString(thread, flagsBits));
    // 7. Return EscapeRegExpPattern(src, flags).
    return JSTaggedValue(EscapeRegExpPattern(thread, source, flags));
}

// 20.2.5.12
JSTaggedValue BuiltinsRegExp::GetSticky(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, RegExp, GetSticky);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    JSHandle<JSTaggedValue> constructor = GetConstructor(argv);
    return GetFlagsInternal(thread, thisObj, constructor, RegExpParser::FLAG_STICKY);
}

// 20.2.5.15
JSTaggedValue BuiltinsRegExp::GetUnicode(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, RegExp, GetUnicode);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    JSHandle<JSTaggedValue> constructor = GetConstructor(argv);
    return GetFlagsInternal(thread, thisObj, constructor, RegExpParser::FLAG_UTF16);
}

// 21.2.4.2
JSTaggedValue BuiltinsRegExp::GetSpecies(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), RegExp, GetSpecies);
    return GetThis(argv).GetTaggedValue();
}

// 21.2.5.6
JSTaggedValue BuiltinsRegExp::Match(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), RegExp, Match);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    // 1. Let rx be the this value.
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    // 3. Let S be ToString(string)
    JSHandle<JSTaggedValue> inputString = GetCallArg(argv, 0);
    JSHandle<EcmaString> stringHandle = JSTaggedValue::ToString(thread, inputString);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    bool useCache = true;
    JSHandle<RegExpExecResultCache> cacheTable(thread->GetCurrentEcmaContext()->GetRegExpCache());
    if (cacheTable->GetLargeStrCount() == 0 || cacheTable->GetConflictCount() == 0) {
        useCache = false;
    }
    // 4. ReturnIfAbrupt(string).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSTaggedValue> string = JSHandle<JSTaggedValue>::Cast(stringHandle);
    if (!thisObj->IsECMAObject()) {
        // 2. If Type(rx) is not Object, throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue::Exception());
    }

    JSHandle<JSRegExp> regexpObj(thisObj);
    JSMutableHandle<JSTaggedValue> pattern(thread, JSTaggedValue::Undefined());
    JSMutableHandle<JSTaggedValue> flags(thread, JSTaggedValue::Undefined());
    if (thisObj->IsJSRegExp()) {
        pattern.Update(regexpObj->GetOriginalSource());
        flags.Update(regexpObj->GetOriginalFlags());
    }

    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    bool isGlobal = false;
    bool fullUnicode = false;
    bool unmodified = IsFastRegExp(thread, thisObj);
    if (unmodified) {
        uint8_t flagsBits = static_cast<uint8_t>(flags->GetInt());
        isGlobal = (flagsBits & RegExpParser::FLAG_GLOBAL) != 0;
        fullUnicode = (flagsBits & RegExpParser::FLAG_UTF16) != 0;
    } else {
        // 5. Let global be ToBoolean(Get(rx, "global")).
        JSHandle<JSTaggedValue> global = globalConst->GetHandledGlobalString();
        JSTaggedValue globalValue =
            ObjectFastOperator::FastGetPropertyByValue(thread, thisObj.GetTaggedValue(), global.GetTaggedValue());
        // 6. ReturnIfAbrupt(global).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        isGlobal = globalValue.ToBoolean();
    }
    // 7. If global is false, then
    if (!isGlobal) {
        // a. Return RegExpExec(rx, S).
        if (useCache) {
            JSTaggedValue cacheResult = cacheTable->FindCachedResult(thread, pattern, flags, inputString,
                                                                     RegExpExecResultCache::EXEC_TYPE, thisObj,
                                                                     JSTaggedValue(0));
            if (!cacheResult.IsUndefined()) {
                return cacheResult;
            }
        }
        JSTaggedValue result = RegExpExec(thread, thisObj, string, useCache);
        return JSTaggedValue(result);
    }

    if (useCache) {
        JSTaggedValue cacheResult = cacheTable->FindCachedResult(thread, pattern, flags, inputString,
                                                                 RegExpExecResultCache::MATCH_TYPE, thisObj,
                                                                 JSTaggedValue(0));
        if (!cacheResult.IsUndefined()) {
            return cacheResult;
        }
    }

    if (!unmodified) {
        // 8. Else global is true
        // a. Let fullUnicode be ToBoolean(Get(rx, "unicode")).
        JSHandle<JSTaggedValue> unicode = globalConst->GetHandledUnicodeString();
        JSTaggedValue uincodeValue =
            ObjectFastOperator::FastGetPropertyByValue(thread, thisObj.GetTaggedValue(), unicode.GetTaggedValue());
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        fullUnicode = uincodeValue.ToBoolean();
    }
    // b. Let setStatus be Set(rx, "lastIndex", 0, true).
    JSHandle<JSTaggedValue> lastIndexString(globalConst->GetHandledLastIndexString());
    ObjectFastOperator::FastSetPropertyByValue(thread, thisObj.GetTaggedValue(), lastIndexString.GetTaggedValue(),
                                               JSTaggedValue(0));
    // c. ReturnIfAbrupt(setStatus).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // d. Let A be ArrayCreate(0).
    JSHandle<JSObject> array(JSArray::ArrayCreate(thread, JSTaggedNumber(0)));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // e. Let n be 0.
    int resultNum = 0;
    JSMutableHandle<JSTaggedValue> result(thread, JSTaggedValue(0));
    // f. Repeat,
    while (true) {
        // i. Let result be RegExpExec(rx, S).
        result.Update(RegExpExec(thread, thisObj, string, useCache));

        // ii. ReturnIfAbrupt(result).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        // iii. If result is null, then
        if (result->IsNull()) {
            // 1. If n=0, return null.
            if (resultNum == 0) {
                return JSTaggedValue::Null();
            }
            if (useCache) {
                RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flags, inputString,
                                                        JSHandle<JSTaggedValue>(array),
                                                        RegExpExecResultCache::MATCH_TYPE, 0, 0);
            }
            // 2. Else, return A.
            return array.GetTaggedValue();
        }
        // iv. Else result is not null,
        // 1. Let matchStr be ToString(Get(result, "0")).
        JSHandle<JSTaggedValue> zeroString = globalConst->GetHandledZeroString();
        JSTaggedValue matchVal = ObjectFastOperator::FastGetPropertyByValue(
            thread, result.GetTaggedValue(), zeroString.GetTaggedValue());
        JSHandle<JSTaggedValue> matchStr(thread, matchVal);
        JSHandle<EcmaString> matchString = JSTaggedValue::ToString(thread, matchStr);
        // 2. ReturnIfAbrupt(matchStr).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        JSHandle<JSTaggedValue> matchValue = JSHandle<JSTaggedValue>::Cast(matchString);
        // 3. Let status be CreateDataProperty(A, ToString(n), matchStr).
        JSObject::CreateDataProperty(thread, array, resultNum, matchValue);
        // 5. If matchStr is the empty String, then
        if (EcmaStringAccessor(JSTaggedValue::ToString(thread, matchValue)).GetLength() == 0) {
            // a. Let thisIndex be ToLength(Get(rx, "lastIndex")).
            JSTaggedValue lastIndex = ObjectFastOperator::FastGetPropertyByValue(thread, thisObj.GetTaggedValue(),
                                                                                 lastIndexString.GetTaggedValue());
            JSHandle<JSTaggedValue> lastIndexHandle(thread, lastIndex);
            JSTaggedNumber thisIndex = JSTaggedValue::ToLength(thread, lastIndexHandle);
            // b. ReturnIfAbrupt(thisIndex).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            // c. Let nextIndex be AdvanceStringIndex(S, thisIndex, fullUnicode).
            // d. Let setStatus be Set(rx, "lastIndex", nextIndex, true).
            JSTaggedValue nextIndex =
                JSTaggedValue(AdvanceStringIndex(string, thisIndex.GetNumber(), fullUnicode));
            ObjectFastOperator::FastSetPropertyByValue(thread, thisObj.GetTaggedValue(),
                                                       lastIndexString.GetTaggedValue(),
                                                       nextIndex);
            // e. ReturnIfAbrupt(setStatus).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
        // 6. Increase n.
        resultNum++;
    }
}

JSTaggedValue BuiltinsRegExp::MatchAll(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, RegExp, MatchAll);
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    // 1. Let R be the this value.
    // 2. If Type(R) is not Object, throw a TypeError exception.
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    auto ecmaVm = thread->GetEcmaVM();
    if (!thisObj->IsECMAObject()) {
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue::Exception());
    }

    // 3. Let S be ? ToString(string).
    JSHandle<JSTaggedValue> inputString = GetCallArg(argv, 0);
    JSHandle<EcmaString> stringHandle = JSTaggedValue::ToString(thread, inputString);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);

    // 4. Let C be ? SpeciesConstructor(R, %RegExp%).
    JSHandle<JSTaggedValue> defaultConstructor = ecmaVm->GetGlobalEnv()->GetRegExpFunction();
    JSHandle<JSObject> objHandle(thisObj);
    JSHandle<JSTaggedValue> constructor = JSObject::SpeciesConstructor(thread, objHandle, defaultConstructor);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);

    const GlobalEnvConstants *globalConstants = thread->GlobalConstants();
    // 5. Let flags be ? ToString(? Get(R, "flags")).
    JSHandle<JSTaggedValue> flagsString(globalConstants->GetHandledFlagsString());
    JSHandle<JSTaggedValue> getFlags(JSObject::GetProperty(thread, thisObj, flagsString).GetValue());
    JSHandle<EcmaString> flagsStrHandle = JSTaggedValue::ToString(thread, getFlags);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);

    // 6. Let matcher be ? Construct(C, « R, flags »).
    JSHandle<JSTaggedValue> undefined = globalConstants->GetHandledUndefined();
    EcmaRuntimeCallInfo *runtimeInfo =
        EcmaInterpreter::NewRuntimeCallInfo(thread, constructor, undefined, undefined, 2); // 2: two args
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    runtimeInfo->SetCallArg(thisObj.GetTaggedValue(), flagsStrHandle.GetTaggedValue());
    JSTaggedValue taggedMatcher = JSFunction::Construct(runtimeInfo);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSTaggedValue> matcherHandle(thread, taggedMatcher);

    // 7. Let lastIndex be ? ToLength(? Get(R, "lastIndex")).
    JSHandle<JSTaggedValue> lastIndexString(globalConstants->GetHandledLastIndexString());
    JSHandle<JSTaggedValue> getLastIndex(JSObject::GetProperty(thread, thisObj, lastIndexString).GetValue());
    JSTaggedNumber thisLastIndex = JSTaggedValue::ToLength(thread, getLastIndex);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);

    // 8. Perform ? Set(matcher, "lastIndex", lastIndex, true).
    ObjectFastOperator::FastSetPropertyByValue(thread, matcherHandle.GetTaggedValue(), lastIndexString.GetTaggedValue(),
                                               thisLastIndex);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);

    // 9. If flags contains "g", let global be true.
    // 10. Else, let global be false.
    JSHandle<EcmaString> gString(globalConstants->GetHandledGString());
    bool global = false;
    if (EcmaStringAccessor::IndexOf(ecmaVm, flagsStrHandle, gString) != -1) {
        global = true;
    }

    // 11. If flags contains "u", let fullUnicode be true.
    // 12. Else, let fullUnicode be false.
    JSHandle<EcmaString> uString(globalConstants->GetHandledUString());
    bool fullUnicode = false;
    if (EcmaStringAccessor::IndexOf(ecmaVm, flagsStrHandle, uString) != -1) {
        fullUnicode = true;
    }

    // 13. Return ! CreateRegExpStringIterator(matcher, S, global, fullUnicode).
    return JSRegExpIterator::CreateRegExpStringIterator(thread, matcherHandle,
                                                        stringHandle, global, fullUnicode).GetTaggedValue();
}

JSTaggedValue BuiltinsRegExp::RegExpReplaceFast(JSThread *thread, JSHandle<JSTaggedValue> &regexp,
                                                JSHandle<EcmaString> inputString, uint32_t inputLength)
{
    ASSERT(regexp->IsJSRegExp());
    BUILTINS_API_TRACE(thread, RegExp, RegExpReplaceFast);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    // get bytecode
    JSTaggedValue bufferData = JSRegExp::Cast(regexp->GetTaggedObject())->GetByteCodeBuffer();
    void *dynBuf = JSNativePointer::Cast(bufferData.GetTaggedObject())->GetExternalPointer();
    // get flags
    auto bytecodeBuffer = reinterpret_cast<uint8_t *>(dynBuf);
    uint32_t flags = *reinterpret_cast<uint32_t *>(bytecodeBuffer + RegExpParser::FLAGS_OFFSET);
    JSHandle<JSTaggedValue> lastIndexHandle(thread->GlobalConstants()->GetHandledLastIndexString());
    uint32_t lastIndex = 0;
    JSHandle<JSRegExp> regexpHandle(regexp);
    bool useCache = false;
    if ((flags & (RegExpParser::FLAG_STICKY | RegExpParser::FLAG_GLOBAL)) == 0) {
        lastIndex = 0;
    } else {
        JSTaggedValue thisIndex =
            ObjectFastOperator::FastGetPropertyByValue(thread, regexp.GetTaggedValue(),
                                                       lastIndexHandle.GetTaggedValue());
        if (thisIndex.IsInt()) {
            lastIndex = static_cast<uint32_t>(thisIndex.GetInt());
        } else {
            JSHandle<JSTaggedValue> thisIndexHandle(thread, thisIndex);
            lastIndex = JSTaggedValue::ToLength(thread, thisIndexHandle).GetNumber();
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
    }

    auto globalConst = thread->GlobalConstants();
    JSHandle<JSTaggedValue> tagInputString = JSHandle<JSTaggedValue>::Cast(inputString);
    JSHandle<JSTaggedValue> pattern(thread, regexpHandle->GetOriginalSource());
    JSHandle<JSTaggedValue> flagsBits(thread, regexpHandle->GetOriginalFlags());

    JSHandle<RegExpExecResultCache> cacheTable(thread->GetCurrentEcmaContext()->GetRegExpCache());
    uint32_t length = EcmaStringAccessor(inputString).GetLength();
    uint32_t largeStrCount = cacheTable->GetLargeStrCount();
    if (largeStrCount != 0) {
        if (length > MIN_REPLACE_STRING_LENGTH) {
            cacheTable->SetLargeStrCount(thread, --largeStrCount);
        }
    } else {
        cacheTable->SetStrLenThreshold(thread, MIN_REPLACE_STRING_LENGTH);
    }
    if (length > cacheTable->GetStrLenThreshold()) {
        useCache = true;
    }
    uint32_t lastIndexInput = lastIndex;
    if (useCache) {
        JSTaggedValue cacheResult = cacheTable->FindCachedResult(thread, pattern, flagsBits, tagInputString,
                                                                 RegExpExecResultCache::REPLACE_TYPE, regexp,
                                                                 JSTaggedValue(lastIndexInput),
                                                                 globalConst->GetEmptyString());
        if (!cacheResult.IsUndefined()) {
            return cacheResult;
        }
    }

    std::string resultString;
    uint32_t nextPosition = 0;
    JSHandle<RegExpGlobalResult> globalTable(thread->GetCurrentEcmaContext()->GetRegExpGlobalResult());
    // 12. Let done be false.
    // 13. Repeat, while done is false
    for (;;) {
        if (lastIndex > inputLength) {
            break;
        }
        bool matchResult = RegExpExecInternal(thread, regexp, inputString, lastIndex);
        if (!matchResult) {
            if (flags & (RegExpParser::FLAG_STICKY | RegExpParser::FLAG_GLOBAL)) {
                lastIndex = 0;
                ObjectFastOperator::FastSetPropertyByValue(thread, regexp.GetTaggedValue(),
                                                           lastIndexHandle.GetTaggedValue(), JSTaggedValue(0));
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            }
            break;
        }
        uint32_t startIndex = static_cast<uint32_t>(globalTable->GetStartOfCaptureIndex(0).GetInt());
        uint32_t endIndex = static_cast<uint32_t>(globalTable->GetEndIndex().GetInt());
        lastIndex = endIndex;
        if (nextPosition < startIndex) {
            auto substr = EcmaStringAccessor::FastSubString(
                thread->GetEcmaVM(), inputString, nextPosition, startIndex - nextPosition);
            resultString += EcmaStringAccessor(substr).ToStdString(StringConvertedUsage::LOGICOPERATION);
        }
        nextPosition = endIndex;
        if (!(flags & RegExpParser::FLAG_GLOBAL)) {
            // a. Let setStatus be Set(R, "lastIndex", e, true).
            ObjectFastOperator::FastSetPropertyByValue(thread, regexp.GetTaggedValue(),
                                                       lastIndexHandle.GetTaggedValue(),
                                                       JSTaggedValue(lastIndex));
            // b. ReturnIfAbrupt(setStatus).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            break;
        }
        if (endIndex == startIndex) {
            bool unicode = EcmaStringAccessor(inputString).IsUtf16() && (flags & RegExpParser::FLAG_UTF16);
            endIndex = static_cast<uint32_t>(AdvanceStringIndex(tagInputString, endIndex, unicode));
        }
        lastIndex = endIndex;
    }
    auto substr = EcmaStringAccessor::FastSubString(
        thread->GetEcmaVM(), inputString, nextPosition, inputLength - nextPosition);
    resultString += EcmaStringAccessor(substr).ToStdString(StringConvertedUsage::LOGICOPERATION);
    auto resultValue = factory->NewFromStdString(resultString);
    if (useCache) {
        RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flagsBits, tagInputString,
                                                JSHandle<JSTaggedValue>(resultValue),
                                                RegExpExecResultCache::REPLACE_TYPE, lastIndexInput, lastIndex,
                                                globalConst->GetEmptyString());
    }
    return resultValue.GetTaggedValue();
}

// 21.2.5.8
// NOLINTNEXTLINE(readability-function-size)
JSTaggedValue BuiltinsRegExp::Replace(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), RegExp, Replace);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    // 1. Let rx be the this value.
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    if (!thisObj->IsECMAObject()) {
        // 2. If Type(rx) is not Object, throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue::Exception());
    }
    // 3. Let S be ToString(string).
    JSHandle<JSTaggedValue> string = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> inputReplaceValue = GetCallArg(argv, 1);
    return ReplaceInternal(thread, thisObj, string, inputReplaceValue);
}

JSTaggedValue BuiltinsRegExp::ReplaceInternal(JSThread *thread,
                                              JSHandle<JSTaggedValue> thisObj,
                                              JSHandle<JSTaggedValue> string,
                                              JSHandle<JSTaggedValue> inputReplaceValue)
{
    JSHandle<EcmaString> srcString = JSTaggedValue::ToString(thread, string);
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();

    // 4. ReturnIfAbrupt(S).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSTaggedValue> inputStr = JSHandle<JSTaggedValue>::Cast(srcString);
    // 5. Let lengthS be the number of code unit elements in S.
    uint32_t length = EcmaStringAccessor(srcString).GetLength();
    // 6. Let functionalReplace be IsCallable(replaceValue).
    bool functionalReplace = inputReplaceValue->IsCallable();
    JSHandle<EcmaString> replaceValueHandle;
    if (!functionalReplace) {
        replaceValueHandle = JSTaggedValue::ToString(thread, inputReplaceValue);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    }
    JSHandle<JSTaggedValue> lastIndex = globalConst->GetHandledLastIndexString();
    // 8. Let global be ToBoolean(Get(rx, "global")).
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    bool isGlobal = false;
    bool fullUnicode = false;
    bool unmodified = IsFastRegExp(thread, thisObj);
    if (unmodified) {
        JSHandle<JSRegExp> regexpObj(thisObj);
        uint8_t flagsBits = static_cast<uint8_t>(regexpObj->GetOriginalFlags().GetInt());
        isGlobal = (flagsBits & RegExpParser::FLAG_GLOBAL) != 0;
        fullUnicode = (flagsBits & RegExpParser::FLAG_UTF16) != 0;
        if (isGlobal) {
            ObjectFastOperator::FastSetPropertyByValue(thread, thisObj.GetTaggedValue(),
                                                       lastIndex.GetTaggedValue(), JSTaggedValue(0));
            // ReturnIfAbrupt(setStatus).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
    } else {
        JSHandle<JSTaggedValue> global = globalConst->GetHandledGlobalString();
        JSTaggedValue globalValue =
            ObjectFastOperator::FastGetPropertyByValue(thread, thisObj.GetTaggedValue(), global.GetTaggedValue());
        // 9. ReturnIfAbrupt(global).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        isGlobal = globalValue.ToBoolean();
        // 10. If global is true, then
        if (isGlobal) {
            // a. Let fullUnicode be ToBoolean(Get(rx, "unicode")).
            JSHandle<JSTaggedValue> unicode = globalConst->GetHandledUnicodeString();
            JSTaggedValue fullUnicodeTag =
                ObjectFastOperator::FastGetPropertyByValue(thread, thisObj.GetTaggedValue(), unicode.GetTaggedValue());
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            fullUnicode = fullUnicodeTag.ToBoolean();
            // b. Let setStatus be Set(rx, "lastIndex", 0, true).
            ObjectFastOperator::FastSetPropertyByValue(thread, thisObj.GetTaggedValue(),
                                                       lastIndex.GetTaggedValue(), JSTaggedValue(0));
            // c. ReturnIfAbrupt(setStatus).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
    }

    // Add cache for regexp replace
    bool useCache = false;
    // Add cache for the intermediate result of replace
    bool useIntermediateCache = false;
    JSMutableHandle<JSTaggedValue> pattern(thread, JSTaggedValue::Undefined());
    JSMutableHandle<JSTaggedValue> flagsBits(thread, JSTaggedValue::Undefined());
    JSHandle<RegExpExecResultCache> cacheTable(thread->GetCurrentEcmaContext()->GetRegExpCache());
    if (isGlobal && thisObj->IsJSRegExp()) {
        JSHClass *hclass = JSHandle<JSObject>::Cast(thisObj)->GetJSHClass();
        JSHClass *originHClass = JSHClass::Cast(globalConst->GetJSRegExpClass().GetTaggedObject());
        if (hclass == originHClass) {
            if (!functionalReplace && EcmaStringAccessor(replaceValueHandle).GetLength() == 0) {
                return RegExpReplaceFast(thread, thisObj, srcString, length);
            }
            JSHandle<JSRegExp> regexpHandle(thisObj);
            if (regexpHandle->IsJSRegExp()) {
                useIntermediateCache = true;
                pattern.Update(regexpHandle->GetOriginalSource());
                flagsBits.Update(regexpHandle->GetOriginalFlags());
            }
            if (!functionalReplace) {
                uint32_t strLength = EcmaStringAccessor(replaceValueHandle).GetLength();
                uint32_t largeStrCount = cacheTable->GetLargeStrCount();
                if (largeStrCount != 0) {
                    if (strLength > MIN_REPLACE_STRING_LENGTH) {
                        cacheTable->SetLargeStrCount(thread, --largeStrCount);
                    }
                } else {
                    cacheTable->SetStrLenThreshold(thread, MIN_REPLACE_STRING_LENGTH);
                }
                if (strLength > cacheTable->GetStrLenThreshold()) {
                    useCache = true;
                    JSTaggedValue cacheResult = cacheTable->FindCachedResult(thread, pattern, flagsBits, string,
                                                                             RegExpExecResultCache::REPLACE_TYPE,
                                                                             thisObj, JSTaggedValue(0),
                                                                             inputReplaceValue.GetTaggedValue());
                    if (!cacheResult.IsUndefined()) {
                        return cacheResult;
                    }
                }
            }
        }
    }

    JSHandle<JSTaggedValue> matchedStr = globalConst->GetHandledZeroString();
    // 11. Let results be a new empty List.
    JSMutableHandle<JSObject> resultsList(thread, JSArray::ArrayCreate(thread, JSTaggedNumber(0)));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    int resultsIndex = 0;
    JSMutableHandle<JSTaggedValue> nextIndexHandle(thread, JSTaggedValue(0));
    JSMutableHandle<JSTaggedValue> execResult(thread, JSTaggedValue(0));
    // Add cache for the intermediate result of replace
    JSTaggedValue cachedResultsList(JSTaggedValue::VALUE_UNDEFINED);
    if (useIntermediateCache) {
        cachedResultsList = cacheTable->FindCachedResult(thread, pattern, flagsBits, string,
                                                         RegExpExecResultCache::INTERMEDIATE_REPLACE_TYPE,
                                                         thisObj, JSTaggedValue(0), JSTaggedValue::Undefined(),
                                                         true);
    }
    if (!cachedResultsList.IsUndefined()) {
        resultsList.Update(cachedResultsList);
        resultsIndex = static_cast<int>(JSArray::Cast(resultsList.GetTaggedValue())->GetArrayLength());
    } else {
        // 12. Let done be false.
        // 13. Repeat, while done is false
        for (;;) {
            // a. Let result be RegExpExec(rx, S).
            execResult.Update(RegExpExec(thread, thisObj, inputStr, useCache));
            // b. ReturnIfAbrupt(result).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            // c. If result is null, set done to true.
            if (execResult->IsNull()) {
                break;
            }
            // d. Else result is not null, i. Append result to the end of results.
            JSObject::CreateDataProperty(thread, resultsList, resultsIndex, execResult);
            resultsIndex++;
            // ii. If global is false, set done to true.
            if (!isGlobal) {
                break;
            }
            // iii. Else, 1. Let matchStr be ToString(Get(result, "0")).
            JSTaggedValue getMatchVal = ObjectFastOperator::FastGetPropertyByValue(
                thread, execResult.GetTaggedValue(), matchedStr.GetTaggedValue());
            JSHandle<JSTaggedValue> getMatch(thread, getMatchVal);
            JSHandle<EcmaString> matchString = JSTaggedValue::ToString(thread, getMatch);
            // 2. ReturnIfAbrupt(matchStr).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            // 3. If matchStr is the empty String, then
            if (EcmaStringAccessor(matchString).GetLength() == 0) {
                // a. Let thisIndex be ToLength(Get(rx, "lastIndex")).
                JSTaggedValue thisIndexVal = ObjectFastOperator::FastGetPropertyByValue(
                    thread, thisObj.GetTaggedValue(), lastIndex.GetTaggedValue());
                JSHandle<JSTaggedValue> thisIndexHandle(thread, thisIndexVal);
                uint32_t thisIndex = 0;
                if (thisIndexHandle->IsInt()) {
                    thisIndex = static_cast<uint32_t>(thisIndexHandle->GetInt());
                } else {
                    thisIndex = JSTaggedValue::ToLength(thread, thisIndexHandle).GetNumber();
                    // b. ReturnIfAbrupt(thisIndex).
                    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                }
                // c. Let nextIndex be AdvanceStringIndex(S, thisIndex, fullUnicode).
                uint32_t nextIndex = static_cast<uint32_t>(AdvanceStringIndex(inputStr, thisIndex, fullUnicode));
                nextIndexHandle.Update(JSTaggedValue(nextIndex));
                // d. Let setStatus be Set(rx, "lastIndex", nextIndex, true).
                ObjectFastOperator::FastSetPropertyByValue(thread, thisObj.GetTaggedValue(), lastIndex.GetTaggedValue(),
                                                           nextIndexHandle.GetTaggedValue());
                // e. ReturnIfAbrupt(setStatus).
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            }
        }
        if (useIntermediateCache) {
            RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flagsBits, string,
                                                    JSHandle<JSTaggedValue>(resultsList),
                                                    RegExpExecResultCache::INTERMEDIATE_REPLACE_TYPE, 0, 0,
                                                    JSTaggedValue::Undefined(), true);
        }
    }
    // 14. Let accumulatedResult be the empty String value.
    std::string accumulatedResult;
    // 15. Let nextSourcePosition be 0.
    uint32_t nextSourcePosition = 0;
    JSMutableHandle<JSTaggedValue> getMatchString(thread, JSTaggedValue::Undefined());
    JSMutableHandle<JSTaggedValue> resultValues(thread, JSTaggedValue(0));
    JSMutableHandle<JSTaggedValue> ncapturesHandle(thread, JSTaggedValue(0));
    JSMutableHandle<JSTaggedValue> capN(thread, JSTaggedValue(0));
    // 16. Repeat, for each result in results,
    for (int i = 0; i < resultsIndex; i++) {
        resultValues.Update(ObjectFastOperator::FastGetPropertyByIndex(thread, resultsList.GetTaggedValue(), i));
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        // a. Let nCaptures be ToLength(Get(result, "length")).
        uint32_t ncaptures;
        if (unmodified) {
            ncaptures = static_cast<uint32_t>(JSArray::Cast(resultValues.GetTaggedValue())->GetArrayLength());
        } else {
            JSHandle<JSTaggedValue> lengthHandle = globalConst->GetHandledLengthString();
            ncapturesHandle.Update(ObjectFastOperator::FastGetPropertyByValue(
                thread, resultValues.GetTaggedValue(), lengthHandle.GetTaggedValue()));
            ncaptures = JSTaggedValue::ToUint32(thread, ncapturesHandle);
        }
        // b. ReturnIfAbrupt(nCaptures).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        // c. Let nCaptures be max(nCaptures − 1, 0).
        ncaptures = std::max<uint32_t>((ncaptures - 1), 0);
        // d. Let matched be ToString(Get(result, "0")).
        JSTaggedValue value = ObjectFastOperator::GetPropertyByIndex(thread, resultValues.GetTaggedValue(), 0);
        getMatchString.Update(value);
        JSHandle<EcmaString> matchString = JSTaggedValue::ToString(thread, getMatchString);
        // e. ReturnIfAbrupt(matched).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        // f. Let matchLength be the number of code units in matched.
        uint32_t matchLength = EcmaStringAccessor(matchString).GetLength();
        // g. Let position be ToInteger(Get(result, "index")).
        JSHandle<JSTaggedValue> resultIndex = globalConst->GetHandledIndexString();
        JSTaggedValue positionTag = ObjectFastOperator::FastGetPropertyByValue(
            thread, resultValues.GetTaggedValue(), resultIndex.GetTaggedValue());
        JSHandle<JSTaggedValue> positionHandle(thread, positionTag);
        uint32_t position = 0;
        if (positionHandle->IsInt()) {
            position = static_cast<uint32_t>(positionHandle->GetInt());
        } else {
            position = JSTaggedValue::ToUint32(thread, positionHandle);
            // h. ReturnIfAbrupt(position).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
        // i. Let position be max(min(position, lengthS), 0).
        position = std::max<uint32_t>(std::min<uint32_t>(position, length), 0);
        // j. Let n be 1.
        uint32_t index = 1;
        // k. Let captures be an empty List.
        JSHandle<TaggedArray> capturesList = factory->NewTaggedArray(ncaptures);
        // l. Repeat while n ≤ nCaptures
        while (index <= ncaptures) {
            // i. Let capN be Get(result, ToString(n)).
            capN.Update(ObjectFastOperator::FastGetPropertyByIndex(thread, resultValues.GetTaggedValue(), index));
            // ii. ReturnIfAbrupt(capN).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            // iii. If capN is not undefined, then
            if (!capN->IsUndefined()) {
                // 1. Let capN be ToString(capN).
                JSHandle<EcmaString> capNStr = JSTaggedValue::ToString(thread, capN);
                // 2. ReturnIfAbrupt(capN).
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                JSHandle<JSTaggedValue> capnStr = JSHandle<JSTaggedValue>::Cast(capNStr);
                capturesList->Set(thread, index - 1, capnStr);
            } else {
                // iv. Append capN as the last element of captures.
                capturesList->Set(thread, index - 1, capN);
            }
            // v. Let n be n+1
            ++index;
        }

        // j. Let namedCaptures be ? Get(result, "groups").
        JSHandle<JSTaggedValue> groupsKey = globalConst->GetHandledGroupsString();
        JSTaggedValue named = ObjectFastOperator::FastGetPropertyByValue(thread,
            resultValues.GetTaggedValue(), groupsKey.GetTaggedValue());
        JSHandle<JSTaggedValue> namedCaptures(thread, named);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        // m. If functionalReplace is true, then
        CString replacement;
        int emptyArrLength = 0;
        if (namedCaptures->IsUndefined()) {
            emptyArrLength = 3; // 3: «matched, pos, and string»
        } else {
            emptyArrLength = 4; // 4: «matched, pos, string, and groups»
        }
        JSHandle<TaggedArray> replacerArgs =
            factory->NewTaggedArray(emptyArrLength + capturesList->GetLength());
        if (functionalReplace) {
            // i. Let replacerArgs be «matched».
            replacerArgs->Set(thread, 0, getMatchString.GetTaggedValue());
            // ii. Append in list order the elements of captures to the end of the List replacerArgs.
            // iii. Append position and S as the last two elements of replacerArgs.
            index = 0;
            while (index < capturesList->GetLength()) {
                replacerArgs->Set(thread, index + 1, capturesList->Get(index));
                ++index;
            }
            replacerArgs->Set(thread, index + 1, JSTaggedValue(position));
            replacerArgs->Set(thread, index + 2, inputStr.GetTaggedValue());  // 2: position of string
            if (!namedCaptures->IsUndefined()) {
                replacerArgs->Set(thread, index + 3, namedCaptures.GetTaggedValue()); // 3: position of groups
            }
            // iv. Let replValue be Call(replaceValue, undefined, replacerArgs).
            const uint32_t argsLength = replacerArgs->GetLength();
            JSHandle<JSTaggedValue> undefined = globalConst->GetHandledUndefined();
            EcmaRuntimeCallInfo *info =
                EcmaInterpreter::NewRuntimeCallInfo(thread, inputReplaceValue, undefined, undefined, argsLength);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            info->SetCallArg(argsLength, replacerArgs);
            JSTaggedValue replaceResult = JSFunction::Call(info);
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            JSHandle<JSTaggedValue> replValue(thread, replaceResult);
            // v. Let replacement be ToString(replValue).
            JSHandle<EcmaString> replacementString = JSTaggedValue::ToString(thread, replValue);
            // o. ReturnIfAbrupt(replacement).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            replacement = ConvertToString(*replacementString, StringConvertedUsage::LOGICOPERATION);
        } else {
            // n. Else,
            if (!namedCaptures->IsUndefined()) {
                JSHandle<JSObject> namedCapturesObj = JSTaggedValue::ToObject(thread, namedCaptures);
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                namedCaptures = JSHandle<JSTaggedValue>::Cast(namedCapturesObj);
            }
            JSHandle<JSTaggedValue> replacementHandle(
                thread, BuiltinsString::GetSubstitution(thread, matchString, srcString,
                                                        position, capturesList, namedCaptures,
                                                        replaceValueHandle));
            replacement = ConvertToString(EcmaString::Cast(replacementHandle->GetTaggedObject()),
                                          StringConvertedUsage::LOGICOPERATION);
        }
        // p. If position ≥ nextSourcePosition, then
        if (position >= nextSourcePosition) {
            // ii. Let accumulatedResult be the String formed by concatenating the code units of the current value
            // of accumulatedResult with the substring of S consisting of the code units from nextSourcePosition
            // (inclusive) up to position (exclusive) and with the code units of replacement.
            auto substr = EcmaStringAccessor::FastSubString(thread->GetEcmaVM(),
                JSHandle<EcmaString>::Cast(inputStr), nextSourcePosition, position - nextSourcePosition);
            accumulatedResult += EcmaStringAccessor(substr).ToStdString(StringConvertedUsage::LOGICOPERATION);
            accumulatedResult += replacement;
            // iii. Let nextSourcePosition be position + matchLength.
            nextSourcePosition = position + matchLength;
        }
    }
    // 17. If nextSourcePosition ≥ lengthS, return accumulatedResult.
    if (nextSourcePosition >= length) {
        JSHandle<EcmaString> resultValue = factory->NewFromStdString(accumulatedResult);
        if (useCache) {
            RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flagsBits, string,
                                                    JSHandle<JSTaggedValue>(resultValue),
                                                    RegExpExecResultCache::REPLACE_TYPE, 0, nextIndexHandle->GetInt(),
                                                    inputReplaceValue.GetTaggedValue());
        }
        return resultValue.GetTaggedValue();
    }
    // 18. Return the String formed by concatenating the code units of accumulatedResult with the substring of S
    // consisting of the code units from nextSourcePosition (inclusive) up through the final code unit of S(inclusive).
    auto substr = EcmaStringAccessor::FastSubString(thread->GetEcmaVM(),
        JSHandle<EcmaString>::Cast(inputStr), nextSourcePosition, length - nextSourcePosition);
    accumulatedResult += EcmaStringAccessor(substr).ToStdString(StringConvertedUsage::LOGICOPERATION);
    JSHandle<EcmaString> resultValue = factory->NewFromStdString(accumulatedResult);
    if (useCache) {
        RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flagsBits, string,
                                                JSHandle<JSTaggedValue>(resultValue),
                                                RegExpExecResultCache::REPLACE_TYPE, 0, nextIndexHandle->GetInt(),
                                                inputReplaceValue.GetTaggedValue());
    }
    return resultValue.GetTaggedValue();
}

// 21.2.5.9
JSTaggedValue BuiltinsRegExp::Search(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), RegExp, Search);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    // 1. Let rx be the this value.
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    // 3. Let S be ToString(string).
    JSHandle<JSTaggedValue> inputStr = GetCallArg(argv, 0);
    JSHandle<EcmaString> stringHandle = JSTaggedValue::ToString(thread, inputStr);

    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSTaggedValue> string = JSHandle<JSTaggedValue>::Cast(stringHandle);
    if (!thisObj->IsECMAObject()) {
        // 2. If Type(rx) is not Object, throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue::Exception());
    }
    // 4. Let previousLastIndex be ? Get(rx, "lastIndex").
    JSHandle<JSTaggedValue> lastIndexString(thread->GlobalConstants()->GetHandledLastIndexString());
    JSHandle<JSTaggedValue> previousLastIndex = JSObject::GetProperty(thread, thisObj, lastIndexString).GetValue();
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 5. If SameValue(previousLastIndex, 0) is false, then
    // Perform ? Set(rx, "lastIndex", 0, true).
    if (!JSTaggedValue::SameValue(previousLastIndex.GetTaggedValue(), JSTaggedValue(0))) {
        JSHandle<JSTaggedValue> value(thread, JSTaggedValue(0));
        JSObject::SetProperty(thread, thisObj, lastIndexString, value, true);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    }
    // 6. Let result be ? RegExpExec(rx, S).
    JSHandle<JSTaggedValue> result(thread, RegExpExec(thread, thisObj, string, false));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 7. Let currentLastIndex be ? Get(rx, "lastIndex").
    JSHandle<JSTaggedValue> currentLastIndex = JSObject::GetProperty(thread, thisObj, lastIndexString).GetValue();
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 8. If SameValue(currentLastIndex, previousLastIndex) is false, then
    // Perform ? Set(rx, "lastIndex", previousLastIndex, true).
    if (!JSTaggedValue::SameValue(previousLastIndex.GetTaggedValue(), currentLastIndex.GetTaggedValue())) {
        JSObject::SetProperty(thread, thisObj, lastIndexString, previousLastIndex, true);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    }
    // 9. If result is null, return -1.
    if (result->IsNull()) {
        return JSTaggedValue(-1);
    }
    // 10. Return ? Get(result, "index").
    JSHandle<JSTaggedValue> index(thread->GlobalConstants()->GetHandledIndexString());
    return JSObject::GetProperty(thread, result, index).GetValue().GetTaggedValue();
}

// 21.2.5.11
// NOLINTNEXTLINE(readability-function-size)
JSTaggedValue BuiltinsRegExp::Split(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), RegExp, Split);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    bool useCache = false;
    // 1. Let rx be the this value.
    JSHandle<JSTaggedValue> thisObj = GetThis(argv);
    auto ecmaVm = thread->GetEcmaVM();
    // 3. Let S be ToString(string).
    JSHandle<JSTaggedValue> inputString = GetCallArg(argv, 0);
    JSHandle<JSTaggedValue> limit = GetCallArg(argv, 1);
    JSHandle<EcmaString> stringHandle = JSTaggedValue::ToString(thread, inputString);

    // 4. ReturnIfAbrupt(string).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<JSTaggedValue> jsString = JSHandle<JSTaggedValue>::Cast(stringHandle);
    if (!thisObj->IsECMAObject()) {
        // 2. If Type(rx) is not Object, throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue::Exception());
    }
    if (IsFastRegExp(thread, thisObj)) {
        JSHandle<JSRegExp> regexpObj(thisObj);
        JSMutableHandle<JSTaggedValue> flags(thread, regexpObj->GetOriginalFlags());
        uint8_t flagsBits = static_cast<uint8_t>(flags->GetInt());
        bool sticky = (flagsBits & RegExpParser::FLAG_STICKY) != 0;
        if (!sticky) {
            if (limit->IsUndefined()) {
                useCache = true;
                return RegExpSplitFast(thread, thisObj, stringHandle, MAX_SPLIT_LIMIT, useCache);
            } else if (limit->IsInt()) {
                int64_t lim = limit->GetInt();
                if (lim >= 0) {
                    return RegExpSplitFast(thread, thisObj, stringHandle, static_cast<uint32_t>(lim), useCache);
                }
            }
        }
    }
    // 5. Let C be SpeciesConstructor(rx, %RegExp%).
    JSHandle<JSTaggedValue> defaultConstructor = ecmaVm->GetGlobalEnv()->GetRegExpFunction();
    JSHandle<JSObject> objHandle(thisObj);
    JSHandle<JSTaggedValue> constructor = JSObject::SpeciesConstructor(thread, objHandle, defaultConstructor);
    // 6. ReturnIfAbrupt(C).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 7. Let flags be ToString(Get(rx, "flags")).
    ObjectFactory *factory = ecmaVm->GetFactory();
    const GlobalEnvConstants *globalConstants = thread->GlobalConstants();
    JSHandle<JSTaggedValue> flagsString(globalConstants->GetHandledFlagsString());
    JSHandle<JSTaggedValue> taggedFlags = JSObject::GetProperty(thread, thisObj, flagsString).GetValue();
    JSHandle<EcmaString> flags;

    if (taggedFlags->IsUndefined()) {
        flags = factory->GetEmptyString();
    } else {
        flags = JSTaggedValue::ToString(thread, taggedFlags);
    }
    //  8. ReturnIfAbrupt(flags).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 9. If flags contains "u", let unicodeMatching be true.
    // 10. Else, let unicodeMatching be false.
    JSHandle<EcmaString> uStringHandle(globalConstants->GetHandledUString());
    bool unicodeMatching = (EcmaStringAccessor::IndexOf(ecmaVm, flags, uStringHandle) != -1);
    // 11. If flags contains "y", let newFlags be flags.
    JSHandle<EcmaString> newFlagsHandle;
    JSHandle<EcmaString> yStringHandle = JSHandle<EcmaString>::Cast(globalConstants->GetHandledYString());
    if (EcmaStringAccessor::IndexOf(ecmaVm, flags, yStringHandle) != -1) {
        newFlagsHandle = flags;
    } else {
        // 12. Else, let newFlags be the string that is the concatenation of flags and "y".
        JSHandle<EcmaString> yStr = JSHandle<EcmaString>::Cast(globalConstants->GetHandledYString());
        newFlagsHandle = factory->ConcatFromString(flags, yStr);
    }

    // 17. If limit is undefined, let lim be 2^32–1; else let lim be ToUint32(limit).
    uint32_t lim;
    if (limit->IsUndefined()) {
        lim = MAX_SPLIT_LIMIT;
    } else {
        lim = JSTaggedValue::ToUint32(thread, limit);
        // 18. ReturnIfAbrupt(lim).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    }

    if (lim == MAX_SPLIT_LIMIT) {
        useCache = true;
    }

    JSHandle<JSRegExp> regexpHandle(thisObj);
    JSMutableHandle<JSTaggedValue> pattern(thread, JSTaggedValue::Undefined());
    JSMutableHandle<JSTaggedValue> flagsBits(thread, JSTaggedValue::Undefined());
    if (thisObj->IsJSRegExp()) {
        pattern.Update(regexpHandle->GetOriginalSource());
        flagsBits.Update(regexpHandle->GetOriginalFlags());
    }
    JSHandle<RegExpExecResultCache> cacheTable(thread->GetCurrentEcmaContext()->GetRegExpCache());
    if (useCache) {
        JSTaggedValue cacheResult = cacheTable->FindCachedResult(thread, pattern, flagsBits, inputString,
                                                                 RegExpExecResultCache::SPLIT_TYPE, thisObj,
                                                                 JSTaggedValue(0));
        if (!cacheResult.IsUndefined()) {
            return cacheResult;
        }
    }

    // 13. Let splitter be Construct(C, «rx, newFlags»).
    JSHandle<JSObject> globalObject(thread, thread->GetEcmaVM()->GetGlobalEnv()->GetGlobalObject());
    JSHandle<JSTaggedValue> undefined = globalConstants->GetHandledUndefined();
    EcmaRuntimeCallInfo *runtimeInfo =
        EcmaInterpreter::NewRuntimeCallInfo(thread, constructor, undefined, undefined, 2); // 2: two args
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    runtimeInfo->SetCallArg(thisObj.GetTaggedValue(), newFlagsHandle.GetTaggedValue());
    JSTaggedValue taggedSplitter = JSFunction::Construct(runtimeInfo);
    // 14. ReturnIfAbrupt(splitter).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);

    JSHandle<JSTaggedValue> splitter(thread, taggedSplitter);
    // 15. Let A be ArrayCreate(0).
    JSHandle<JSObject> array(JSArray::ArrayCreate(thread, JSTaggedNumber(0)));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 16. Let lengthA be 0.
    uint32_t aLength = 0;

    // 19. Let size be the number of elements in S.
    uint32_t size = EcmaStringAccessor(jsString->GetTaggedObject()).GetLength();
    // 20. Let p be 0.
    uint32_t startIndex = 0;
    // 21. If lim = 0, return A.
    if (lim == 0) {
        return JSTaggedValue(static_cast<JSArray *>(array.GetTaggedValue().GetTaggedObject()));
    }
    // 22. If size = 0, then
    if (size == 0) {
        // a. Let z be RegExpExec(splitter, S).
        JSHandle<JSTaggedValue> execResult(thread, RegExpExec(thread, splitter, jsString, useCache));
        // b. ReturnIfAbrupt(z).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        // c. If z is not null, return A.
        if (!execResult->IsNull()) {
            return JSTaggedValue(static_cast<JSArray *>(array.GetTaggedValue().GetTaggedObject()));
        }
        // d. Assert: The following call will never result in an abrupt completion.
        // e. Perform CreateDataProperty(A, "0", S).
        JSObject::CreateDataProperty(thread, array, 0, jsString);
        // f. Return A.
        return JSTaggedValue(static_cast<JSArray *>(array.GetTaggedValue().GetTaggedObject()));
    }
    // 23. Let q be p.
    uint32_t endIndex = startIndex;
    JSMutableHandle<JSTaggedValue> lastIndexvalue(thread, JSTaggedValue(endIndex));
    // 24. Repeat, while q < size
    JSHandle<JSTaggedValue> lastIndexString = globalConstants->GetHandledLastIndexString();
    while (endIndex < size) {
        // a. Let setStatus be Set(splitter, "lastIndex", q, true).
        lastIndexvalue.Update(JSTaggedValue(endIndex));
        JSObject::SetProperty(thread, splitter, lastIndexString, lastIndexvalue, true);
        // b. ReturnIfAbrupt(setStatus).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        JSHandle<JSTaggedValue> execResult(thread, RegExpExec(thread, splitter, jsString, useCache));
        // d. ReturnIfAbrupt(z).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        // e. If z is null, let q be AdvanceStringIndex(S, q, unicodeMatching).
        if (execResult->IsNull()) {
            endIndex = static_cast<uint32_t>(AdvanceStringIndex(jsString, endIndex, unicodeMatching));
        } else {
            // f. Else z is not null,
            // i. Let e be ToLength(Get(splitter, "lastIndex")).
            JSHandle<JSTaggedValue> lastIndexHandle =
                JSObject::GetProperty(thread, splitter, lastIndexString).GetValue();
            JSTaggedNumber lastIndexNumber = JSTaggedValue::ToLength(thread, lastIndexHandle);
            // ii. ReturnIfAbrupt(e).
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
            uint32_t lastIndex = lastIndexNumber.GetNumber();
            // iii. If e = p, let q be AdvanceStringIndex(S, q, unicodeMatching).
            if (lastIndex == startIndex) {
                endIndex = static_cast<uint32_t>(AdvanceStringIndex(jsString, endIndex, unicodeMatching));
            } else {
                // iv. Else e != p,
                // 1. Let T be a String value equal to the substring of S consisting of the elements at indices p
                // (inclusive) through q (exclusive).
                auto substr = EcmaStringAccessor::FastSubString(thread->GetEcmaVM(),
                    JSHandle<EcmaString>::Cast(jsString), startIndex, endIndex - startIndex);
                std::string stdStrT = EcmaStringAccessor(substr).ToStdString(StringConvertedUsage::LOGICOPERATION);
                // 2. Assert: The following call will never result in an abrupt completion.
                // 3. Perform CreateDataProperty(A, ToString(lengthA), T).
                JSHandle<JSTaggedValue> tValue(factory->NewFromStdString(stdStrT));
                JSObject::CreateDataProperty(thread, array, aLength, tValue);
                // 4. Let lengthA be lengthA +1.
                ++aLength;
                // 5. If lengthA = lim, return A.
                if (aLength == lim) {
                    if (useCache) {
                        RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flagsBits, inputString,
                                                                JSHandle<JSTaggedValue>(array),
                                                                RegExpExecResultCache::SPLIT_TYPE, 0, lastIndex);
                    }
                    return array.GetTaggedValue();
                }
                // 6. Let p be e.
                startIndex = lastIndex;
                // 7. Let numberOfCaptures be ToLength(Get(z, "length")).
                JSHandle<JSTaggedValue> lengthString(thread->GlobalConstants()->GetHandledLengthString());
                JSHandle<JSTaggedValue> capturesHandle =
                    JSObject::GetProperty(thread, execResult, lengthString).GetValue();
                JSTaggedNumber numberOfCapturesNumber = JSTaggedValue::ToLength(thread, capturesHandle);
                // 8. ReturnIfAbrupt(numberOfCaptures).
                RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                uint32_t numberOfCaptures = numberOfCapturesNumber.GetNumber();
                // 9. Let numberOfCaptures be max(numberOfCaptures-1, 0).
                numberOfCaptures = (numberOfCaptures == 0) ? 0 : numberOfCaptures - 1;
                // 10. Let i be 1.
                uint32_t i = 1;
                // 11. Repeat, while i ≤ numberOfCaptures.
                while (i <= numberOfCaptures) {
                    // a. Let nextCapture be Get(z, ToString(i)).
                    JSHandle<JSTaggedValue> nextCapture = JSObject::GetProperty(thread, execResult, i).GetValue();
                    // b. ReturnIfAbrupt(nextCapture).
                    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
                    // c. Perform CreateDataProperty(A, ToString(lengthA), nextCapture).
                    JSObject::CreateDataProperty(thread, array, aLength, nextCapture);
                    // d. Let i be i + 1.
                    ++i;
                    // e. Let lengthA be lengthA +1.
                    ++aLength;
                    // f. If lengthA = lim, return A.
                    if (aLength == lim) {
                        if (useCache) {
                            RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flagsBits, inputString,
                                                                    JSHandle<JSTaggedValue>(array),
                                                                    RegExpExecResultCache::SPLIT_TYPE, 0, lastIndex);
                        }
                        return array.GetTaggedValue();
                    }
                }
                // 12. Let q be p.
                endIndex = startIndex;
            }
        }
    }
    // 25. Let T be a String value equal to the substring of S consisting of the elements at indices p (inclusive)
    // through size (exclusive).
    auto substr = EcmaStringAccessor::FastSubString(thread->GetEcmaVM(),
        JSHandle<EcmaString>::Cast(jsString), startIndex, size - startIndex);
    std::string stdStrT = EcmaStringAccessor(substr).ToStdString(StringConvertedUsage::LOGICOPERATION);
    // 26. Assert: The following call will never result in an abrupt completion.
    // 27. Perform CreateDataProperty(A, ToString(lengthA), t).
    JSHandle<JSTaggedValue> tValue(factory->NewFromStdString(stdStrT));
    JSObject::CreateDataProperty(thread, array, aLength, tValue);
    if (lim == MAX_SPLIT_LIMIT) {
        RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flagsBits, inputString,
                                                JSHandle<JSTaggedValue>(array), RegExpExecResultCache::SPLIT_TYPE,
                                                0, endIndex);
    }
    // 28. Return A.
    return array.GetTaggedValue();
}

JSTaggedValue BuiltinsRegExp::RegExpSplitFast(JSThread *thread, const JSHandle<JSTaggedValue> &regexp,
                                              JSHandle<EcmaString> string, uint32_t limit, bool useCache)
{
    JSHandle<JSTaggedValue> jsString = JSHandle<JSTaggedValue>::Cast(string);
    if (limit == 0) {
        return JSArray::ArrayCreate(thread, JSTaggedNumber(0), ArrayMode::LITERAL).GetTaggedValue();
    }
    uint32_t size = EcmaStringAccessor(jsString->GetTaggedObject()).GetLength();
    if (size == 0) {
        bool matchResult = RegExpExecInternal(thread, regexp, string, 0); // 0: lastIndex
        if (matchResult) {
            return JSArray::ArrayCreate(thread, JSTaggedNumber(0), ArrayMode::LITERAL).GetTaggedValue();
        }
        JSHandle<TaggedArray> element = thread->GetEcmaVM()->GetFactory()->NewTaggedArray(1); // 1: length
        element->Set(thread, 0, jsString);
        return JSArray::CreateArrayFromList(thread, element).GetTaggedValue();
    }
    JSHandle<JSRegExp> regexpObj(regexp);
    JSMutableHandle<JSTaggedValue> pattern(thread, regexpObj->GetOriginalSource());
    JSMutableHandle<JSTaggedValue> flags(thread, regexpObj->GetOriginalFlags());
    uint8_t flagsBits = static_cast<uint8_t>(flags->GetInt());
    bool isUnicode = (flagsBits & RegExpParser::FLAG_UTF16) != 0;

    JSHandle<RegExpExecResultCache> cacheTable(thread->GetCurrentEcmaContext()->GetRegExpCache());
    if (useCache) {
        JSTaggedValue cacheResult = cacheTable->FindCachedResult(thread, pattern, flags, jsString,
                                                                 RegExpExecResultCache::SPLIT_TYPE, regexp,
                                                                 JSTaggedValue(0));
        if (!cacheResult.IsUndefined()) {
            return cacheResult;
        }
    }

    uint32_t nextMatchFrom = 0;
    uint32_t lastMatchEnd = 0;
    uint32_t arrLen = 1; // at least one result string
    JSHandle<JSArray> splitArray(JSArray::ArrayCreate(thread, JSTaggedNumber(1), ArrayMode::LITERAL));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    TaggedArray *srcElements = TaggedArray::Cast(splitArray->GetElements().GetTaggedObject());
    JSMutableHandle<TaggedArray> elements(thread, srcElements);
    JSMutableHandle<JSTaggedValue> matchValue(thread, JSTaggedValue::Undefined());
    while (nextMatchFrom < size) {
        bool matchResult = RegExpExecInternal(thread, regexp, string, nextMatchFrom);
        if (!matchResult) {
            // done match
            break;
        }
        // find match result
        JSHandle<RegExpGlobalResult> matchResultInfo(thread->GetCurrentEcmaContext()->GetRegExpGlobalResult());
        uint32_t matchStartIndex = static_cast<uint32_t>(matchResultInfo->GetStartOfCaptureIndex(0).GetInt());
        uint32_t matchEndIndex = static_cast<uint32_t>(matchResultInfo->GetEndOfCaptureIndex(0).GetInt());
        if (matchEndIndex == lastMatchEnd && matchEndIndex == nextMatchFrom) {
            // advance index and continue if match result is empty.
            nextMatchFrom = static_cast<uint32_t>(AdvanceStringIndex(jsString, nextMatchFrom, isUnicode));
        } else {
            matchValue.Update(JSTaggedValue(EcmaStringAccessor::FastSubString(thread->GetEcmaVM(),
                string, lastMatchEnd, matchStartIndex - lastMatchEnd)));
            if (arrLen > elements->GetLength()) {
                elements.Update(JSObject::GrowElementsCapacity(thread,
                    JSHandle<JSObject>::Cast(splitArray), elements->GetLength(), true));
            }
            elements->Set(thread, arrLen - 1, matchValue);
            splitArray->SetArrayLength(thread, arrLen);
            if (arrLen == limit) {
                if (useCache) {
                    RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flags, jsString,
                                                            JSHandle<JSTaggedValue>(splitArray),
                                                            RegExpExecResultCache::SPLIT_TYPE, 0, matchEndIndex);
                }
                return JSTaggedValue(splitArray.GetTaggedValue().GetTaggedObject());
            }
            arrLen++;
            uint32_t capturesSize = static_cast<uint32_t>(matchResultInfo->GetTotalCaptureCounts().GetInt());
            uint32_t captureIndex = 1;
            while (captureIndex < capturesSize) {
                uint32_t captureStartIndex = static_cast<uint32_t>(
                    matchResultInfo->GetStartOfCaptureIndex(captureIndex).GetInt());
                uint32_t captureEndIndex = static_cast<uint32_t>(
                    matchResultInfo->GetEndOfCaptureIndex(captureIndex).GetInt());
                int32_t subStrLen = static_cast<int32_t>(captureEndIndex - captureStartIndex);
                if (subStrLen < 0) {
                    matchValue.Update(JSTaggedValue::Undefined());
                } else {
                    matchValue.Update(JSTaggedValue(EcmaStringAccessor::FastSubString(thread->GetEcmaVM(),
                        string, captureStartIndex, subStrLen)));
                }
                if (arrLen > elements->GetLength()) {
                    elements.Update(JSObject::GrowElementsCapacity(thread,
                        JSHandle<JSObject>::Cast(splitArray), arrLen, true));
                }
                elements->Set(thread, arrLen - 1, matchValue);
                splitArray->SetArrayLength(thread, arrLen);
                if (arrLen == limit) {
                    if (useCache) {
                        RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flags, jsString,
                                                                JSHandle<JSTaggedValue>(splitArray),
                                                                RegExpExecResultCache::SPLIT_TYPE, 0, matchEndIndex);
                    }
                    return JSTaggedValue(splitArray.GetTaggedValue().GetTaggedObject());
                }
                arrLen++;
                captureIndex++;
            }
            lastMatchEnd = matchEndIndex;
            nextMatchFrom = matchEndIndex;
        }
    }
    matchValue.Update(JSTaggedValue(EcmaStringAccessor::FastSubString(thread->GetEcmaVM(),
        JSHandle<EcmaString>::Cast(jsString), lastMatchEnd, size - lastMatchEnd)));
    if (arrLen > elements->GetLength()) {
        elements.Update(JSObject::GrowElementsCapacity(thread, JSHandle<JSObject>::Cast(splitArray), arrLen, true));
    }
    elements->Set(thread, arrLen - 1, matchValue);
    splitArray->SetArrayLength(thread, arrLen);
    if (limit == MAX_SPLIT_LIMIT) {
        RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flags, jsString,
                                                JSHandle<JSTaggedValue>(splitArray), RegExpExecResultCache::SPLIT_TYPE,
                                                0, lastMatchEnd);
    }
    return JSTaggedValue(splitArray.GetTaggedValue().GetTaggedObject());
}

bool BuiltinsRegExp::RegExpExecInternal(JSThread *thread, const JSHandle<JSTaggedValue> &regexp,
                                        JSHandle<EcmaString> &inputString, int32_t lastIndex)
{
    size_t stringLength = EcmaStringAccessor(inputString).GetLength();
    bool isUtf16 = EcmaStringAccessor(inputString).IsUtf16();
    FlatStringInfo flatStrInfo = EcmaStringAccessor::FlattenAllString(thread->GetEcmaVM(), inputString);
    if (EcmaStringAccessor(inputString).IsTreeString()) { // use flattenedString as srcString
        inputString = JSHandle<EcmaString>(thread, flatStrInfo.GetString());
    }
    const uint8_t *strBuffer;
    if (isUtf16) {
        strBuffer = reinterpret_cast<const uint8_t *>(flatStrInfo.GetDataUtf16());
    } else {
        strBuffer = flatStrInfo.GetDataUtf8();
    }
    bool isSuccess = Matcher(thread, regexp, strBuffer, stringLength, lastIndex, isUtf16);
    if (isSuccess) {
        JSHandle<RegExpGlobalResult> globalTable(thread->GetCurrentEcmaContext()->GetRegExpGlobalResult());
        globalTable->ResetDollar(thread);
        globalTable->SetInputString(thread, inputString.GetTaggedValue());
    }
    return isSuccess;
}

// NOLINTNEXTLINE(readability-non-const-parameter)
bool BuiltinsRegExp::Matcher(JSThread *thread, const JSHandle<JSTaggedValue> &regexp,
                             const uint8_t *buffer, size_t length, int32_t lastIndex,
                             bool isUtf16)
{
    BUILTINS_API_TRACE(thread, RegExp, Matcher);
    // get bytecode
    JSTaggedValue bufferData = JSRegExp::Cast(regexp->GetTaggedObject())->GetByteCodeBuffer();
    void *dynBuf = JSNativePointer::Cast(bufferData.GetTaggedObject())->GetExternalPointer();
    auto bytecodeBuffer = reinterpret_cast<uint8_t *>(dynBuf);
    // execute
    RegExpCachedChunk chunk(thread);
    RegExpExecutor executor(&chunk);
    if (lastIndex < 0) {
        lastIndex = 0;
    }
    bool ret = executor.Execute(buffer, lastIndex, static_cast<uint32_t>(length), bytecodeBuffer, isUtf16);
    if (ret) {
        executor.GetResult(thread);
    }
    return ret;
}

int64_t BuiltinsRegExp::AdvanceStringIndex(const JSHandle<JSTaggedValue> &inputStr, int64_t index,
                                           bool unicode)
{
    // 1. Assert: Type(S) is String.
    ASSERT(inputStr->IsString());
    // 2. Assert: index is an integer such that 0≤index≤2^53 - 1
    ASSERT(0 <= index && index <= pow(2, 53) - 1);
    // 3. Assert: Type(unicode) is Boolean.
    // 4. If unicode is false, return index+1.
    if (!unicode) {
        return index + 1;
    }
    // 5. Let length be the number of code units in S.
    uint32_t length = EcmaStringAccessor(inputStr->GetTaggedObject()).GetLength();
    // 6. If index+1 ≥ length, return index+1.
    if (index + 1 >= length) {
        return index + 1;
    }
    // 7. Let first be the code unit value at index index in S.
    uint16_t first = EcmaStringAccessor(inputStr->GetTaggedObject()).Get(index);
    // 8. If first < 0xD800 or first > 0xDFFF, return index+1.
    if (first < 0xD800 || first > 0xDFFF) {  // NOLINT(readability-magic-numbers)
        return index + 1;
    }
    // 9. Let second be the code unit value at index index+1 in S.
    uint16_t second = EcmaStringAccessor(inputStr->GetTaggedObject()).Get(index + 1);
    // 10. If second < 0xDC00 or second > 0xDFFF, return index+1.
    if (second < 0xDC00 || second > 0xDFFF) {  // NOLINT(readability-magic-numbers)
        return index + 1;
    }
    // 11. Return index + 2.
    return index + 2;
}

JSTaggedValue BuiltinsRegExp::GetFlagsInternal(JSThread *thread, const JSHandle<JSTaggedValue> &obj,
                                               const JSHandle<JSTaggedValue> &constructor, const uint8_t mask)
{
    BUILTINS_API_TRACE(thread, RegExp, GetFlagsInternal);
    // 1. Let R be the this value.
    // 2. If Type(R) is not Object, throw a TypeError exception.
    if (!obj->IsECMAObject()) {
        // throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not Object", JSTaggedValue(false));
    }
    // 3. If R does not have an [[OriginalFlags]] internal slot, throw a TypeError exception.
    JSHandle<JSObject> patternObj = JSHandle<JSObject>::Cast(obj);
    if (!patternObj->IsJSRegExp()) {
        // a. If SameValue(R, %RegExp.prototype%) is true, return undefined.
        const GlobalEnvConstants *globalConst = thread->GlobalConstants();
        JSHandle<JSTaggedValue> constructorKey = globalConst->GetHandledConstructorString();
        JSHandle<JSTaggedValue> objConstructor = JSTaggedValue::GetProperty(thread, obj, constructorKey).GetValue();
        RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, JSTaggedValue(false));
        if (objConstructor->IsJSFunction() && constructor->IsJSFunction()) {
            JSHandle<GlobalEnv> objRealm = JSObject::GetFunctionRealm(thread, objConstructor);
            JSHandle<GlobalEnv> ctorRealm = JSObject::GetFunctionRealm(thread, constructor);
            if (objRealm->GetRegExpPrototype() == obj && *objRealm == *ctorRealm) {
                return JSTaggedValue::Undefined();
            }
        }
        // b. throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this does not have [[OriginalFlags]]", JSTaggedValue(false));
    }
    // 4. Let flags be the value of R’s [[OriginalFlags]] internal slot.
    JSHandle<JSRegExp> regexpObj(thread, JSRegExp::Cast(obj->GetTaggedObject()));
    // 5. If flags contains the code unit "[flag]", return true.
    // 6. Return false.
    uint8_t flags = static_cast<uint8_t>(regexpObj->GetOriginalFlags().GetInt());
    return GetTaggedBoolean(flags & mask);
}

// 22.2.7.8
JSHandle<JSTaggedValue> BuiltinsRegExp::MakeMatchIndicesIndexPairArray(JSThread *thread,
    const std::vector<std::pair<JSTaggedValue, JSTaggedValue>>& indices,
    const std::vector<JSHandle<JSTaggedValue>>& groupNames, bool hasGroups)
{
    // 1. Let n be the number of elements in indices.
    uint32_t n = indices.size();
    // Assert: groupNames has n - 1 elements.
    ASSERT(groupNames.size() == n - 1);
    // 5. Let A be ! ArrayCreate(n).
    JSHandle<JSObject> results(JSArray::ArrayCreate(thread, JSTaggedNumber(n)));
    RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    // 6. If hasGroups is true, then
    //    a. Let groups be OrdinaryObjectCreate(null).
    // 7. Else,
    //    a. Let groups be undefined.
    JSMutableHandle<JSTaggedValue> groups(thread, JSTaggedValue::Undefined());
    if (hasGroups) {
        JSHandle<JSTaggedValue> nullHandle(thread, JSTaggedValue::Null());
        JSHandle<JSObject> nullObj = factory->OrdinaryNewJSObjectCreate(nullHandle);
        groups.Update(nullObj.GetTaggedValue());
    }
    // 8. Perform ! CreateDataPropertyOrThrow(A, "groups", groups).
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    JSHandle<JSTaggedValue> groupsKey = globalConst->GetHandledGroupsString();
    JSObject::CreateDataProperty(thread, results, groupsKey, groups);
    // 9. For each integer i such that 0 ≤ i < n, in ascending order, do
    //    a. Let matchIndices be indices[i].
    //    b. If matchIndices is not undefined, then
    //        i. Let matchIndexPair be GetMatchIndexPair(S, matchIndices).
    //    c. Else,
    //        i. Let matchIndexPair be undefined.
    //    d. Perform ! CreateDataPropertyOrThrow(A, ! ToString(𝔽(i)), matchIndexPair).
    //    e. If i > 0 and groupNames[i - 1] is not undefined, then
    //        i. Assert: groups is not undefined.
    //        ii. Perform ! CreateDataPropertyOrThrow(groups, groupNames[i - 1], matchIndexPair).
    JSMutableHandle<JSTaggedValue> matchIndexPair(thread, JSTaggedValue::Undefined());
    for (uint32_t i = 0; i < n; i++) {
        std::pair<JSTaggedValue, JSTaggedValue> matchIndices = indices[i];
        if (!matchIndices.first.IsUndefined()) {
            JSHandle<TaggedArray> match = factory->NewTaggedArray(2); // 2 means the length of array
            match->Set(thread, 0, matchIndices.first);
            match->Set(thread, 1, matchIndices.second);
            JSHandle<JSTaggedValue> pair(JSArray::CreateArrayFromList(thread, JSHandle<TaggedArray>::Cast(match)));
            matchIndexPair.Update(pair.GetTaggedValue());
        } else {
            matchIndexPair.Update(JSTaggedValue::Undefined());
        }
        JSObject::CreateDataProperty(thread, results, i, matchIndexPair);
        if (i > 0) {
            JSHandle<JSTaggedValue> groupName = groupNames[i - 1];
            if (!groupName->IsUndefined()) {
                JSHandle<JSObject> groupObject = JSHandle<JSObject>::Cast(groups);
                JSObject::CreateDataProperty(thread, groupObject, groupName, matchIndexPair);
            }
        }
    }
    // 10. Return A.
    return JSHandle<JSTaggedValue>::Cast(results);
}

// 21.2.5.2.2
JSTaggedValue BuiltinsRegExp::RegExpBuiltinExec(JSThread *thread, const JSHandle<JSTaggedValue> &regexp,
                                                const JSHandle<JSTaggedValue> &inputStr, bool useCache)
{
    ASSERT(JSObject::IsRegExp(thread, regexp));
    ASSERT(inputStr->IsString());
    BUILTINS_API_TRACE(thread, RegExp, RegExpBuiltinExec);
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    JSHandle<JSTaggedValue> lastIndexHandle = globalConst->GetHandledLastIndexString();
    JSTaggedValue result =
        ObjectFastOperator::FastGetPropertyByValue(thread, regexp.GetTaggedValue(), lastIndexHandle.GetTaggedValue());
    int32_t lastIndex = 0;
    if (result.IsInt()) {
        lastIndex = result.GetInt();
    } else {
        JSHandle<JSTaggedValue> lastIndexResult(thread, result);
        JSTaggedNumber lastIndexNumber = JSTaggedValue::ToLength(thread, lastIndexResult);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        lastIndex = lastIndexNumber.GetNumber();
    }

    JSHandle<JSRegExp> regexpObj(regexp);
    JSMutableHandle<JSTaggedValue> pattern(thread, regexpObj->GetOriginalSource());
    JSMutableHandle<JSTaggedValue> flags(thread, regexpObj->GetOriginalFlags());
    JSHandle<RegExpExecResultCache> cacheTable(thread->GetCurrentEcmaContext()->GetRegExpCache());

    uint8_t flagsBits = static_cast<uint8_t>(flags->GetInt());
    bool global = (flagsBits & RegExpParser::FLAG_GLOBAL) != 0;
    bool sticky = (flagsBits & RegExpParser::FLAG_STICKY) != 0;
    bool hasIndices = (flagsBits & RegExpParser::FLAG_HASINDICES) != 0;
    if (!global && !sticky) {
        lastIndex = 0;
    }
    uint32_t lastIndexInput = static_cast<uint32_t>(lastIndex);
    if (useCache) {
        JSTaggedValue cacheResult = cacheTable->FindCachedResult(thread, pattern, flags, inputStr,
                                                                 RegExpExecResultCache::EXEC_TYPE, regexp,
                                                                 JSTaggedValue(lastIndexInput));
        if (!cacheResult.IsUndefined()) {
            return cacheResult;
        }
    }

    uint32_t length = EcmaStringAccessor(inputStr->GetTaggedObject()).GetLength();
    if (lastIndex > static_cast<int32_t>(length)) {
        ObjectFastOperator::FastSetPropertyByValue(thread, regexp.GetTaggedValue(), lastIndexHandle.GetTaggedValue(),
                                                   JSTaggedValue(0));
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        return JSTaggedValue::Null();
    }
    JSHandle<EcmaString> inputString = JSHandle<EcmaString>::Cast(inputStr);
    bool matchResult = RegExpExecInternal(thread, regexp, inputString, lastIndex);
    if (!matchResult) {
        if (global || sticky) {
            JSHandle<JSTaggedValue> lastIndexValue(thread, JSTaggedValue(0));
            ObjectFastOperator::FastSetPropertyByValue(thread, regexp.GetTaggedValue(),
                                                       lastIndexHandle.GetTaggedValue(),
                                                       JSTaggedValue(0));
            RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        }
        return JSTaggedValue::Null();
    }
    JSHandle<RegExpGlobalResult> globalTable(thread->GetCurrentEcmaContext()->GetRegExpGlobalResult());
    uint32_t endIndex = static_cast<uint32_t>(globalTable->GetEndIndex().GetInt());
    if (global || sticky) {
        // a. Let setStatus be Set(R, "lastIndex", e, true).
        ObjectFastOperator::FastSetPropertyByValue(thread, regexp.GetTaggedValue(), lastIndexHandle.GetTaggedValue(),
                                                   JSTaggedValue(endIndex));
        // b. ReturnIfAbrupt(setStatus).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    }
    uint32_t capturesSize = static_cast<uint32_t>(globalTable->GetTotalCaptureCounts().GetInt());
    JSHandle<JSObject> results(JSArray::ArrayCreate(thread, JSTaggedNumber(capturesSize)));
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 24. Perform CreateDataProperty(A, "index", matchIndex).
    JSHandle<JSTaggedValue> indexKey = globalConst->GetHandledIndexString();
    JSHandle<JSTaggedValue> indexValue(thread, globalTable->GetStartOfCaptureIndex(0));
    JSObject::CreateDataProperty(thread, results, indexKey, indexValue);
    // 25. Perform CreateDataProperty(A, "input", S).
    JSHandle<JSTaggedValue> inputKey = globalConst->GetHandledInputString();
    JSHandle<JSTaggedValue> inputValue(thread, static_cast<EcmaString *>(inputStr->GetTaggedObject()));
    JSObject::CreateDataProperty(thread, results, inputKey, inputValue);

    // 27. Perform CreateDataProperty(A, "0", matched_substr).
    uint32_t startIndex = static_cast<uint32_t>(globalTable->GetStartOfCaptureIndex(0).GetInt());
    uint32_t len = static_cast<uint32_t>(globalTable->GetEndOfCaptureIndex(0).GetInt()) - startIndex;
    JSHandle<JSTaggedValue> zeroValue(thread, JSTaggedValue(EcmaStringAccessor::FastSubString(
        thread->GetEcmaVM(), inputString, startIndex, len)));
    JSObject::CreateDataProperty(thread, results, 0, zeroValue);

    // Let indices be a new empty List.
    // Let groupNames be a new empty List.
    // Append match to indices.
    std::vector<std::pair<JSTaggedValue, JSTaggedValue>> indices;
    std::vector<JSHandle<JSTaggedValue>> groupNames;
    indices.emplace_back(std::make_pair(globalTable->GetStartOfCaptureIndex(0), JSTaggedValue(endIndex)));
    // If R contains any GroupName, then
    //   a. Let groups be OrdinaryObjectCreate(null).
    //   b. Let hasGroups be true.
    // Else,
    //   a. Let groups be undefined.
    //   b. Let hasGroups be false.
    JSHandle<JSTaggedValue> groupName(thread, regexpObj->GetGroupName());
    JSMutableHandle<JSTaggedValue> groups(thread, JSTaggedValue::Undefined());
    bool hasGroups = false;
    if (!groupName->IsUndefined()) {
        ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
        JSHandle<JSTaggedValue> nullHandle(thread, JSTaggedValue::Null());
        JSHandle<JSObject> nullObj = factory->OrdinaryNewJSObjectCreate(nullHandle);
        groups.Update(nullObj.GetTaggedValue());
        hasGroups = true;
    }
    // Perform ! CreateDataPropertyOrThrow(A, "groups", groups).
    JSHandle<JSTaggedValue> groupsKey = globalConst->GetHandledGroupsString();
    JSObject::CreateDataProperty(thread, results, groupsKey, groups);
    // Create a new RegExp on global
    uint32_t captureIndex = 1;
    JSHandle<JSTaggedValue> undefined = globalConst->GetHandledUndefined();
    JSMutableHandle<JSTaggedValue> iValue(thread, JSTaggedValue::Undefined());
    // 28. For each integer i such that i > 0 and i <= n
    for (; captureIndex < capturesSize; captureIndex++) {
        // a. Let capture_i be ith element of r's captures List
        int32_t captureStartIndex = globalTable->GetStartOfCaptureIndex(captureIndex).GetInt();
        int32_t captureEndIndex = globalTable->GetEndOfCaptureIndex(captureIndex).GetInt();
        int32_t subStrLen = captureEndIndex - captureStartIndex;
        if (subStrLen < 0) {
            iValue.Update(JSTaggedValue::Undefined());
            indices.emplace_back(std::make_pair(JSTaggedValue::Undefined(), JSTaggedValue::Undefined()));
        } else {
            iValue.Update(JSTaggedValue(EcmaStringAccessor::FastSubString(
                thread->GetEcmaVM(), inputString, captureStartIndex, subStrLen)));
            indices.emplace_back(std::make_pair(captureStartIndex, captureEndIndex));
        }
        // add to RegExp.$i and i must <= 9
        if (captureIndex <= REGEXP_GLOBAL_ARRAY_SIZE) {
            globalTable->SetCapture(thread, captureIndex, iValue.GetTaggedValue());
        }

        JSObject::CreateDataProperty(thread, results, captureIndex, iValue);
        if (!groupName->IsUndefined()) {
            JSHandle<JSObject> groupObject = JSHandle<JSObject>::Cast(groups);
            TaggedArray *groupArray = TaggedArray::Cast(regexpObj->GetGroupName().GetTaggedObject());
            if (groupArray->GetLength() > captureIndex - 1) {
                JSHandle<JSTaggedValue> skey(thread, groupArray->Get(captureIndex - 1));
                JSObject::CreateDataProperty(thread, groupObject, skey, iValue);
                groupNames.emplace_back(skey);
            } else {
                groupNames.emplace_back(undefined);
            }
        } else {
            groupNames.emplace_back(undefined);
        }
    }
    // If hasIndices is true, then
    //   a. Let indicesArray be MakeMatchIndicesIndexPairArray(S, indices, groupNames, hasGroups).
    //   b. Perform ! CreateDataPropertyOrThrow(A, "indices", indicesArray).
    if (hasIndices) {
        auto indicesArray = MakeMatchIndicesIndexPairArray(thread, indices, groupNames, hasGroups);
        JSHandle<JSTaggedValue> indicesKey = globalConst->GetHandledIndicesString();
        JSObject::CreateDataProperty(thread, results, indicesKey, indicesArray);
    }
    JSHandle<JSTaggedValue> emptyString = thread->GlobalConstants()->GetHandledEmptyString();
    while (captureIndex <= REGEXP_GLOBAL_ARRAY_SIZE) {
        globalTable->SetCapture(thread, captureIndex, emptyString.GetTaggedValue());
        ++captureIndex;
    }
    if (useCache) {
        RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flags, inputStr,
                                                JSHandle<JSTaggedValue>(results), RegExpExecResultCache::EXEC_TYPE,
                                                lastIndexInput, endIndex);
    }
    // 29. Return A.
    return results.GetTaggedValue();
}

// 21.2.5.2.1
JSTaggedValue BuiltinsRegExp::RegExpExec(JSThread *thread, const JSHandle<JSTaggedValue> &regexp,
                                         const JSHandle<JSTaggedValue> &inputString, bool useCache)
{
    BUILTINS_API_TRACE(thread, RegExp, RegExpExec);
    // 1. Assert: Type(R) is Object.
    ASSERT(regexp->IsECMAObject());
    // 2. Assert: Type(S) is String.
    ASSERT(inputString->IsString());
    // 3. Let exec be Get(R, "exec").
    JSHandle<EcmaString> inputStr = JSTaggedValue::ToString(thread, inputString);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    JSHandle<JSTaggedValue> execHandle = globalConst->GetHandledExecString();
    JSTaggedValue execVal = ObjectFastOperator::FastGetPropertyByValue(thread, regexp.GetTaggedValue(),
                                                                       execHandle.GetTaggedValue());
    if (execVal == env->GetTaggedRegExpExecFunction() && regexp->IsJSRegExp()) {
        JSTaggedValue result = RegExpBuiltinExec(thread, regexp, JSHandle<JSTaggedValue>(inputStr), useCache);
        // b. ReturnIfAbrupt(result).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        if (!result.IsECMAObject() && !result.IsNull()) {
            // throw a TypeError exception.
            THROW_TYPE_ERROR_AND_RETURN(thread, "exec result is null or is not Object", JSTaggedValue::Exception());
        }
        return result;
    }

    JSHandle<JSTaggedValue> exec(thread, execVal);
    // 4. ReturnIfAbrupt(exec).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 5. If IsCallable(exec) is true, then
    if (exec->IsCallable()) {
        JSHandle<JSTaggedValue> undefined = globalConst->GetHandledUndefined();
        EcmaRuntimeCallInfo *info = EcmaInterpreter::NewRuntimeCallInfo(thread, exec, regexp, undefined, 1);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        info->SetCallArg(inputStr.GetTaggedValue());
        JSTaggedValue result = JSFunction::Call(info);
        // b. ReturnIfAbrupt(result).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        if (!result.IsECMAObject() && !result.IsNull()) {
            // throw a TypeError exception.
            THROW_TYPE_ERROR_AND_RETURN(thread, "exec result is null or is not Object", JSTaggedValue::Exception());
        }
        return result;
    }
    // 6. If R does not have a [[RegExpMatcher]] internal slot, throw a TypeError exception.
    if (!regexp->IsJSRegExp()) {
        // throw a TypeError exception.
        THROW_TYPE_ERROR_AND_RETURN(thread, "this does not have a [[RegExpMatcher]]", JSTaggedValue::Exception());
    }
    // 7. Return RegExpBuiltinExec(R, S).
    return RegExpBuiltinExec(thread, regexp, inputString, useCache);
}

JSTaggedValue BuiltinsRegExp::RegExpExecForTestFast(JSThread *thread, JSHandle<JSTaggedValue> &regexp,
                                                    const JSHandle<JSTaggedValue> &inputStr, bool useCache)
{
    JSHandle<JSObject> object = JSHandle<JSObject>::Cast(regexp);
    JSTaggedValue lastIndexValue = object->GetPropertyInlinedProps(LAST_INDEX_OFFSET);
    // ASSERT GetPropertyInlinedProps(LAST_INDEX_OFFSET) is not hole
    ASSERT(!JSTaggedValue::SameValue(lastIndexValue, JSTaggedValue::Hole()));
    // 1. load lastIndex as length
    int32_t lastIndex = 0;
    if (lastIndexValue.IsInt()) {
        lastIndex = lastIndexValue.GetInt();
    } else {
        JSHandle<JSTaggedValue> lastIndexResult(thread, lastIndexValue);
        JSTaggedNumber lastIndexNumber = JSTaggedValue::ToLength(thread, lastIndexResult);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        lastIndex = lastIndexNumber.GetNumber();
    }
    // 2. Check whether the regexp is global or sticky, which determines whether we update last index later on.
    JSHandle<JSRegExp> regexpObj(regexp);
    JSMutableHandle<JSTaggedValue> pattern(thread, regexpObj->GetOriginalSource());
    JSMutableHandle<JSTaggedValue> flags(thread, regexpObj->GetOriginalFlags());
    JSHandle<RegExpExecResultCache> cacheTable(thread->GetCurrentEcmaContext()->GetRegExpCache());
    uint8_t flagsBits = static_cast<uint8_t>(flags->GetInt());
    bool global = (flagsBits & RegExpParser::FLAG_GLOBAL) != 0;
    bool sticky = (flagsBits & RegExpParser::FLAG_STICKY) != 0;
    if (!global && !sticky) {
        lastIndex = 0;
    }
    // 3. Search RegExpExecResult cache
    uint32_t lastIndexInput = static_cast<uint32_t>(lastIndex);
    if (useCache) {
        JSTaggedValue cacheResult = cacheTable->FindCachedResult(thread, pattern, flags, inputStr,
                                                                 RegExpExecResultCache::TEST_TYPE, regexp,
                                                                 JSTaggedValue(lastIndexInput));
        if (!cacheResult.IsUndefined()) {
            return cacheResult;
        }
    }

    uint32_t length = EcmaStringAccessor(inputStr->GetTaggedObject()).GetLength();
    if (lastIndex > static_cast<int32_t>(length)) {
        object->SetPropertyInlinedPropsWithRep(thread, LAST_INDEX_OFFSET, JSTaggedValue(0));
        return JSTaggedValue::False();
    }
    JSHandle<EcmaString> inputString = JSHandle<EcmaString>::Cast(inputStr);
    bool matchResult = RegExpExecInternal(thread, regexp, inputString, lastIndex);
    if (!matchResult) {
        if (global || sticky) {
            object->SetPropertyInlinedPropsWithRep(thread, LAST_INDEX_OFFSET, JSTaggedValue(0));
        }
        if (useCache) {
            RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flags, inputStr,
                                                    JSHandle<JSTaggedValue>(thread, JSTaggedValue(matchResult)),
                                                    RegExpExecResultCache::TEST_TYPE,
                                                    lastIndexInput, 0); // 0: match fail so lastIndex is 0
        }
        return JSTaggedValue::False();
    }
    JSHandle<RegExpGlobalResult> globalTable(thread->GetCurrentEcmaContext()->GetRegExpGlobalResult());
    JSTaggedValue endIndex = globalTable->GetEndIndex();
    if (global || sticky) {
        object->SetPropertyInlinedPropsWithRep(thread, LAST_INDEX_OFFSET, endIndex);
    }
    if (useCache) {
        RegExpExecResultCache::AddResultInCache(thread, cacheTable, pattern, flags, inputStr,
                                                JSHandle<JSTaggedValue>(thread, JSTaggedValue(matchResult)),
                                                RegExpExecResultCache::TEST_TYPE,
                                                lastIndexInput, endIndex.GetInt());
    }
    return GetTaggedBoolean(matchResult);
}

// 21.2.3.2.1
JSTaggedValue BuiltinsRegExp::RegExpAlloc(JSThread *thread, const JSHandle<JSTaggedValue> &newTarget)
{
    BUILTINS_API_TRACE(thread, RegExp, RegExpAlloc);
    /**
     * 1. Let obj be OrdinaryCreateFromConstructor(newTarget, "%RegExpPrototype%",
     * «[[RegExpMatcher]],[[OriginalSource]], [[OriginalFlags]]»).
     * */
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    JSHandle<JSTaggedValue> func = env->GetRegExpFunction();
    JSHandle<JSTaggedValue> obj(factory->NewJSObjectByConstructor(JSHandle<JSFunction>(func), newTarget));
    // 2. ReturnIfAbrupt(obj).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 5. Return obj.
    return obj.GetTaggedValue();
}

uint32_t BuiltinsRegExp::UpdateExpressionFlags(JSThread *thread, const CString &checkStr)
{
    uint32_t flagsBits = 0;
    uint32_t flagsBitsTemp = 0;
    for (char i : checkStr) {
        switch (i) {
            case 'g':
                flagsBitsTemp = RegExpParser::FLAG_GLOBAL;
                break;
            case 'i':
                flagsBitsTemp = RegExpParser::FLAG_IGNORECASE;
                break;
            case 'm':
                flagsBitsTemp = RegExpParser::FLAG_MULTILINE;
                break;
            case 's':
                flagsBitsTemp = RegExpParser::FLAG_DOTALL;
                break;
            case 'u':
                flagsBitsTemp = RegExpParser::FLAG_UTF16;
                break;
            case 'y':
                flagsBitsTemp = RegExpParser::FLAG_STICKY;
                break;
            case 'd':
                flagsBitsTemp = RegExpParser::FLAG_HASINDICES;
                break;
            default: {
                ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
                JSHandle<JSObject> syntaxError =
                    factory->GetJSError(base::ErrorType::SYNTAX_ERROR, "invalid regular expression flags");
                THROW_NEW_ERROR_AND_RETURN_VALUE(thread, syntaxError.GetTaggedValue(), 0);
            }
        }
        if ((flagsBits & flagsBitsTemp) != 0) {
            ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
            JSHandle<JSObject> syntaxError =
                factory->GetJSError(base::ErrorType::SYNTAX_ERROR, "invalid regular expression flags");
            THROW_NEW_ERROR_AND_RETURN_VALUE(thread, syntaxError.GetTaggedValue(), 0);
        }
        flagsBits |= flagsBitsTemp;
    }
    return flagsBits;
}

JSTaggedValue BuiltinsRegExp::FlagsBitsToString(JSThread *thread, uint8_t flags)
{
    ASSERT((flags & 0x80) == 0);  // 0x80: first bit of flags must be 0
    BUILTINS_API_TRACE(thread, RegExp, FlagsBitsToString);
    uint8_t *flagsStr = new uint8_t[RegExpParser::FLAG_NUM + 1];  // FLAG_NUM flags + '\0'
    size_t flagsLen = 0;
    if (flags & RegExpParser::FLAG_HASINDICES) {
        flagsStr[flagsLen] = 'd';
        flagsLen++;
    }
    if (flags & RegExpParser::FLAG_GLOBAL) {
        flagsStr[flagsLen] = 'g';
        flagsLen++;
    }
    if (flags & RegExpParser::FLAG_IGNORECASE) {
        flagsStr[flagsLen] = 'i';
        flagsLen++;
    }
    if (flags & RegExpParser::FLAG_MULTILINE) {
        flagsStr[flagsLen] = 'm';
        flagsLen++;
    }
    if (flags & RegExpParser::FLAG_DOTALL) {
        flagsStr[flagsLen] = 's';
        flagsLen++;
    }
    if (flags & RegExpParser::FLAG_UTF16) {
        flagsStr[flagsLen] = 'u';
        flagsLen++;
    }
    if (flags & RegExpParser::FLAG_STICKY) {
        flagsStr[flagsLen] = 'y';
        flagsLen++;
    }
    flagsStr[flagsLen] = '\0';
    JSHandle<EcmaString> flagsString = thread->GetEcmaVM()->GetFactory()->NewFromUtf8(flagsStr, flagsLen);
    delete[] flagsStr;

    return flagsString.GetTaggedValue();
}

// 21.2.3.2.2
JSTaggedValue BuiltinsRegExp::RegExpInitialize(JSThread *thread, const JSHandle<JSTaggedValue> &obj,
                                               const JSHandle<JSTaggedValue> &pattern,
                                               const JSHandle<JSTaggedValue> &flags)
{
    BUILTINS_API_TRACE(thread, RegExp, RegExpInitialize);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<EcmaString> patternStrHandle;
    uint8_t flagsBits = 0;
    // 1. If pattern is undefined, let P be the empty String.
    if (pattern->IsUndefined()) {
        patternStrHandle = factory->GetEmptyString();
    } else {
        // 2. Else, let P be ToString(pattern).
        patternStrHandle = JSTaggedValue::ToString(thread, pattern);
        // 3. ReturnIfAbrupt(P).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    }
    // 4. If flags is undefined, let F be the empty String.
    if (flags->IsUndefined()) {
        flagsBits = 0;
    } else if (flags->IsInt()) {
        flagsBits = static_cast<uint8_t>(flags->GetInt());
    } else {
        // 5. Else, let F be ToString(flags).
        JSHandle<EcmaString> flagsStrHandle = JSTaggedValue::ToString(thread, flags);
        // 6. ReturnIfAbrupt(F).
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        /**
         * 7. If F contains any code unit other than "d", "g", "i", "m", "u", or "y" or if it contains the same code
         * unit more than once, throw a SyntaxError exception.
         **/
        CString checkStr = ConvertToString(*flagsStrHandle, StringConvertedUsage::LOGICOPERATION);
        flagsBits = static_cast<uint8_t>(UpdateExpressionFlags(thread, checkStr));
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    }
    // String -> CString
    CString patternStdStr = ConvertToString(*patternStrHandle, StringConvertedUsage::LOGICOPERATION);
    // 9. 10.
    Chunk chunk(thread->GetNativeAreaAllocator());
    RegExpParser parser = RegExpParser(thread, &chunk);
    RegExpParserCache *regExpParserCache = thread->GetCurrentEcmaContext()->GetRegExpParserCache();
    CVector<CString> groupName;
    auto getCache = regExpParserCache->GetCache(*patternStrHandle, flagsBits, groupName);
    if (getCache.first.IsHole()) {
        parser.Init(const_cast<char *>(reinterpret_cast<const char *>(patternStdStr.c_str())), patternStdStr.size(),
                    flagsBits);
        parser.Parse();
        if (parser.IsError()) {
            JSHandle<JSObject> syntaxError =
                factory->GetJSError(base::ErrorType::SYNTAX_ERROR, parser.GetErrorMsg().c_str());
            THROW_NEW_ERROR_AND_RETURN_VALUE(thread, syntaxError.GetTaggedValue(), JSTaggedValue::Exception());
        }
        groupName = parser.GetGroupNames();
    }
    JSHandle<JSRegExp> regexp(thread, JSRegExp::Cast(obj->GetTaggedObject()));
    // 11. Set the value of obj’s [[OriginalSource]] internal slot to P.
    regexp->SetOriginalSource(thread, patternStrHandle.GetTaggedValue());
    // 12. Set the value of obj’s [[OriginalFlags]] internal slot to F.
    regexp->SetOriginalFlags(thread, JSTaggedValue(flagsBits));
    if (!groupName.empty()) {
        JSHandle<TaggedArray> taggedArray = factory->NewTaggedArray(groupName.size());
        for (size_t i = 0; i < groupName.size(); ++i) {
            JSHandle<JSTaggedValue> flagsKey(factory->NewFromStdString(groupName[i].c_str()));
            taggedArray->Set(thread, i, flagsKey);
        }
        regexp->SetGroupName(thread, taggedArray);
    }
    // 13. Set obj’s [[RegExpMatcher]] internal slot.
    if (getCache.first.IsHole()) {
        auto bufferSize = parser.GetOriginBufferSize();
        auto buffer = parser.GetOriginBuffer();
        factory->NewJSRegExpByteCodeData(regexp, buffer, bufferSize);
        regExpParserCache->SetCache(*patternStrHandle, flagsBits, regexp->GetByteCodeBuffer(), bufferSize, groupName);
    } else {
        regexp->SetByteCodeBuffer(thread, getCache.first);
        regexp->SetLength(static_cast<uint32_t>(getCache.second));
    }
    // 14. Let setStatus be Set(obj, "lastIndex", 0, true).
    JSHandle<JSTaggedValue> lastIndexString = thread->GlobalConstants()->GetHandledLastIndexString();
    ObjectFastOperator::FastSetPropertyByValue(thread, obj.GetTaggedValue(),
                                               lastIndexString.GetTaggedValue(), JSTaggedValue(0));
    // 15. ReturnIfAbrupt(setStatus).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 16. Return obj.
    return obj.GetTaggedValue();
}

JSTaggedValue BuiltinsRegExp::RegExpCreate(JSThread *thread, const JSHandle<JSTaggedValue> &pattern,
                                           const JSHandle<JSTaggedValue> &flags)
{
    BUILTINS_API_TRACE(thread, RegExp, Create);
    auto ecmaVm = thread->GetEcmaVM();
    JSHandle<GlobalEnv> env = ecmaVm->GetGlobalEnv();
    JSHandle<JSTaggedValue> newTarget = env->GetRegExpFunction();
    // 1. Let obj be RegExpAlloc(%RegExp%).
    JSHandle<JSTaggedValue> object(thread, RegExpAlloc(thread, newTarget));
    // 2. ReturnIfAbrupt(obj).
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    // 3. Return RegExpInitialize(obj, P, F).
    return RegExpInitialize(thread, object, pattern, flags);
}

// 21.2.3.2.4
EcmaString *BuiltinsRegExp::EscapeRegExpPattern(JSThread *thread, const JSHandle<JSTaggedValue> &src,
                                                const JSHandle<JSTaggedValue> &flags)
{
    BUILTINS_API_TRACE(thread, RegExp, EscapeRegExpPattern);
    // String -> CString
    JSHandle<EcmaString> srcStr(thread, static_cast<EcmaString *>(src->GetTaggedObject()));
    JSHandle<EcmaString> flagsStr(thread, static_cast<EcmaString *>(flags->GetTaggedObject()));
    CString srcStdStr = ConvertToString(*srcStr, StringConvertedUsage::LOGICOPERATION);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    // "" -> (?:)
    if (srcStdStr.empty()) {
        srcStdStr = "(?:)";
    }
    // "/" -> "\/"
    srcStdStr = base::StringHelper::ReplaceAll(srcStdStr, "/", "\\/");
    // "\\" -> "\"
    srcStdStr = base::StringHelper::ReplaceAll(srcStdStr, "\\", "\\");

    return *factory->NewFromUtf8(srcStdStr);
}

JSTaggedValue BuiltinsRegExp::IsValidRegularExpression(JSThread *thread, JSHandle<JSTaggedValue> &thisObj)
{
    JSHandle<EcmaString> patternStrHandle = JSTaggedValue::ToString(thread, thisObj);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    uint32_t regExpStringLength = static_cast<uint32_t>(EcmaStringAccessor(patternStrHandle).GetLength());
    if (regExpStringLength > BuiltinsRegExp::MAX_REGEXP_STRING_COUNT) {
        return JSTaggedValue::False();
    }
    return JSTaggedValue::True();
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define SET_GET_CAPTURE_IMPL(index)                                                                                   \
    JSTaggedValue BuiltinsRegExp::GetCapture##index(JSThread *thread, [[maybe_unused]] const JSHandle<JSObject> &obj) \
    {                                                                                                                 \
        return RegExpGlobalResult::GetCapture<index>(thread);                                                         \
    }                                                                                                                 \
    bool BuiltinsRegExp::SetCapture##index([[maybe_unused]] JSThread *thread,                                         \
                                           [[maybe_unused]] const JSHandle<JSObject> &obj,                            \
                                           [[maybe_unused]] const JSHandle<JSTaggedValue> &value,                     \
                                           [[maybe_unused]] bool mayThrow)                                            \
    {                                                                                                                 \
        return true;                                                                                                  \
    }

    SET_GET_CAPTURE_IMPL(1)
    SET_GET_CAPTURE_IMPL(2)
    SET_GET_CAPTURE_IMPL(3)
    SET_GET_CAPTURE_IMPL(4)
    SET_GET_CAPTURE_IMPL(5)
    SET_GET_CAPTURE_IMPL(6)
    SET_GET_CAPTURE_IMPL(7)
    SET_GET_CAPTURE_IMPL(8)
    SET_GET_CAPTURE_IMPL(9)
#undef SET_GET_CAPTURE_IMPL

JSTaggedValue RegExpExecResultCache::CreateCacheTable(JSThread *thread)
{
    int length = CACHE_TABLE_HEADER_SIZE + INITIAL_CACHE_NUMBER * ENTRY_SIZE;

    auto table = static_cast<RegExpExecResultCache *>(
        *thread->GetEcmaVM()->GetFactory()->NewTaggedArray(length, JSTaggedValue::Undefined()));
    table->SetLargeStrCount(thread, DEFAULT_LARGE_STRING_COUNT);
    table->SetConflictCount(thread, DEFAULT_CONFLICT_COUNT);
    table->SetStrLenThreshold(thread, 0);
    table->SetHitCount(thread, 0);
    table->SetCacheCount(thread, 0);
    table->SetCacheLength(thread, INITIAL_CACHE_NUMBER);
    return JSTaggedValue(table);
}

JSTaggedValue RegExpExecResultCache::FindCachedResult(JSThread *thread,
                                                      const JSHandle<JSTaggedValue> &pattern,
                                                      const JSHandle<JSTaggedValue> &flags,
                                                      const JSHandle<JSTaggedValue> &input, CacheType type,
                                                      const JSHandle<JSTaggedValue> &regexp,
                                                      JSTaggedValue lastIndexInput, JSTaggedValue extend,
                                                      bool isIntermediateResult)
{
    JSTaggedValue patternValue = pattern.GetTaggedValue();
    JSTaggedValue flagsValue = flags.GetTaggedValue();
    JSTaggedValue inputValue = input.GetTaggedValue();

    if (!pattern->IsString() || !flags->IsInt() || !input->IsString() || !lastIndexInput.IsInt()) {
        return JSTaggedValue::Undefined();
    }

    uint32_t hash = pattern->GetKeyHashCode() + static_cast<uint32_t>(flags->GetInt()) +
                    input->GetKeyHashCode() + static_cast<uint32_t>(lastIndexInput.GetInt());
    uint32_t entry = hash & static_cast<uint32_t>(GetCacheLength() - 1);
    if (!Match(entry, patternValue, flagsValue, inputValue, lastIndexInput, extend, type)) {
        uint32_t entry2 = (entry + 1) & static_cast<uint32_t>(GetCacheLength() - 1);
        if (!Match(entry2, patternValue, flagsValue, inputValue, lastIndexInput, extend, type)) {
            return JSTaggedValue::Undefined();
        }
        entry = entry2;
    }
    ASSERT((static_cast<size_t>(CACHE_TABLE_HEADER_SIZE) +
        static_cast<size_t>(entry) * static_cast<size_t>(ENTRY_SIZE)) <= static_cast<size_t>(UINT32_MAX));
    uint32_t index = CACHE_TABLE_HEADER_SIZE + entry * ENTRY_SIZE;
    // update cached value if input value is changed
    JSTaggedValue cachedStr = Get(index + INPUT_STRING_INDEX);
    if (!cachedStr.IsUndefined() && cachedStr != inputValue) {
        Set(thread, index + INPUT_STRING_INDEX, inputValue);
    }
    JSTaggedValue result;
    switch (type) {
        case REPLACE_TYPE:
            result = Get(index + RESULT_REPLACE_INDEX);
            break;
        case SPLIT_TYPE:
            result = Get(index + RESULT_SPLIT_INDEX);
            break;
        case MATCH_TYPE:
            result = Get(index + RESULT_MATCH_INDEX);
            break;
        case EXEC_TYPE:
            result = Get(index + RESULT_EXEC_INDEX);
            break;
        case INTERMEDIATE_REPLACE_TYPE:
            result = Get(index + RESULT_INTERMEDIATE_REPLACE_INDEX);
            break;
        case TEST_TYPE:
            result = Get(index + RESULT_TEST_INDEX);
            break;
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
            break;
    }
    SetHitCount(thread, GetHitCount() + 1);
    JSHandle<JSTaggedValue> lastIndexHandle = thread->GlobalConstants()->GetHandledLastIndexString();
    ObjectFastOperator::FastSetPropertyByValue(thread, regexp.GetTaggedValue(), lastIndexHandle.GetTaggedValue(),
                                               Get(index + LAST_INDEX_INDEX));
    if (!isIntermediateResult && result.IsJSArray()) {
        JSHandle<JSArray> resultHandle(thread, JSArray::Cast(result));
        JSHandle<JSArray> copyArray = thread->GetEcmaVM()->GetFactory()->CloneArrayLiteral(resultHandle);
        return copyArray.GetTaggedValue();
    }
    return result;
}

void RegExpExecResultCache::AddResultInCache(JSThread *thread, JSHandle<RegExpExecResultCache> cache,
                                             const JSHandle<JSTaggedValue> &pattern,
                                             const JSHandle<JSTaggedValue> &flags, const JSHandle<JSTaggedValue> &input,
                                             const JSHandle<JSTaggedValue> &resultArray, CacheType type,
                                             uint32_t lastIndexInput, uint32_t lastIndex, JSTaggedValue extend,
                                             bool isIntermediateResult)
{
    if (!pattern->IsString() || !flags->IsInt() || !input->IsString()) {
        return;
    }

    JSHandle<JSTaggedValue> resultArrayCopy;
    if (!isIntermediateResult && resultArray->IsJSArray()) {
        JSHandle<JSArray> copyArray = thread->GetEcmaVM()->GetFactory()
                                            ->CloneArrayLiteral(JSHandle<JSArray>(resultArray));
        resultArrayCopy = JSHandle<JSTaggedValue>(copyArray);
    } else {
        resultArrayCopy = JSHandle<JSTaggedValue>(resultArray);
    }

    JSTaggedValue patternValue = pattern.GetTaggedValue();
    JSTaggedValue flagsValue = flags.GetTaggedValue();
    JSTaggedValue inputValue = input.GetTaggedValue();
    JSTaggedValue lastIndexInputValue(lastIndexInput);
    JSTaggedValue lastIndexValue(lastIndex);

    uint32_t hash = patternValue.GetKeyHashCode() + static_cast<uint32_t>(flagsValue.GetInt()) +
                    inputValue.GetKeyHashCode() + lastIndexInput;
    uint32_t entry = hash & static_cast<uint32_t>(cache->GetCacheLength() - 1);
    ASSERT((static_cast<size_t>(CACHE_TABLE_HEADER_SIZE) +
        static_cast<size_t>(entry) * static_cast<size_t>(ENTRY_SIZE)) <= static_cast<size_t>(UINT32_MAX));
    uint32_t index = CACHE_TABLE_HEADER_SIZE + entry * ENTRY_SIZE;
    if (cache->Get(index).IsUndefined()) {
        cache->SetCacheCount(thread, cache->GetCacheCount() + 1);
        cache->SetEntry(thread, entry, patternValue, flagsValue, inputValue,
                        lastIndexInputValue, lastIndexValue, extend);
        cache->UpdateResultArray(thread, entry, resultArrayCopy.GetTaggedValue(), type);
    } else if (cache->Match(entry, patternValue, flagsValue, inputValue, lastIndexInputValue, extend, type)) {
        cache->UpdateResultArray(thread, entry, resultArrayCopy.GetTaggedValue(), type);
    } else {
        uint32_t entry2 = (entry + 1) & static_cast<uint32_t>(cache->GetCacheLength() - 1);
        ASSERT((static_cast<size_t>(CACHE_TABLE_HEADER_SIZE) +
            static_cast<size_t>(entry2) * static_cast<size_t>(ENTRY_SIZE)) <= static_cast<size_t>(UINT32_MAX));
        uint32_t index2 = CACHE_TABLE_HEADER_SIZE + entry2 * ENTRY_SIZE;
        JSHandle<JSTaggedValue> extendHandle(thread, extend);
        if (cache->GetCacheLength() < DEFAULT_CACHE_NUMBER) {
            GrowRegexpCache(thread, cache);
            // update value after gc.
            patternValue = pattern.GetTaggedValue();
            flagsValue = flags.GetTaggedValue();
            inputValue = input.GetTaggedValue();

            cache->SetCacheLength(thread, DEFAULT_CACHE_NUMBER);
            entry2 = hash & static_cast<uint32_t>(cache->GetCacheLength() - 1);
            index2 = CACHE_TABLE_HEADER_SIZE + entry2 * ENTRY_SIZE;
        }
        JSTaggedValue extendValue = extendHandle.GetTaggedValue();
        if (cache->Get(index2).IsUndefined()) {
            cache->SetCacheCount(thread, cache->GetCacheCount() + 1);
            cache->SetEntry(thread, entry2, patternValue, flagsValue, inputValue,
                            lastIndexInputValue, lastIndexValue, extendValue);
            cache->UpdateResultArray(thread, entry2, resultArrayCopy.GetTaggedValue(), type);
        } else if (cache->Match(entry2, patternValue, flagsValue, inputValue, lastIndexInputValue, extendValue, type)) {
            cache->UpdateResultArray(thread, entry2, resultArrayCopy.GetTaggedValue(), type);
        } else {
            cache->SetConflictCount(thread, cache->GetConflictCount() > 1 ? (cache->GetConflictCount() - 1) : 0);
            cache->SetCacheCount(thread, cache->GetCacheCount() - 1);
            cache->ClearEntry(thread, entry2);
            cache->SetEntry(thread, entry, patternValue, flagsValue, inputValue,
                            lastIndexInputValue, lastIndexValue, extendValue);
            cache->UpdateResultArray(thread, entry, resultArrayCopy.GetTaggedValue(), type);
        }
    }
}

void RegExpExecResultCache::GrowRegexpCache(JSThread *thread, JSHandle<RegExpExecResultCache> cache)
{
    int length = CACHE_TABLE_HEADER_SIZE + DEFAULT_CACHE_NUMBER * ENTRY_SIZE;
    auto factory = thread->GetEcmaVM()->GetFactory();
    auto newCache = factory->ExtendArray(JSHandle<TaggedArray>(cache), length, JSTaggedValue::Undefined());
    thread->GetCurrentEcmaContext()->SetRegExpCache(newCache.GetTaggedValue());
}

void RegExpExecResultCache::SetEntry(JSThread *thread, int entry, JSTaggedValue &pattern, JSTaggedValue &flags,
                                     JSTaggedValue &input, JSTaggedValue &lastIndexInputValue,
                                     JSTaggedValue &lastIndexValue, JSTaggedValue &extendValue)
{
    ASSERT((static_cast<size_t>(CACHE_TABLE_HEADER_SIZE) +
            static_cast<size_t>(entry) * static_cast<size_t>(ENTRY_SIZE)) <= static_cast<size_t>(INT_MAX));
    int index = CACHE_TABLE_HEADER_SIZE + entry * ENTRY_SIZE;
    Set(thread, index + PATTERN_INDEX, pattern);
    Set(thread, index + FLAG_INDEX, flags);
    Set(thread, index + INPUT_STRING_INDEX, input);
    Set(thread, index + LAST_INDEX_INPUT_INDEX, lastIndexInputValue);
    Set(thread, index + LAST_INDEX_INDEX, lastIndexValue);
    Set(thread, index + EXTEND_INDEX, extendValue);
}

void RegExpExecResultCache::UpdateResultArray(JSThread *thread, int entry, JSTaggedValue resultArray, CacheType type)
{
    ASSERT((static_cast<size_t>(CACHE_TABLE_HEADER_SIZE) +
            static_cast<size_t>(entry) * static_cast<size_t>(ENTRY_SIZE)) <= static_cast<size_t>(INT_MAX));
    int index = CACHE_TABLE_HEADER_SIZE + entry * ENTRY_SIZE;
    switch (type) {
        break;
        case REPLACE_TYPE:
            Set(thread, index + RESULT_REPLACE_INDEX, resultArray);
            break;
        case SPLIT_TYPE:
            Set(thread, index + RESULT_SPLIT_INDEX, resultArray);
            break;
        case MATCH_TYPE:
            Set(thread, index + RESULT_MATCH_INDEX, resultArray);
            break;
        case EXEC_TYPE:
            Set(thread, index + RESULT_EXEC_INDEX, resultArray);
            break;
        case INTERMEDIATE_REPLACE_TYPE:
            Set(thread, index + RESULT_INTERMEDIATE_REPLACE_INDEX, resultArray);
            break;
        case TEST_TYPE:
            Set(thread, index + RESULT_TEST_INDEX, resultArray);
            break;
        default:
            LOG_ECMA(FATAL) << "this branch is unreachable";
            UNREACHABLE();
            break;
    }
}

void RegExpExecResultCache::ClearEntry(JSThread *thread, int entry)
{
    ASSERT((static_cast<size_t>(CACHE_TABLE_HEADER_SIZE) +
            static_cast<size_t>(entry) * static_cast<size_t>(ENTRY_SIZE)) <= static_cast<size_t>(INT_MAX));
    int index = CACHE_TABLE_HEADER_SIZE + entry * ENTRY_SIZE;
    JSTaggedValue undefined = JSTaggedValue::Undefined();
    for (int i = 0; i < ENTRY_SIZE; i++) {
        Set(thread, index + i, undefined);
    }
}

bool RegExpExecResultCache::Match(int entry, JSTaggedValue &pattern, JSTaggedValue &flags, JSTaggedValue &input,
                                  JSTaggedValue &lastIndexInputValue, JSTaggedValue &extend, CacheType type)
{
    ASSERT((static_cast<size_t>(CACHE_TABLE_HEADER_SIZE) +
            static_cast<size_t>(entry) * static_cast<size_t>(ENTRY_SIZE)) <= static_cast<size_t>(INT_MAX));
    int index = CACHE_TABLE_HEADER_SIZE + entry * ENTRY_SIZE;

    JSTaggedValue typeKey = Get(index + RESULT_REPLACE_INDEX + type);
    if (typeKey.IsUndefined()) {
        return false;
    }

    JSTaggedValue keyPattern = Get(index + PATTERN_INDEX);
    if (keyPattern.IsUndefined()) {
        return false;
    }

    uint8_t flagsBits = static_cast<uint8_t>(flags.GetInt());
    JSTaggedValue keyFlags = Get(index + FLAG_INDEX);
    uint8_t keyFlagsBits = static_cast<uint8_t>(keyFlags.GetInt());
    if (flagsBits != keyFlagsBits) {
        return false;
    }

    uint32_t lastIndexInputInt = static_cast<uint32_t>(lastIndexInputValue.GetInt());
    JSTaggedValue keyLastIndexInput = Get(index + LAST_INDEX_INPUT_INDEX);
    uint32_t keyLastIndexInputInt = static_cast<uint32_t>(keyLastIndexInput.GetInt());
    if (lastIndexInputInt != keyLastIndexInputInt) {
        return false;
    }

    JSTaggedValue keyInput = Get(index + INPUT_STRING_INDEX);
    JSTaggedValue keyExtend = Get(index + EXTEND_INDEX);
    EcmaString *patternStr = EcmaString::Cast(pattern.GetTaggedObject());
    EcmaString *inputStr = EcmaString::Cast(input.GetTaggedObject());
    EcmaString *keyPatternStr = EcmaString::Cast(keyPattern.GetTaggedObject());
    EcmaString *keyInputStr = EcmaString::Cast(keyInput.GetTaggedObject());
    bool extendEqual = false;
    if (extend.IsString() && keyExtend.IsString()) {
        EcmaString *extendStr = EcmaString::Cast(extend.GetTaggedObject());
        EcmaString *keyExtendStr = EcmaString::Cast(keyExtend.GetTaggedObject());
        extendEqual = EcmaStringAccessor::StringsAreEqual(extendStr, keyExtendStr);
    } else if (extend.IsUndefined() && keyExtend.IsUndefined()) {
        extendEqual = true;
    } else {
        return false;
    }
    return extendEqual &&
           EcmaStringAccessor::StringsAreEqual(patternStr, keyPatternStr) &&
           EcmaStringAccessor::StringsAreEqual(inputStr, keyInputStr);
}

JSTaggedValue RegExpGlobalResult::CreateGlobalResultTable(JSThread *thread)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    uint32_t initialLength = GLOBAL_TABLE_SIZE + INITIAL_CAPTURE_INDICES;
    auto table = static_cast<RegExpGlobalResult *>(
        *factory->NewTaggedArray(initialLength, JSTaggedValue::Undefined()));
    // initialize dollars with empty string
    JSTaggedValue emptyString = factory->GetEmptyString().GetTaggedValue();
    for (uint32_t i = 1; i <= DOLLAR_NUMBER; i++) {
        table->SetCapture(thread, CAPTURE_START_INDEX + i, emptyString);
    }
    // initialize match info
    table->SetTotalCaptureCounts(thread, JSTaggedValue(0));
    table->SetInputString(thread, emptyString);
    for (uint32_t i = 0; i < INITIAL_CAPTURE_INDICES / 2; i++) { // 2: capture pair
        table->SetStartOfCaptureIndex(thread, i, JSTaggedValue(0));
        table->SetEndOfCaptureIndex(thread, i, JSTaggedValue(0));
    }
    return JSTaggedValue(table);
}

JSHandle<RegExpGlobalResult> RegExpGlobalResult::GrowCapturesCapacity(JSThread *thread,
    JSHandle<RegExpGlobalResult>result, uint32_t length)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<TaggedArray> newResult = factory->ExtendArray(
        JSHandle<TaggedArray>(result), length, JSTaggedValue(0));
    thread->GetCurrentEcmaContext()->SetRegExpGlobalResult(newResult.GetTaggedValue());
    return JSHandle<RegExpGlobalResult>(newResult);
}
}  // namespace panda::ecmascript::builtins
