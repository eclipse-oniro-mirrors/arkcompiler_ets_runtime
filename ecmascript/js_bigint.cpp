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

#include "ecmascript/js_bigint.h"
#include "ecmascript/js_tagged_value-inl.h"
#include "ecmascript/js_tagged_number.h"

namespace panda::ecmascript {
class ObjectFactory;
constexpr char dp[] = "0123456789abcdefghijklmnopqrstuvwxyz";
static int CharToInt(char c)
{
    uint32_t res = 0;
    if (c >= '0' && c <= '9') {
        res = c - '0';
    } else if (c >= 'A' && c <= 'Z') {
        res = c - 'A' + 10; // 10:res must Greater than 10.
    } else if (c >= 'a' && c <= 'z') {
        res = c - 'a' + 10; // 10:res must Greater than 10
    }
    return static_cast<int>(res);
}

static void Division(CString &num, uint32_t conversionToRadix, uint32_t currentRadix, uint32_t &remain)
{
    ASSERT(conversionToRadix != 0);
    uint32_t temp = 0;
    remain = 0;
    for (size_t i = 0; i < num.size(); i++) {
        temp = (currentRadix * remain + static_cast<uint32_t>(CharToInt(num[i])));
        num[i] = dp[temp / conversionToRadix];
        remain = temp % conversionToRadix;
    }
    size_t count = 0;
    while (count < num.size() && num[count] == '0') {
        count++;
    }
    num = num.substr(count);
}

CString BigIntHelper::Conversion(const CString &num, uint32_t conversionToRadix, uint32_t currentRadix)
{
    ASSERT(conversionToRadix != 0);
    CString newNum = num;
    CString res;
    uint32_t remain = 0;
    while (newNum.size() != 0) {
        Division(newNum, conversionToRadix, currentRadix, remain);
        res = dp[remain] + res;
    }
    return res;
}

JSHandle<BigInt> BigInt::GetUint64MaxBigint(JSThread *thread)
{
    JSHandle<BigInt> bigint = CreateBigint(thread, 3);
    bigint->SetDigit(0, 0);
    bigint->SetDigit(1, 0);
    bigint->SetDigit(2, 1);
    return bigint;
}

JSHandle<BigInt> BigInt::GetInt64MaxBigint(JSThread *thread)
{
    JSHandle<BigInt> bigint = CreateBigint(thread, 2);
    bigint->SetDigit(0, 0);
    bigint->SetDigit(1, 0x80000000); // 0x80000000:Int MAX
    return bigint;
}

JSHandle<BigInt> BigIntHelper::SetBigInt(JSThread *thread, const CString &numStr, uint32_t currentRadix)
{
    int flag = 0;
    if (numStr[0] == '-') {
        flag = 1;
    }

    CString binaryStr = "";
    if (currentRadix != BigInt::BINARY) {
        binaryStr = Conversion(numStr.substr(flag), BigInt::BINARY, currentRadix);
    } else {
        binaryStr = numStr.substr(flag);
    }

    JSHandle<BigInt> bigint;
    size_t binaryStrLen = binaryStr.size();
    size_t len = binaryStrLen / BigInt::DATEBITS;
    size_t mod = binaryStrLen % BigInt::DATEBITS;
    int index = 0;
    if (mod == 0) {
        index = static_cast<int>(len - 1);
        bigint = BigInt::CreateBigint(thread, len);
    } else {
        len++;
        index = static_cast<int>(len - 1);
        bigint = BigInt::CreateBigint(thread, len);
        uint32_t val = 0;
        for (size_t i = 0; i < mod; ++i) {
            val <<= 1;
            val |= static_cast<uint32_t>(binaryStr[i] - '0');
        }
        bigint->SetDigit(index, val);
        index--;
    }
    if (flag == 1) {
        bigint->SetSign(true);
    }
    size_t i = mod;
    while (i < binaryStrLen) {
        uint32_t val = 0;
        for (size_t j = 0; j < BigInt::DATEBITS && i < binaryStrLen; ++j, ++i) {
            val <<= 1;
            val |= static_cast<uint32_t>(binaryStr[i] - '0');
        }
        bigint->SetDigit(index, val);
        index--;
    }
    return BigIntHelper::RightTruncate(thread, bigint);
}

JSHandle<BigInt> BigIntHelper::RightTruncate(JSThread *thread, JSHandle<BigInt> x)
{
    int len  = static_cast<int>(x->GetLength());
    ASSERT(len != 0);
    if (len == 1 && x->GetDigit(0) == 0) {
        x->SetSign(false);
        return x;
    }
    int index = len - 1;
    if (x->GetDigit(index) != 0) {
        return x;
    }
    while (index >= 0) {
        if (x->GetDigit(index) != 0) {
            break;
        }
        index--;
    }

    if (index == -1) {
        return BigInt::Int32ToBigInt(thread, 0);
    } else {
        ASSERT(index >= 0);
        return BigInt::Copy(thread, x, index + 1);
    }
}

CString BigIntHelper::GetBinary(const BigInt *bigint)
{
    ASSERT(bigint != nullptr);
    int index = 0;
    int len = static_cast<int>(bigint->GetLength());
    int strLen = BigInt::DATEBITS * len;
    CString res(strLen, '0');
    int strIndex = strLen - 1;
    while (index < len) {
        int bityLen = BigInt::DATEBITS;
        uint32_t val = bigint->GetDigit(index);
        while (bityLen--) {
            res[strIndex--] = (val & 1) + '0';
            val = val >> 1;
        }
        index++;
    }
    DeZero(res);
    return res;
}

JSHandle<BigInt> BigInt::CreateBigint(JSThread *thread, uint32_t length)
{
    ASSERT(length < MAXSIZE);
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<BigInt> bigint = factory->NewBigInt(length);
    return bigint;
}

// 6.1.6.2.13
bool BigInt::Equal(const JSTaggedValue &x, const JSTaggedValue &y)
{
    BigInt* xVal = BigInt::Cast(x.GetTaggedObject());
    BigInt* yVal = BigInt::Cast(y.GetTaggedObject());
    return Equal(xVal, yVal);
}

bool BigInt::Equal(const BigInt *x, const BigInt *y)
{
    ASSERT(x != nullptr);
    ASSERT(y != nullptr);
    if (x->GetSign() != y->GetSign() || x->GetLength() != y->GetLength()) {
        return false;
    }
    for (uint32_t i = 0; i < x->GetLength(); ++i) {
        if (x->GetDigit(i) != y->GetDigit(i)) {
            return false;
        }
    }
    return true;
}

// 6.1.6.2.14
bool BigInt::SameValue(const JSTaggedValue &x, const JSTaggedValue &y)
{
    return Equal(x, y);
}

// 6.1.6.2.15
bool BigInt::SameValueZero(const JSTaggedValue &x, const JSTaggedValue &y)
{
    return Equal(x, y);
}

JSHandle<BigInt> BigInt::BitwiseOp(JSThread *thread, Operate op, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    uint32_t maxLen = 0;
    uint32_t minLen = 0;
    uint32_t xlen = x->GetLength();
    uint32_t ylen = y->GetLength();
    if (xlen > ylen) {
        maxLen = xlen;
        minLen = ylen;
    } else {
        maxLen = ylen;
        minLen = xlen;
    }
    JSHandle<BigInt> bigint = BigInt::CreateBigint(thread, maxLen);
    for (size_t i = 0; i < minLen; ++i) {
        if (op == Operate::OR) {
            bigint->SetDigit(i, x->GetDigit(i) | y->GetDigit(i));
        } else if (op == Operate::AND) {
            bigint->SetDigit(i, x->GetDigit(i) & y->GetDigit(i));
        } else {
            ASSERT(op == Operate::XOR);
            bigint->SetDigit(i, x->GetDigit(i) ^ y->GetDigit(i));
        }
    }
    if (op == Operate::OR || op == Operate::XOR) {
        if (xlen > ylen) {
            for (size_t i = ylen; i < xlen; ++i) {
                bigint->SetDigit(i, x->GetDigit(i));
            }
        } else if (ylen > xlen) {
            for (size_t i = xlen; i < ylen; ++i) {
                bigint->SetDigit(i, y->GetDigit(i));
            }
        }
    }
    return BigIntHelper::RightTruncate(thread, bigint);
}

JSHandle<BigInt> OneIsNegativeAND(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    JSHandle<BigInt> yVal = BigInt::BitwiseSubOne(thread, y, y->GetLength());
    uint32_t xLength = x->GetLength();
    uint32_t yLength = yVal->GetLength();
    uint32_t minLen = xLength;
    if (xLength > yLength) {
        minLen = yLength;
    }
    JSHandle<BigInt> newBigint = BigInt::CreateBigint(thread, xLength);
    uint32_t i = 0;
    while (i < minLen) {
        uint32_t res = x->GetDigit(i) & ~(yVal->GetDigit(i));
        newBigint->SetDigit(i, res);
        ++i;
    }
    while (i < xLength) {
        newBigint->SetDigit(i, x->GetDigit(i));
        ++i;
    }
    return BigIntHelper::RightTruncate(thread, newBigint);
}

// 6.1.6.2.20 BigInt::bitwiseAND ( x, y )
JSHandle<BigInt> BigInt::BitwiseAND(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    if (x->GetSign() && y->GetSign()) {
        // (-x) & (-y) == -(((x-1) | (y-1)) + 1)
        JSHandle<BigInt> xVal = BitwiseSubOne(thread, x, x->GetLength());
        JSHandle<BigInt> yVal = BitwiseSubOne(thread, y, y->GetLength());
        JSHandle<BigInt> temp = BitwiseOp(thread, Operate::OR, xVal, yVal);
        JSHandle<BigInt> res = BitwiseAddOne(thread, temp);
        return res;
    }
    if (x->GetSign() != y->GetSign()) {
        // x & (-y) == x & ~(y-1)
        if (!x->GetSign()) {
            return OneIsNegativeAND(thread, x, y);
        } else {
            return OneIsNegativeAND(thread, y, x);
        }
    }
    return BitwiseOp(thread, Operate::AND, x, y);
}

JSHandle<BigInt> OneIsNegativeXOR(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    JSHandle<BigInt> yVal = BigInt::BitwiseSubOne(thread, y, y->GetLength());
    JSHandle<BigInt> temp = BigInt::BitwiseOp(thread, Operate::XOR, x, yVal);
    JSHandle<BigInt> res = BigInt::BitwiseAddOne(thread, temp);
    return res;
}

// 6.1.6.2.21 BigInt::bitwiseOR ( x, y )
JSHandle<BigInt> BigInt::BitwiseXOR(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    if (x->GetSign() && y->GetSign()) {
        // (-x) ^ (-y) == (x-1) ^ (y-1)
        JSHandle<BigInt> xVal = BitwiseSubOne(thread, x, x->GetLength());
        JSHandle<BigInt> yVal = BitwiseSubOne(thread, y, y->GetLength());
        return BitwiseOp(thread, Operate::XOR, xVal, yVal);
    }
    if (x->GetSign() != y->GetSign()) {
        // x ^ (-y) == -((x ^ (y-1)) + 1)
        if (!x->GetSign()) {
            return OneIsNegativeXOR(thread, x, y);
        } else {
            return OneIsNegativeXOR(thread, y, x);
        }
    }
    return BitwiseOp(thread, Operate::XOR, x, y);
}

JSHandle<BigInt> BigInt::BitwiseSubOne(JSThread *thread, JSHandle<BigInt> bigint, uint32_t maxLen)
{
    ASSERT(!bigint->IsZero());
    ASSERT(maxLen >= bigint->GetLength());

    JSHandle<BigInt> newBigint = BigInt::CreateBigint(thread, maxLen);

    uint32_t bigintLen = bigint->GetLength();
    uint32_t carry = 1;
    for (uint32_t i = 0; i < bigintLen; i++) {
        uint32_t bigintCarry = 0;
        newBigint->SetDigit(i, BigIntHelper::SubHelper(bigint->GetDigit(i), carry, bigintCarry));
        carry = bigintCarry;
    }
    ASSERT(!carry);
    for (uint32_t i = bigintLen; i < maxLen; i++) {
        newBigint->SetDigit(i, carry);
    }
    return BigIntHelper::RightTruncate(thread, newBigint);
}

JSHandle<BigInt> BigInt::BitwiseAddOne(JSThread *thread, JSHandle<BigInt> bigint)
{
    uint32_t bigintLength = bigint->GetLength();

    bool needExpend = true;
    for (uint32_t i = 0; i < bigintLength; i++) {
        if (std::numeric_limits<uint32_t>::max() != bigint->GetDigit(i)) {
            needExpend = false;
            break;
        }
    }
    uint32_t newLength = bigintLength;
    if (needExpend) {
        newLength += 1;
    }
    JSHandle<BigInt> newBigint = BigInt::CreateBigint(thread, newLength);

    uint32_t carry = 1;
    for (uint32_t i = 0; i < bigintLength; i++) {
        uint32_t bigintCarry = 0;
        newBigint->SetDigit(i, BigIntHelper::AddHelper(bigint->GetDigit(i), carry, bigintCarry));
        carry = bigintCarry;
    }
    if (needExpend) {
        newBigint->SetDigit(bigintLength, carry);
    } else {
        ASSERT(!carry);
    }
    newBigint->SetSign(true);
    return BigIntHelper::RightTruncate(thread, newBigint);
}

JSHandle<BigInt> OneIsNegativeOR(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    uint32_t xLength = x->GetLength();
    uint32_t maxLen = xLength;
    if (maxLen < y->GetLength()) {
        maxLen = y->GetLength();
    }
    JSHandle<BigInt> yVal = BigInt::BitwiseSubOne(thread, y, maxLen);
    uint32_t yLength = yVal->GetLength();
    uint32_t minLen = xLength;
    if (minLen > yLength) {
        minLen = yLength;
    }
    JSHandle<BigInt> newBigint = BigInt::CreateBigint(thread, yLength);
    uint32_t i = 0;
    while (i < minLen) {
        uint32_t res = ~(x->GetDigit(i)) & yVal->GetDigit(i);
        newBigint->SetDigit(i, res);
        ++i;
    }
    while (i < yLength) {
        newBigint->SetDigit(i, yVal->GetDigit(i));
        ++i;
    }
    JSHandle<BigInt> temp = BigIntHelper::RightTruncate(thread, newBigint);
    JSHandle<BigInt> res = BigInt::BitwiseAddOne(thread, temp);
    res->SetSign(true);
    return res;
}

// 6.1.6.2.22 BigInt::bitwiseOR ( x, y )
JSHandle<BigInt> BigInt::BitwiseOR(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    if (x->GetSign() && y->GetSign()) {
        // (-x) | (-y) == -(((x-1) & (y-1)) + 1)
        uint32_t maxLen = x->GetLength();
        uint32_t yLen = y->GetLength();
        maxLen < yLen ? maxLen = yLen : 0;
        JSHandle<BigInt> xVal = BitwiseSubOne(thread, x, maxLen);
        JSHandle<BigInt> yVal = BitwiseSubOne(thread, y, yLen);
        JSHandle<BigInt> temp = BitwiseOp(thread, Operate::AND, xVal, yVal);
        JSHandle<BigInt> res = BitwiseAddOne(thread, temp);
        res->SetSign(true);
        return res;
    }
    if (x->GetSign() != y->GetSign()) {
        // x | (-y) == -(((y-1) & ~x) + 1)
        if (!x->GetSign()) {
            return OneIsNegativeOR(thread, x, y);
        } else {
            return OneIsNegativeOR(thread, y, x);
        }
    }
    return BitwiseOp(thread, Operate::OR, x, y);
}

// 6.1.6.2.23 BigInt::toString ( x )
JSHandle<EcmaString> BigInt::ToString(JSThread *thread, JSHandle<BigInt> bigint, uint32_t conversionToRadix)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    CString result = bigint->ToStdString(conversionToRadix);
    return factory->NewFromASCII(result.c_str());
}

CString BigInt::ToStdString(uint32_t conversionToRadix) const
{
    CString result =
        BigIntHelper::Conversion(BigIntHelper::GetBinary(this), conversionToRadix, BINARY);
    if (GetSign()) {
        result = "-" + result;
    }
    return result;
}

JSTaggedValue BigInt::NumberToBigInt(JSThread *thread, JSHandle<JSTaggedValue> number)
{
    if (!number->IsInteger()) {
        THROW_RANGE_ERROR_AND_RETURN(thread, "The number cannot be converted to a BigInt because it is not an integer",
                                     JSTaggedValue::Exception());
    }
    double num = number->GetNumber();
    if (num == 0.0) {
        return Int32ToBigInt(thread, 0).GetTaggedValue();
    }

    // Bit operations must be of integer type
    uint64_t bits = 0;
    if (memcpy_s(&bits, sizeof(bits), &num, sizeof(num)) != EOK) {
        LOG_FULL(FATAL) << "memcpy_s failed";
        UNREACHABLE();
    }
    // Take out bits 62-52 (11 bits in total) and subtract 1023
    uint64_t integerDigits = ((bits >> base::DOUBLE_SIGNIFICAND_SIZE) & 0x7FF) - base::DOUBLE_EXPONENT_BIAS;
    uint32_t mayNeedLen = integerDigits / DATEBITS + 1;

    JSHandle<BigInt> bigint = CreateBigint(thread, mayNeedLen);
    bigint->SetSign(num < 0);
    uint64_t mantissa = (bits & base::DOUBLE_SIGNIFICAND_MASK) | base::DOUBLE_HIDDEN_BIT;
    int mantissaSize = base::DOUBLE_SIGNIFICAND_SIZE;

    int leftover = 0;
    bool isFirstInto = true;
    for (int index = static_cast<int>(mayNeedLen - 1); index >= 0; --index) {
        uint32_t doubleNum = 0;
        if (isFirstInto) {
            isFirstInto = false;
            leftover = mantissaSize - static_cast<int>(integerDigits % DATEBITS);
            doubleNum = static_cast<uint32_t>(mantissa >> leftover);
            mantissa = mantissa << (64 - leftover); // 64 : double bits size
            bigint->SetDigit(index, doubleNum);
        } else {
            leftover -= DATEBITS;
            doubleNum = static_cast<uint32_t>(mantissa >> DATEBITS);
            mantissa = mantissa << DATEBITS;
            bigint->SetDigit(index, doubleNum);
        }
    }
    return BigIntHelper::RightTruncate(thread, bigint).GetTaggedValue();
}

JSHandle<BigInt> BigInt::Int32ToBigInt(JSThread *thread, const int &number)
{
    JSHandle<BigInt> bigint = CreateBigint(thread, 1);
    uint32_t value = 0;
    bool sign = number < 0;
    if (sign) {
        value = static_cast<uint32_t>(-(number + 1)) + 1;
    } else {
        value = number;
    }
    bigint->SetDigit(0, value);
    bigint->SetSign(sign);
    return bigint;
}

JSHandle<BigInt> BigInt::Int64ToBigInt(JSThread *thread, const int64_t &number)
{
    uint64_t value = 0;
    bool sign = number < 0;
    if (sign) {
        value = static_cast<uint64_t>(-(number + 1)) + 1;
    } else {
        value = number;
    }
    JSHandle<BigInt> bigint = Uint64ToBigInt(thread, value);
    bigint->SetSign(sign);
    return BigIntHelper::RightTruncate(thread, bigint);
}

JSHandle<BigInt> BigInt::Uint64ToBigInt(JSThread *thread, const uint64_t &number)
{
    JSHandle<BigInt> bigint = CreateBigint(thread, 2); // 2 : one int64_t bits need two uint32_t bits
    uint32_t lowBits = static_cast<uint32_t>(number & 0xffffffff);
    uint32_t highBits = static_cast<uint32_t>((number >> DATEBITS) & 0xffffffff);
    bigint->SetDigit(0, lowBits);
    bigint->SetDigit(1, highBits);
    return BigIntHelper::RightTruncate(thread, bigint);
}

uint64_t BigInt::ToUint64()
{
    uint32_t len = GetLength();
    ASSERT(len <= 2); // The maximum length of the BigInt data is less or equal 2
    uint32_t lowBits = GetDigit(0);
    uint32_t highBits = 0;
    if (len > 1) {
        highBits = GetDigit(1);
    }
    uint64_t value = static_cast<uint64_t>(lowBits);
    value |= static_cast<uint64_t>(highBits) << DATEBITS;
    if (GetSign()) {
        value = ~(value - 1);
    }
    return value;
}

int64_t BigInt::ToInt64()
{
    return static_cast<int64_t>(ToUint64());
}

void BigInt::BigIntToInt64(JSThread *thread, JSHandle<JSTaggedValue> bigint, int64_t *cValue, bool *lossless)
{
    ASSERT(cValue != nullptr);
    ASSERT(lossless != nullptr);
    JSHandle<BigInt> bigInt64(thread, JSTaggedValue::ToBigInt64(thread, bigint));
    RETURN_IF_ABRUPT_COMPLETION(thread);
    if (Equal(bigInt64.GetTaggedValue(), bigint.GetTaggedValue())) {
        *lossless = true;
    }
    *cValue = bigInt64->ToInt64();
}

void BigInt::BigIntToUint64(JSThread *thread, JSHandle<JSTaggedValue> bigint, uint64_t *cValue, bool *lossless)
{
    ASSERT(cValue != nullptr);
    ASSERT(lossless != nullptr);
    JSHandle<BigInt> bigUint64(thread, JSTaggedValue::ToBigUint64(thread, bigint));
    RETURN_IF_ABRUPT_COMPLETION(thread);
    if (Equal(bigUint64.GetTaggedValue(), bigint.GetTaggedValue())) {
        *lossless = true;
    }
    *cValue = bigUint64->ToUint64();
}

JSHandle<BigInt> BigInt::CreateBigWords(JSThread *thread, bool sign, uint32_t size, const uint64_t *words)
{
    ASSERT(words != nullptr);
    const uint32_t MULTIPLE = 2;
    uint32_t needLen = size * MULTIPLE;
    JSHandle<BigInt> bigint = CreateBigint(thread, needLen);
    for (uint32_t index = 0; index < size; ++index) {
        uint32_t lowBits = static_cast<uint32_t>(words[index] & 0xffffffff);
        uint32_t highBits = static_cast<uint32_t>((words[index] >> DATEBITS) & 0xffffffff);
        bigint->SetDigit(MULTIPLE * index, lowBits);
        bigint->SetDigit(MULTIPLE * index + 1, highBits);
    }
    bigint->SetSign(sign);
    return BigIntHelper::RightTruncate(thread, bigint);
}

JSHandle<BigInt> BigInt::Add(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    bool xSignFlag = x->GetSign();
    bool ySignFlag = y->GetSign();
    // x + y == x + y
    // -x + -y == -(x + y)
    if (xSignFlag == ySignFlag) {
        return BigintAdd(thread, x, y, xSignFlag);
    }
    // x + -y == x - y == -(y - x)
    // -x + y == y - x == -(x - y)
    uint32_t xLength = x->GetLength();
    uint32_t yLength = y->GetLength();
    uint32_t i = xLength - 1;
    int subSize = static_cast<int>(xLength - yLength);
    if (subSize > 0) {
        return BigintSub(thread, x, y, xSignFlag);
    } else if (subSize == 0) {
        while (i > 0 && x->GetDigit(i) == y->GetDigit(i)) {
            i--;
        }
        if ((x->GetDigit(i) > y->GetDigit(i))) {
            return BigintSub(thread, x, y, xSignFlag);
        } else {
            return BigintSub(thread, y, x, ySignFlag);
        }
    } else {
        return BigintSub(thread, y, x, ySignFlag);
    }
}
JSHandle<BigInt> BigInt::Subtract(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    bool xSignFlag = x->GetSign();
    bool ySignFlag = y->GetSign();
    if (xSignFlag != ySignFlag) {
        // x - (-y) == x + y
        // (-x) - y == -(x + y)
        return BigintAdd(thread, x, y, xSignFlag);
    }
    // x - y == -(y - x)
    // (-x) - (-y) == y - x == -(x - y)
    uint32_t xLength = x->GetLength();
    uint32_t yLength = y->GetLength();
    uint32_t i = xLength - 1;
    int subSize = static_cast<int>(xLength - yLength);
    if (subSize > 0) {
        return BigintSub(thread, x, y, xSignFlag);
    } else if (subSize == 0) {
        while (i > 0 && x->GetDigit(i) == y->GetDigit(i)) {
            i--;
        }
        if ((x->GetDigit(i) > y->GetDigit(i))) {
            return BigintSub(thread, x, y, xSignFlag);
        } else {
            return BigintSub(thread, y, x, !ySignFlag);
        }
    } else {
        return BigintSub(thread, y, x, !ySignFlag);
    }
}

JSHandle<BigInt> BigInt::BigintAdd(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y, bool resultSign)
{
    if (x->GetLength() < y->GetLength()) {
        return BigintAdd(thread, y, x, resultSign);
    }
    JSHandle<BigInt> bigint = BigInt::CreateBigint(thread, x->GetLength() + 1);
    uint32_t bigintCarry = 0;
    uint32_t i = 0;
    while (i < y->GetLength()) {
        uint32_t newBigintCarry = 0;
        uint32_t addPlus = BigIntHelper::AddHelper(x->GetDigit(i), y->GetDigit(i), newBigintCarry);
        addPlus = BigIntHelper::AddHelper(addPlus, bigintCarry, newBigintCarry);
        bigint->SetDigit(i, addPlus);
        bigintCarry = newBigintCarry;
        i++;
    }
    while (i < x->GetLength()) {
        uint32_t newBigintCarry = 0;
        uint32_t addPlus = BigIntHelper::AddHelper(x->GetDigit(i), bigintCarry, newBigintCarry);
        bigint->SetDigit(i, addPlus);
        bigintCarry = newBigintCarry;
        i++;
    }
    bigint->SetDigit(i, bigintCarry);
    bigint->SetSign(resultSign);
    return BigIntHelper::RightTruncate(thread, bigint);
}

inline uint32_t BigIntHelper::AddHelper(uint32_t x, uint32_t y, uint32_t &bigintCarry)
{
    uint32_t addPlus = x + y;
    if (addPlus < x) {
        bigintCarry += 1;
    }
    return addPlus;
}

JSHandle<BigInt> BigInt::BigintSub(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y, bool resultSign)
{
    JSHandle<BigInt> bigint = BigInt::CreateBigint(thread, x->GetLength());
    uint32_t bigintCarry = 0;
    uint32_t i = 0;
    while (i < y->GetLength()) {
        uint32_t newBigintCarry = 0;
        uint32_t minuSub = BigIntHelper::SubHelper(x->GetDigit(i), y->GetDigit(i), newBigintCarry);
        minuSub = BigIntHelper::SubHelper(minuSub, bigintCarry, newBigintCarry);
        bigint->SetDigit(i, minuSub);
        bigintCarry = newBigintCarry;
        i++;
    }
    while (i < x->GetLength()) {
        uint32_t newBigintCarry = 0;
        uint32_t minuSub = BigIntHelper::SubHelper(x->GetDigit(i), bigintCarry, newBigintCarry);
        bigint->SetDigit(i, minuSub);
        bigintCarry = newBigintCarry;
        i++;
    }
    bigint->SetSign(resultSign);
    return BigIntHelper::RightTruncate(thread, bigint);
}

JSHandle<BigInt> BigInt::BigintAddOne(JSThread *thread, JSHandle<BigInt> x)
{
    JSHandle<BigInt> temp = Int32ToBigInt(thread, 1);
    return Add(thread, x, temp);
}

JSHandle<BigInt> BigInt::BigintSubOne(JSThread *thread, JSHandle<BigInt> x)
{
    JSHandle<BigInt> temp = Int32ToBigInt(thread, 1);
    return Subtract(thread, x, temp);
}

inline uint32_t BigIntHelper::SubHelper(uint32_t x, uint32_t y, uint32_t &bigintCarry)
{
    uint32_t minuSub = x - y;
    if (minuSub > x) {
        bigintCarry += 1;
    }
    return minuSub;
}

ComparisonResult BigInt::Compare(const JSTaggedValue &x, const JSTaggedValue &y)
{
    if (!LessThan(x, y)) {
        if (!LessThan(y, x)) {
            return ComparisonResult::EQUAL;
        }
        return ComparisonResult::GREAT;
    }
    return ComparisonResult::LESS;
}

bool BigInt::LessThan(const JSTaggedValue &x, const JSTaggedValue &y)
{
    BigInt* xVal = BigInt::Cast(x.GetTaggedObject());
    BigInt* yVal = BigInt::Cast(y.GetTaggedObject());
    return LessThan(xVal, yVal);
}

bool BigInt::LessThan(const BigInt *x, const BigInt *y)
{
    ASSERT(x != nullptr);
    ASSERT(y != nullptr);
    bool xSignFlag = x->GetSign();
    bool ySignFlag = y->GetSign();
    int minSize = static_cast<int>(x->GetLength() - y->GetLength());
    uint32_t i = x->GetLength() - 1;
    if (xSignFlag != ySignFlag) {
        return xSignFlag ? true : false;
    } else {
        if (minSize != 0 && xSignFlag) {
            return minSize > 0 ? true : false;
        }
        if (minSize != 0 && !xSignFlag) {
            return minSize > 0 ? false : true;
        }
        while (i > 0 && x->GetDigit(i) == y->GetDigit(i)) {
            i--;
        }
        if ((x->GetDigit(i) > y->GetDigit(i))) {
            return xSignFlag ? true : false;
        } else if ((x->GetDigit(i) < y->GetDigit(i))) {
            return !xSignFlag ? true : false;
        } else {
            return false;
        }
    }
}

JSHandle<BigInt> BigInt::SignedRightShift(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    bool xIsNull = x->GetDigit(0);
    bool yIsNull = y->GetDigit(0);
    if (!xIsNull || !yIsNull) {
        return x;
    }
    if (y->GetSign()) {
        return LeftShiftHelper(thread, x, y);
    } else {
        return RightShiftHelper(thread, x, y);
    }
}

JSHandle<BigInt> BigInt::RightShiftHelper(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    CString shiftBinay = BigIntHelper::GetBinary(*x);
    CString revTemp = CString(shiftBinay.rbegin(), shiftBinay.rend());
    for (uint32_t i = 0; i < y->GetLength(); i++) {
        revTemp = revTemp.erase(0, y->GetDigit(i));
    }
    CString finalBinay = CString(revTemp.rbegin(), revTemp.rend());
    if (finalBinay.empty()) {
        finalBinay = "0";
    }
    JSHandle<BigInt> bigint = BigIntHelper::SetBigInt(thread, finalBinay, BINARY);
    if (x->GetSign()) {
        bigint->SetDigit(0, bigint->GetDigit(0) + 1);
    }
    bigint->SetSign(x->GetSign());
    return BigIntHelper::RightTruncate(thread, bigint);
}

JSHandle<BigInt> BigInt::LeftShift(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    if (y->GetSign()) {
        return RightShiftHelper(thread, x, y);
    } else {
        return LeftShiftHelper(thread, x, y);
    }
}

JSHandle<BigInt> BigInt::LeftShiftHelper(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    CString shiftBinary = BigIntHelper::GetBinary(*x);
    for (size_t i = 0; i < y->GetLength(); i++) {
        shiftBinary = shiftBinary.append(y->GetDigit(i), '0');
    }
    JSHandle<BigInt> bigint = BigIntHelper::SetBigInt(thread, shiftBinary, BINARY);
    bigint->SetSign(x->GetSign());
    return BigIntHelper::RightTruncate(thread, bigint);
}

JSTaggedValue BigInt::UnsignedRightShift(JSThread *thread)
{
    THROW_TYPE_ERROR_AND_RETURN(thread, "BigInt have no unsigned right shift, use >> instead",
                                JSTaggedValue::Exception());
}

JSHandle<BigInt> BigInt::Copy(JSThread *thread, JSHandle<BigInt> x, uint32_t len)
{
    ASSERT(x->GetLength() >= len);
    JSHandle<BigInt> newBig = CreateBigint(thread, len);
    std::copy(x->GetData(), x->GetData() + len, newBig->GetData());
    newBig->SetSign(x->GetSign());
    return newBig;
}

JSHandle<BigInt> BigInt::UnaryMinus(JSThread *thread, JSHandle<BigInt> x)
{
    if (x->IsZero()) {
        return x;
    }
    JSHandle<BigInt> y = Copy(thread, x, x->GetLength());
    y->SetSign(!y->GetSign());
    return y;
}

// 6.1.6.2.2   BigInt::bitwiseNOT ( x )
JSHandle<BigInt> BigInt::BitwiseNOT(JSThread *thread, JSHandle<BigInt> x)
{
    // ~(-x) == ~(~(x-1)) == x-1
    // ~x == -x-1 == -(x+1)
    JSHandle<BigInt> result = BigintAddOne(thread, x);
    if (x->GetSign()) {
        result->SetSign(false);
    } else {
        result->SetSign(true);
    }
    return result;
}

JSHandle<BigInt> BigInt::Exponentiate(JSThread *thread, JSHandle<BigInt> base, JSHandle<BigInt> exponent)
{
    if (exponent->GetSign()) {
        JSHandle<BigInt> bigint(thread, JSTaggedValue::Exception());
        THROW_RANGE_ERROR_AND_RETURN(thread, "Exponent must be positive", bigint);
    }
    ASSERT(exponent->GetLength() == 1);
    if (exponent->IsZero()) {
        return BigIntHelper::SetBigInt(thread, "1");
    }
    
    if (base->IsZero()) {
        return BigIntHelper::SetBigInt(thread, "0");
    }
    uint32_t EValue = exponent->GetDigit(0);
    if (EValue == 1) {
        return base;
    }
    uint32_t j = exponent->GetDigit(0) - 1;
    CString a = BigIntHelper::GetBinary(*base);
    a = BigIntHelper::Conversion(a, DECIMAL, BINARY);
    CString b = a;
    for (uint32_t i = 0; i < j; ++i) {
        b = BigIntHelper::MultiplyImpl(b, a);
    }
    if (exponent->GetDigit(0) & 1) {
        if (base->GetSign()) {
            b = "-" + b;
        }
    }
    return BigIntHelper::SetBigInt(thread, b, DECIMAL);
}

CString BigIntHelper::MultiplyImpl(CString &a, CString &b)
{
    int size1 = static_cast<int>(a.size());
    int size2 = static_cast<int>(b.size());
    CString str(size1 + size2, '0');
    for (int i = size2 - 1; i >= 0; --i) {
        int mulflag = 0;
        int addflag = 0;
        for (int j = size1 - 1; j >= 0; --j) {
            int temp1 = static_cast<int>((b[i] - '0') * (a[j] - '0')) + mulflag;
            mulflag = temp1 / 10; // 10:help to Remove single digits
            temp1 = temp1 % 10; // 10:help to Take single digit
            int temp2 = static_cast<int>(str[i + j + 1] - '0') + temp1 + addflag;
            str[i + j + 1] = static_cast<int8_t>(temp2 % 10 + '0'); // 2 and 10 and 48 is number
            addflag = temp2 / 10;
        }
        str[i] += static_cast<uint32_t>(mulflag + addflag);
    }
    if (str[0] == '0') {
        str = str.substr(1, str.size());
    }
    return str;
}

JSHandle<BigInt> BigInt::Multiply(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    if (x->IsZero()) {
        return x;
    }
    if (y->IsZero()) {
        return y;
    }
    CString left = BigIntHelper::GetBinary(*x);
    CString right = BigIntHelper::GetBinary(*y);
    left = BigIntHelper::Conversion(left, DECIMAL, BINARY);
    right = BigIntHelper::Conversion(right, DECIMAL, BINARY);

    CString ab = BigIntHelper::MultiplyImpl(left, right);
    if (x->GetSign() != y->GetSign()) {
        ab = "-" + ab;
    }
    return BigIntHelper::SetBigInt(thread, ab, DECIMAL);
}

void BigIntHelper::DeZero(CString &a)
{
    size_t count = 0;
    while (count < a.size() && a[count] == '0') {
        count++;
    }
    if (count == a.size()) {
        a = "0";
    } else {
        a = a.substr(count);
    }
}

Comparestr BigInt::ComString(const CString &a, const CString &b)
{
    if (a.length() > b.length()) {
        return Comparestr::GREATER;
    }
    if (a.length() < b.length()) {
        return Comparestr::LESS;
    }
    for (size_t i = 0; i < a.length(); i++) {
        if (a[i] > b[i]) {
            return Comparestr::GREATER;
        }
        if (a[i] < b[i]) {
            return Comparestr::LESS;
        }
    }
    return Comparestr::EQUAL;
}

void BigIntHelper::Minus(CString &a, CString &b)
{
    int i = static_cast<int>(a.size() - 1);
    int j = static_cast<int>(b.size() - 1);
    int carry = 0;
    int sum = 0;
    while (i >= 0 || j >= 0 || carry != 0) {
        sum = (i >= 0 ? a[i] - '0' : 0) - (j >= 0 ? b[j] - '0' : 0) - carry;
        if (sum < 0) {
            carry = 1;
            sum += 10; // 10:means borrow 1 from high
        } else {
            carry = 0;
        }
        a[i] = sum % 10 + '0'; // 10:To get single digits
        i--;
        j--;
    }
    DeZero(a);
}

CString BigIntHelper::Divide(CString &a, CString &b)
{
    CString dsy = "";
    CString quotient = "";
    if (BigInt::ComString(a, b) == Comparestr::EQUAL) {
        return "1";
    }
    if (BigInt::ComString(a, b) == Comparestr::LESS) {
        return "0";
    }
    for (size_t i = 0; i < a.length(); i++) {
        dsy += a[i];
        DeZero(dsy);
        int j = 0;
        while (!(BigInt::ComString(dsy, b) == Comparestr::LESS)) {
            Minus(dsy, b);
            j++;
        }
        quotient += (static_cast<int8_t>(j + '0'));
    }
    DeZero(quotient);
    return quotient;
}

JSHandle<BigInt> BigIntHelper::DivideImpl(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    CString a = Conversion(GetBinary(*x), BigInt::DECIMAL, BigInt::BINARY);
    CString b = Conversion(GetBinary(*y), BigInt::DECIMAL, BigInt::BINARY);
    CString ab = Divide(a, b);
    if (x->GetSign() != y->GetSign()) {
        ab = "-" + ab;
    }
    return SetBigInt(thread, ab, BigInt::DECIMAL);
}

JSHandle<BigInt> BigInt::Divide(JSThread *thread, JSHandle<BigInt> x, JSHandle<BigInt> y)
{
    if (y->IsZero()) {
        JSHandle<BigInt> bigint(thread, JSTaggedValue::Exception());
        THROW_RANGE_ERROR_AND_RETURN(thread, "Division by zero", bigint);
    }
    return BigIntHelper::DivideImpl(thread, x, y);
}


JSHandle<BigInt> BigInt::Remainder(JSThread *thread, JSHandle<BigInt> n, JSHandle<BigInt> d)
{
    if (d->IsZero()) {
        JSHandle<BigInt> bigint(thread, JSTaggedValue::Exception());
        THROW_RANGE_ERROR_AND_RETURN(thread, "Division by zero", bigint);
    }
    if (n->IsZero()) {
        return n;
    }
    JSHandle<BigInt> q = Divide(thread, n, d);
    JSHandle<BigInt> p = Multiply(thread, q, d);
    return Subtract(thread, n, p);
}

JSHandle<BigInt> BigInt::FloorMod(JSThread *thread, JSHandle<BigInt> leftVal, JSHandle<BigInt> rightVal)
{
    if (leftVal->GetSign()) {
        JSHandle<BigInt> quotientVal = Divide(thread, leftVal, rightVal);
        if (quotientVal->IsZero()) {
            return Add(thread, leftVal, rightVal);
        } else {
            JSHandle<BigInt> num = Multiply(thread, quotientVal, rightVal);
            if (Equal(*num, *leftVal)) {
                return Int32ToBigInt(thread, 0);
            } else {
                return Subtract(thread, leftVal, Subtract(thread, num, rightVal));
            }
        }
    }
    return Remainder(thread, leftVal, rightVal);
}

JSTaggedValue BigInt::AsUintN(JSThread *thread, JSTaggedNumber &bits, JSHandle<BigInt> bigint)
{
    uint32_t bit = bits.ToUint32();
    if (bit == 0) {
        return Int32ToBigInt(thread, 0).GetTaggedValue();
    }
    if (bigint->IsZero()) {
        return bigint.GetTaggedValue();
    }
    JSHandle<BigInt> exponent = Int32ToBigInt(thread, bit);
    JSHandle<BigInt> base = Int32ToBigInt(thread, 2); // 2 : base value
    JSHandle<BigInt> tValue = Exponentiate(thread, base, exponent);
    return FloorMod(thread, bigint, tValue).GetTaggedValue();
}

JSTaggedValue BigInt::AsintN(JSThread *thread, JSTaggedNumber &bits, JSHandle<BigInt> bigint)
{
    uint32_t bit = bits.ToUint32();
    if (bit == 0) {
        return Int32ToBigInt(thread, 0).GetTaggedValue();
    }
    if (bigint->IsZero()) {
        return bigint.GetTaggedValue();
    }
    JSHandle<BigInt> exp = Int32ToBigInt(thread, bit);
    JSHandle<BigInt> exponent = Int32ToBigInt(thread, bit - 1);
    JSHandle<BigInt> base = Int32ToBigInt(thread, 2); // 2 : base value
    JSHandle<BigInt> tValue = Exponentiate(thread, base, exp);
    JSHandle<BigInt> modValue =  FloorMod(thread, bigint, tValue);
    JSHandle<BigInt> resValue = Exponentiate(thread, base, exponent);
    // If mod ≥ 2bits - 1, return ℤ(mod - 2bits); otherwise, return (mod).
    if (LessThan(*resValue, *modValue) || Equal(*resValue, *modValue)) {
        return Subtract(thread, modValue, tValue).GetTaggedValue();
    }
    return modValue.GetTaggedValue();
}

static JSTaggedNumber CalculateNumber(const uint64_t &sign, const uint64_t &mantissa, uint64_t &exponent)
{
    exponent = (exponent + base::DOUBLE_EXPONENT_BIAS) << base::DOUBLE_SIGNIFICAND_SIZE;
    uint64_t doubleBit = sign | exponent | mantissa;
    double res = 0;
    if (memcpy_s(&res, sizeof(res), &doubleBit, sizeof(doubleBit)) != EOK) {
        LOG_FULL(FATAL) << "memcpy_s failed";
        UNREACHABLE();
    }
    return JSTaggedNumber(res);
}

static JSTaggedNumber Rounding(const uint64_t &sign, uint64_t &mantissa, uint64_t &exponent, bool needRound)
{
    if (needRound || (mantissa & 1) == 1) {
        ++mantissa;
        if ((mantissa >> base::DOUBLE_SIGNIFICAND_SIZE) != 0) {
            mantissa = 0;
            exponent++;
            if (exponent > base::DOUBLE_EXPONENT_BIAS)
                return JSTaggedNumber(sign ? -base::POSITIVE_INFINITY : base::POSITIVE_INFINITY);
        }
    }
    return CalculateNumber(sign, mantissa, exponent);
}

JSTaggedNumber BigInt::BigIntToNumber(JSHandle<BigInt> bigint)
{
    if (bigint->IsZero()) {
        return JSTaggedNumber(0);
    }
    uint32_t bigintLen = bigint->GetLength();
    uint32_t BigintHead = bigint->GetDigit(bigintLen - 1);
    uint32_t bits = BigInt::DATEBITS;
    int preZero = 0;
    while (bits--) {
        if (((BigintHead >> bits) | 0) != 0) {
            break;
        }
        preZero++;
    }
    int bigintBitLen = static_cast<int>(bigintLen * BigInt::DATEBITS) - preZero;
    // if Significant bits greater than 1024 then double is infinity
    bool bigintSign = bigint->GetSign();
    if (bigintBitLen > (base::DOUBLE_EXPONENT_BIAS + 1)) {
        return JSTaggedNumber(bigintSign ? -base::POSITIVE_INFINITY : base::POSITIVE_INFINITY);
    }
    uint64_t sign = bigintSign ? 1ULL << 63 : 0; // 63 : Set the sign bit of sign to 1
    int needMoveBit = preZero + BigInt::DATEBITS + 1;
    // Align to the most significant bit, then right shift 12 bits so that the head of the mantissa is in place
    uint64_t mantissa = (static_cast<uint64_t>(BigintHead) << needMoveBit) >> 12; // 12 mantissa just has 52 bits
    int remainMantissaBits = needMoveBit - 12;
    uint64_t exponent = static_cast<uint64_t>(bigintBitLen - 1);
    int index = static_cast<int>(bigintLen - 1);
    uint32_t digit = 0;
    if (index > 0) {
        digit = bigint->GetDigit(--index);
    } else {
        return CalculateNumber(sign, mantissa, exponent);
    }
    // pad unset mantissa
    if (static_cast<uint32_t>(remainMantissaBits) >= BigInt::DATEBITS) {
        mantissa |= (static_cast<uint64_t>(digit) << (remainMantissaBits - BigInt::DATEBITS));
        remainMantissaBits -= BigInt::DATEBITS;
        index--;
    }
    if (remainMantissaBits > 0 && index >= 0) {
        digit = bigint->GetDigit(index);
        mantissa |= (static_cast<uint64_t>(digit) >> (BigInt::DATEBITS - remainMantissaBits));
        remainMantissaBits -= BigInt::DATEBITS;
    }
    // After the mantissa is filled, if the bits of bigint have not been used up, consider the rounding problem
    // The remaining bits of the current digit
    if (remainMantissaBits > 0) {
        return CalculateNumber(sign, mantissa, exponent);
    }
    int remainDigitBits = 0;
    if (remainMantissaBits < 0) {
        remainDigitBits = -remainMantissaBits;
    } else {
        if (!index) {
            return CalculateNumber(sign, mantissa, exponent);
        }
        digit = bigint->GetDigit(index--);
        remainDigitBits = BigInt::DATEBITS;
    }
    uint32_t temp = 1ULL << (remainDigitBits - 1);
    if (!(digit & temp)) {
        return CalculateNumber(sign, mantissa, exponent);
    }
    if ((digit & (temp - 1)) != 0) {
        return Rounding(sign, mantissa, exponent, true);
    }
    while (index > 0) {
        if (bigint->GetDigit(index--) != 0) {
            return Rounding(sign, mantissa, exponent, true);
        }
    }
    return Rounding(sign, mantissa, exponent, false);
}

static int CompareToBitsLen(JSHandle<BigInt> bigint, int numBitLen, int &preZero)
{
    uint32_t bigintLen = bigint->GetLength();
    uint32_t BigintHead = bigint->GetDigit(bigintLen - 1);
    uint32_t bits = BigInt::DATEBITS;
    while (bits) {
        bits--;
        if (((BigintHead >> bits) | 0) != 0) {
            break;
        }
        preZero++;
    }
    
    int bigintBitLen = static_cast<int>(bigintLen * BigInt::DATEBITS) - preZero;
    bool bigintSign = bigint->GetSign();
    if (bigintBitLen > numBitLen) {
        return bigintSign ? 0 : 1;
    }

    if (bigintBitLen < numBitLen) {
        return bigintSign ? 1 : 0;
    }
    return -1;
}

ComparisonResult BigInt::CompareWithNumber(JSHandle<BigInt> bigint, JSHandle<JSTaggedValue> number)
{
    double num = number->GetNumber();
    bool numberSign = num < 0;
    if (std::isnan(num)) {
        return ComparisonResult::UNDEFINED;
    }
    if (!std::isfinite(num)) {
        return (!numberSign ?  ComparisonResult::LESS : ComparisonResult::GREAT);
    }
    // Bit operations must be of integer type
    uint64_t bits = 0;
    if (memcpy_s(&bits, sizeof(bits), &num, sizeof(num)) != EOK) {
        LOG_FULL(FATAL) << "memcpy_s failed";
        UNREACHABLE();
    }
    int exponential = (bits >> base::DOUBLE_SIGNIFICAND_SIZE) & 0x7FF;

    // Take out bits 62-52 (11 bits in total) and subtract 1023
    int integerDigits = exponential - base::DOUBLE_EXPONENT_BIAS;
    uint64_t mantissa = (bits & base::DOUBLE_SIGNIFICAND_MASK) | base::DOUBLE_HIDDEN_BIT;
    bool bigintSign = bigint->GetSign();
    // Handling the opposite sign
    if (!numberSign && bigintSign) {
        return ComparisonResult::LESS;
    } else if (numberSign && !bigintSign) {
        return ComparisonResult::GREAT;
    }
    if (bigint->IsZero() && !num) {
        return ComparisonResult::EQUAL;
    }
    if (bigint->IsZero() && num > 0) {
        return ComparisonResult::LESS;
    }

    if (integerDigits < 0) {
        return bigintSign ? ComparisonResult::LESS : ComparisonResult::GREAT;
    }

    // Compare the significant bits of bigint with the significant integer bits of double
    int preZero = 0;
    int res =  CompareToBitsLen(bigint, integerDigits + 1, preZero);
    if (res == 0) {
        return ComparisonResult::LESS;
    } else if (res == 1) {
        return ComparisonResult::GREAT;
    }
    int mantissaSize = base::DOUBLE_SIGNIFICAND_SIZE; // mantissaSize
    uint32_t bigintLen = bigint->GetLength();
    int leftover = 0;
    bool IsFirstInto = true;
    for (int index = static_cast<int>(bigintLen - 1); index >= 0; --index) {
        uint32_t doubleNum = 0;
        uint32_t BigintNum = bigint->GetDigit(index);
        if (IsFirstInto) {
            IsFirstInto = false;
            leftover = mantissaSize - BigInt::DATEBITS + preZero + 1;
            doubleNum = static_cast<uint32_t>(mantissa >> leftover);
            mantissa = mantissa << (64 - leftover); // 64 : double bits
            if (BigintNum > doubleNum) {
                return bigintSign ? ComparisonResult::LESS : ComparisonResult::GREAT;
            }
            if (BigintNum < doubleNum) {
                return bigintSign ? ComparisonResult::GREAT : ComparisonResult::LESS;
            }
        } else {
            leftover -= BigInt::DATEBITS;
            doubleNum = static_cast<uint32_t>(mantissa >> BigInt::DATEBITS);
            mantissa = mantissa << BigInt::DATEBITS;
            if (BigintNum > doubleNum) {
                return bigintSign ? ComparisonResult::LESS : ComparisonResult::GREAT;
            }
            if (BigintNum < doubleNum) {
                return bigintSign ? ComparisonResult::GREAT : ComparisonResult::LESS;
            }
            leftover -= BigInt::DATEBITS;
        }
    }

    if (mantissa != 0) {
        ASSERT(leftover > 0);
        return bigintSign ? ComparisonResult::GREAT : ComparisonResult::LESS;
    }
    return ComparisonResult::EQUAL;
}
} // namespace
