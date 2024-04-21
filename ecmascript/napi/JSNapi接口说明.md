## ArrayBufferRef

ArrayBufferRef ��һ��ͨ�õġ��̶����ȵ�ԭʼ���������ݻ�������������ֱ�Ӷ�ȡ���������Ҫͨ����������� DataView �������������е����ݡ�

### New

Local<ArrayBufferRef> ArrayBufferRef::New(const EcmaVM *vm, int32_t length)��

Local<ArrayBufferRef> ArrayBufferRef::New(const EcmaVM *vm, void *buffer, int32_t length, const NativePointerCallback &deleter, void *data)��

����һ��ArrayBuffer����

**������**

| ������  | ����            | ���� | ˵��                        |
| ------- | --------------- | ---- | --------------------------- |
| vm      | const EcmaVM *  | ��   | ���������                |
| length  | int32_t         | ��   | ָ���ĳ��ȡ�                |
| buffer  | void *          | ��   | ָ����������                |
| deleter | const NativePointerCallback & | ��   | ɾ��ArrayBufferʱ�����Ĳ��� |
| data    | void *          | ��   | ָ�����ݡ�                  |

**����ֵ��**

| ����                  | ˵��                             |
| --------------------- | -------------------------------- |
| Local<ArrayBufferRef> | �����´�����ArrayBufferRef���� |

**ʾ����**

```C++
Local<ArrayBufferRef> arrayBuffer1 = ArrayBufferRef::New(vm, 10);
uint8_t *buffer = new uint8_t[10]();
int *data = new int;
*data = 10;
NativePointerCallback deleter = [](void *env, void *buffer, void *data) -> void {
    delete[] reinterpret_cast<uint8_t *>(buffer);
    int *currentData = reinterpret_cast<int *>(data);
    delete currentData;
};
Local<ArrayBufferRef> arrayBuffer2 = ArrayBufferRef::New(vm, buffer, 10, deleter, data);
```

### GetBuffer

void *ArrayBufferRef::GetBuffer()��

��ȡArrayBufferRef��ԭʼ��������

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ����   | ˵��                                             |
| ------ | ------------------------------------------------ |
| void * | ����Ϊvoid *��ʹ��ʱ��ǿתΪ����������ʱ�����͡� |

**ʾ����**

```c++
uint8_t *buffer = new uint8_t[10]();
int *data = new int;
*data = 10;
NativePointerCallback deleter = [](void *env, void *buffer, void *data) -> void {
delete[] reinterpret_cast<uint8_t *>(buffer);
    int *currentData = reinterpret_cast<int *>(data);
    delete currentData;
};
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, buffer, 10, deleter, data);
uint8_t *getBuffer = reinterpret_cast<uint8_t *>(arrayBuffer->GetBuffer());
```

### ByteLength

int32_t ArrayBufferRef::ByteLength([[maybe_unused]] const EcmaVM *vm)��

�˺������ش�ArrayBufferRef�������ĳ��ȣ����ֽ�Ϊ��λ����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����    | ˵��                            |
| ------- | ------------------------------- |
| int32_t | ��int32_t���ͷ���buffer�ĳ��ȡ� |

**ʾ����**

```c++
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, 10);
int lenth = arrayBuffer->ByteLength(vm);
```

### IsDetach

bool ArrayBufferRef::IsDetach()��

�ж�ArrayBufferRef���仺�����Ƿ��Ѿ����롣

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ���� | ˵��                                      |
| ---- | ----------------------------------------- |
| bool | �������Ѿ����뷵��true��δ���뷵��false�� |

**ʾ����**

```C++
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, 10);
bool b = arrayBuffer->IsDetach();
```

### Detach

void ArrayBufferRef::Detach(const EcmaVM *vm)��

��ArrayBufferRef���仺�������룬����������������Ϊ0��

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ���� | ˵��                             |
| ---- | -------------------------------- |
| void | ��ArrayBufferRef���仺�������롣 |

**ʾ����**

```C++
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, 10);
arrayBuffer->Detach(vm);
```



## BooleanRef

Boolean��һ�������������ͣ����ڱ�ʾ���١�

### New

Local<BooleanRef> BooleanRef::New(const EcmaVM *vm, bool input)��

����һ��BooleanRef����

**������**

| ������ | ����           | ���� | ˵��                         |
| ------ | -------------- | ---- | ---------------------------- |
| vm     | const EcmaVM * | ��   | ������������������     |
| input  | bool           | ��   | ָ��BooleanRef�����boolֵ�� |

**����ֵ��**

| ����              | ˵��                         |
| ----------------- | ---------------------------- |
| Local<BooleanRef> | �����´�����BooleanRef���� |

**ʾ����**

```c++
Local<BooleanRef> boolRef = BooleanRef::New(vm, true);
```

## BufferRef

���������ݴ�һ��λ�ô��䵽��һ��λ��ʱ��ʱ�洢���ݡ�

### New

Local<BufferRef> BufferRef::New(const EcmaVM *vm, int32_t length);

Local<BufferRef> BufferRef::New(const EcmaVM *vm, void *buffer, int32_t length, const NativePointerCallback &deleter, void *data)

����һ��BufferRef����

**������**

| ������  | ����            | ���� | ˵��                                               |
| ------- | --------------- | ---- | -------------------------------------------------- |
| vm      | const EcmaVM *  | ��   | ���������                                       |
| length  | int32_t         | ��   | ָ���ĳ��ȡ�                                       |
| buffer  | void *          | ��   | ָ��������                                         |
| deleter | const NativePointerCallback & | ��   | һ��ɾ�������������ڲ�����Ҫ������ʱ�ͷ����ڴ档 |
| data    | void *          | ��   | ���ݸ�ɾ�����Ķ������ݡ�                           |

**����ֵ��**

| ����             | ˵��                        |
| ---------------- | --------------------------- |
| Local<BufferRef> | �����´�����BufferRef���� |

**ʾ����**

```c++
Local<BufferRef> bufferRef1 = BufferRef::New(vm, 10);
uint8_t *buffer = new uint8_t[10]();
int *data = new int;
*data = 10;
NativePointerCallback deleter = [](void *env, void *buffer, void *data) -> void {
    delete[] reinterpret_cast<uint8_t *>(buffer);
    int *currentData = reinterpret_cast<int *>(data);
    delete currentData;
};
Local<BufferRef> bufferRef2 = BufferRef::New(vm, buffer, 10, deleter, data);
```

### ByteLength

int32_t BufferRef::ByteLength(const EcmaVM *vm)��

�˺������ش�ArrayBufferRef�������ĳ��ȣ����ֽ�Ϊ��λ����

**������**

| ������ | ����           | ���� | ˵��         |
| :----: | -------------- | ---- | ------------ |
|   vm   | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����    | ˵��                            |
| ------- | ------------------------------- |
| int32_t | ��int32_t���ͷ���buffer�ĳ��ȡ� |

**ʾ����**

```c++
Local<BufferRef> buffer = BufferRef::New(vm, 10);
int32_t lenth = buffer->ByteLength(vm);
```

### GetBuffer

void *BufferRef::GetBuffer()��

��ȡBufferRef��ԭʼ��������

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ����   | ˵��                                             |
| ------ | ------------------------------------------------ |
| void * | ����Ϊvoid *��ʹ��ʱ��ǿתΪ����������ʱ�����͡� |

**ʾ����**

```c++
uint8_t *buffer = new uint8_t[10]();
int *data = new int;
*data = 10;
NativePointerCallback deleter = [](void *env, void *buffer, void *data) -> void {
    delete[] reinterpret_cast<uint8_t *>(buffer);
    int *currentData = reinterpret_cast<int *>(data);
    delete currentData;
};
Local<BufferRef> bufferRef = BufferRef::New(vm, buffer, 10, deleter, data);
uint8_t *getBuffer = reinterpret_cast<uint8_t *>(bufferRef->GetBuffer());
```
### BufferToStringCallback

static ecmascript::JSTaggedValue BufferToStringCallback(ecmascript::EcmaRuntimeCallInfo *ecmaRuntimeCallInfo);

����ToString����ʱ�ᴥ���Ļص�������

**������**

| ������              | ����                   | ���� | ˵��                                   |
| ------------------- | ---------------------- | ---- | -------------------------------------- |
| ecmaRuntimeCallInfo | EcmaRuntimeCallInfo  * | ��   | ����ָ���Ļص��������Լ�����Ҫ�Ĳ����� |

**����ֵ��**

| ����          | ˵��                                                         |
| ------------- | ------------------------------------------------------------ |
| JSTaggedValue | �����ûص������Ľ��ת��ΪJSTaggedValue���ͣ�����Ϊ�˺����ķ���ֵ�� |

**ʾ����**

```C++
Local<BufferRef> bufferRef = BufferRef::New(vm, 10);
Local<StringRef> bufferStr = bufferRef->ToString(vm);
```

## DataViewRef

һ�����ڲ������������ݵ���ͼ�������ṩ��һ�ַ�ʽ�����ʺ��޸� ArrayBuffer �е��ֽڡ�

### New

static Local<DataViewRef> New(const EcmaVM *vm, Local<ArrayBufferRef> arrayBuffer, uint32_t byteOffset,uint32_t byteLength)��

����һ���µ�DataView����

**������**

| ������      | ����                  | ���� | ˵��                       |
| ----------- | --------------------- | ---- | -------------------------- |
| vm          | const EcmaVM *        | ��   | ���������               |
| arrayBuffer | Local<ArrayBufferRef> | ��   | ָ���Ļ�������             |
| byteOffset  | uint32_t              | ��   | �ӵڼ����ֽڿ�ʼд�����ݡ� |
| byteLength  | uint32_t              | ��   | Ҫ�����Ļ������ĳ��ȡ�     |

**����ֵ��**

| ����               | ˵��                   |
| ------------------ | ---------------------- |
| Local<DataViewRef> | һ���µ�DataView���� |

**ʾ����**

```c++
const int32_t length = 15;
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, length);
Local<DataViewRef> dataObj = DataViewRef::New(vm, arrayBuffer, 5, 7);
```

### ByteOffset

uint32_t DataViewRef::ByteOffset()��

��ȡDataViewRef��������ƫ������

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ����     | ˵��                               |
| -------- | ---------------------------------- |
| uint32_t | �û��������Ǹ��ֽڿ�ʼд����ȡ�� |

**ʾ����**

```C++
const int32_t length = 15;
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, length);
Local<DataViewRef> dataView = DataViewRef::New(vm, arrayBuffer, 5, 7);
uint32_t offSet = dataView->ByteOffset();
```

### ByteLength

uint32_t DataViewRef::ByteLength()��

��ȡDataViewRef�������ɲ����ĳ��ȡ�

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ����     | ˵��                            |
| -------- | ------------------------------- |
| uint32_t | DataViewRef�������ɲ����ĳ��ȡ� |

**ʾ����**

```C++
const int32_t length = 15;
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, length);
Local<DataViewRef> dataView = DataViewRef::New(vm, arrayBuffer, 5, 7);
uint32_t offSet = dataView->ByteLength();
```

### GetArrayBuffer

Local<ArrayBufferRef> DataViewRef::GetArrayBuffer(const EcmaVM *vm)��

��ȡDataViewRef����Ļ�������

**������**

| ������ | ����           | ���� | ˵��         |
| :----: | -------------- | ---- | ------------ |
|   vm   | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����                  | ˵��                                                         |
| --------------------- | ------------------------------------------------------------ |
| Local<ArrayBufferRef> | ��ȡDataViewRef�Ļ�����������ת��ΪLocal<ArrayBufferRef>���ͣ�����Ϊ�����ķ���ֵ�� |

**ʾ����**

```c++
const int32_t length = 15;
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, length);
Local<DataViewRef> dataView = DataViewRef::New(vm, arrayBuffer, 5, 7);
Local<ArrayBufferRef> getBuffer = dataView->GetArrayBuffer(vm);
```

## DateRef

Date�������ڱ�ʾ���ں�ʱ�䡣���ṩ����෽�����������������ں�ʱ�䡣

### GetTime

double DateRef::GetTime()��

�÷��������Լ�Ԫ���������ڵĺ��������ü�Ԫ����Ϊ 1970 �� 1 �� 1 �գ�UTC ��ʼʱ����ҹ��

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ����   | ˵��                                             |
| ------ | ------------------------------------------------ |
| double | һ��double���֣���ʾ�����ڵ�ʱ����Ժ���Ϊ��λ�� |

**ʾ����**

```c++
double time = 1690854800000; // 2023-07-04T00:00:00.000Z
Local<DateRef> object = DateRef::New(vm, time);
double getTime = object->GetTime();
```

## JSNApi

JSNApi�ṩ��һЩͨ�õĽӿ����ڲ�ѯ���ȡһЩ�������ԡ�

### IsBundle

bool JSNApi::IsBundle(EcmaVM *vm)��

�жϽ��ļ�����ģ����ʱ���ǲ���JSBundleģʽ��

**������**

| ������ | ����           | ���� | ˵��         |
| :----: | -------------- | ---- | ------------ |
|   vm   | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ���� | ˵��                                          |
| ---- | --------------------------------------------- |
| bool | ��ΪJSBundleģʽʱʱ����true�����򷵻�false�� |

**ʾ����**

```c++
bool b = JSNApi::IsBundle(vm);
```

### addWorker

void JSNApi::addWorker(EcmaVM *hostVm, EcmaVM *workerVm)

����һ���µ��������ӵ�ָ��������Ĺ����б��С�

**������**

|  ������  | ����           | ���� | ˵��                 |
| :------: | -------------- | ---- | -------------------- |
|  hostVm  | const EcmaVM * | ��   | ָ�����������     |
| workerVm | const EcmaVM * | ��   | �����µĹ���������� |

**����ֵ��**

| ���� | ˵��       |
| ---- | ---------- |
| void | �޷���ֵ�� |

**ʾ����**

```c++
JSRuntimeOptions option;
EcmaVM *workerVm = JSNApi::CreateEcmaVM(option);
JSNApi::addWorker(hostVm, workerVm);
```

### SerializeValue

void *JSNApi::SerializeValue(const EcmaVM *vm, Local<JSValueRef> value, Local<JSValueRef> transfer)

��value���л�Ϊtransfer���͡�

**������**

|  ������  | ����              | ���� | ˵��               |
| :------: | ----------------- | ---- | ------------------ |
|    vm    | const EcmaVM *    | ��   | ָ�����������   |
|  value   | Local<JSValueRef> | ��   | ��Ҫ���л������ݡ� |
| transfer | Local<JSValueRef> | ��   | ���л������͡�     |

**����ֵ��**

| ����   | ˵��                                                         |
| ------ | ------------------------------------------------------------ |
| void * | ת��ΪSerializationData *�ɵ���GetData��GetSize��ȡ���л��������Լ���С�� |

**ʾ����**

```c++
Local<JSValueRef> value = StringRef::NewFromUtf8(vm, "abcdefbb");
Local<JSValueRef> transfer = StringRef::NewFromUtf8(vm, "abcdefbb");
SerializationData *ptr = JSNApi::SerializeValue(vm, value, transfer);
```

### DisposeGlobalHandleAddr

static void DisposeGlobalHandleAddr(const EcmaVM *vm, uintptr_t addr);

�ͷ�ȫ�־��

**������**

| ������ | ����           | ���� | ˵��             |
| :----: | -------------- | ---- | ---------------- |
|   vm   | const EcmaVM * | ��   | ָ����������� |
|  addr  | uintptr_t      | ��   | ȫ�־���ĵ�ַ�� |

**����ֵ��**

| ���� | ˵��       |
| ---- | ---------- |
| void | �޷���ֵ�� |

**ʾ����**

```C++
Local<JSValueRef> value = StringRef::NewFromUtf8(vm, "abc");
uintptr_t address = JSNApi::GetGlobalHandleAddr(vm, reinterpret_cast<uintptr_t>(*value));
JSNApi::DisposeGlobalHandleAddr(vm, address);
```

### CheckSecureMem

static bool CheckSecureMem(uintptr_t mem);

���������ڴ��ַ�Ƿ�ȫ��

**������**

| ������ | ����      | ���� | ˵��                 |
| :----: | --------- | ---- | -------------------- |
|  mem   | uintptr_t | ��   | ��Ҫ�����ڴ��ַ�� |

**����ֵ��**

| ���� | ˵��                                |
| ---- | ----------------------------------- |
| bool | �ڴ��ַ��ȫ����true���򷵻�false�� |

**ʾ����**

```c++
Local<JSValueRef> value = StringRef::NewFromUtf8(vm, "abc");
uintptr_t address = JSNApi::GetGlobalHandleAddr(vm, reinterpret_cast<uintptr_t>(*value));
bool b = CheckSecureMem(address);
```

### GetGlobalObject

Local<ObjectRef> JSNApi::GetGlobalObject(const EcmaVM *vm)

�����ܷ�ɹ���ȡJavaScript�������envȫ�ֶ����Լ���ȫ�ֶ����Ƿ�Ϊ�ǿն���

**������**

| ������ | ����           | ���� | ˵��             |
| :----: | -------------- | ---- | ---------------- |
|   vm   | const EcmaVM * | ��   | ָ����������� |

**����ֵ��**

| ����             | ˵��                                                      |
| ---------------- | --------------------------------------------------------- |
| Local<ObjectRef> | �ɵ���ObjectRef�Լ�����JSValueRef�ĺ������ж����Ƿ���Ч�� |

**ʾ����**

```C++
Local<ObjectRef> globalObject = JSNApi::GetGlobalObject(vm);
bool b = globalObject.IsEmpty();
```

### GetUncaughtException

Local<ObjectRef> JSNApi::GetUncaughtException(const EcmaVM *vm)��

��ȡ�����δ������쳣

**������**

| ������ | ����           | ���� | ˵��             |
| :----: | -------------- | ---- | ---------------- |
|   vm   | const EcmaVM * | ��   | ָ����������� |

**����ֵ��**

| ����             | ˵��                                                      |
| ---------------- | --------------------------------------------------------- |
| Local<ObjectRef> | �ɵ���ObjectRef�Լ�����JSValueRef�ĺ������ж����Ƿ���Ч�� |

**ʾ����**

```c++
Local<ObjectRef> excep = JSNApi::GetUncaughtException(vm);
if (!excep.IsNull()) {
    // Error Message ...
}
```

### GetAndClearUncaughtException

Local<ObjectRef> JSNApi::GetAndClearUncaughtException(const EcmaVM *vm)��

���ڻ�ȡ�����δ�����������쳣��

**������**

| ������ | ����           | ���� | ˵��             |
| :----: | -------------- | ---- | ---------------- |
|   vm   | const EcmaVM * | ��   | ָ����������� |

**����ֵ��**

| ����             | ˵��                                                      |
| ---------------- | --------------------------------------------------------- |
| Local<ObjectRef> | �ɵ���ObjectRef�Լ�����JSValueRef�ĺ������ж����Ƿ���Ч�� |

**ʾ����**

```C++
Local<ObjectRef> excep = JSNApi::GetUncaughtException(vm);
if (!excep.IsNull()) {
    // Error Message ...
    JSNApi::GetAndClearUncaughtException(vm);
}
```

### HasPendingException

bool JSNApi::HasPendingException(const EcmaVM *vm)��

���ڼ��������Ƿ���δ������쳣��

**������**

| ������ | ����           | ���� | ˵��             |
| :----: | -------------- | ---- | ---------------- |
|   vm   | const EcmaVM * | ��   | ָ����������� |

**����ֵ��**

| ���� | ˵��                                          |
| ---- | --------------------------------------------- |
| bool | �������������쳣��������true���򷵻�false�� |

**ʾ����**

```c++
if (JSNApi::HasPendingException(vm)) {
    JSNApi::PrintExceptionInfo(const EcmaVM *vm);
}
```

### PrintExceptionInfo

void JSNApi::PrintExceptionInfo(const EcmaVM *vm)��

���ڴ�ӡδ������쳣�����������������쳣��

**������**

| ������ | ����           | ���� | ˵��             |
| :----: | -------------- | ---- | ---------------- |
|   vm   | const EcmaVM * | ��   | ָ����������� |

**����ֵ��**

| ���� | ˵��       |
| ---- | ---------- |
| void | �޷���ֵ�� |

**ʾ����**

```C++
if (JSNApi::HasPendingException(vm)) {
    JSNApi::PrintExceptionInfo(vm);
}
```

### SetWeakCallback

uintptr_t JSNApi::SetWeakCallback(const EcmaVM *vm, uintptr_t localAddress, void *ref, WeakRefClearCallBack freeGlobalCallBack, WeakRefClearCallBack nativeFinalizeCallback)��

�˺�����������һ�����ص����������ص�������һ���������͵Ļص��������������ڲ���Ҫʱ�Զ��ͷ��ڴ棬�Ա����ڴ�й©���⡣

**������**

|         ������         | ����                 | ���� | ˵��                                                       |
| :--------------------: | -------------------- | ---- | ---------------------------------------------------------- |
|           vm           | const EcmaVM *       | ��   | ָ�����������                                           |
|      localAddress      | uintptr_t            | ��   | ���ص�ַ��ָ���õ����������ڴ��ַ                         |
|          ref           | void *               | ��   | ��Ҫ���ö�����ڴ��ַ��                                   |
|   freeGlobalCallBack   | WeakRefClearCallBack | ��   | ����������ص��������������ñ����ʱ���ûص������������á� |
| nativeFinalizeCallback | WeakRefClearCallBack | ��   | C++ԭ�����������������                                    |

**����ֵ��**

| ����      | ˵��                       |
| --------- | -------------------------- |
| uintptr_t | ���������ڴ�ռ��еĵ�ַ�� |

**ʾ����**

```C++
template <typename T> void FreeGlobalCallBack(void *ptr)
{
    T *i = static_cast<T *>(ptr);
}
template <typename T> void NativeFinalizeCallback(void *ptr)
{
    T *i = static_cast<T *>(ptr);
    delete i;
}
Global<JSValueRef> global(vm, BooleanRef::New(vm, true));
void *ref = new char('a');
WeakRefClearCallBack freeGlobalCallBack = FreeGlobalCallBack<char>;
WeakRefClearCallBack nativeFinalizeCallback = NativeFinalizeCallback<char>;
global.SetWeakCallback(ref, freeGlobalCallBack, nativeFinalizeCallback);
```

### ThrowException

void JSNApi::ThrowException(const EcmaVM *vm, Local<JSValueRef> error)��

VM������׳�ָ���쳣��

**������**

| ������ | ����              | ���� | ˵��             |
| :----: | ----------------- | ---- | ---------------- |
|   vm   | const EcmaVM *    | ��   | ָ����������� |
| error  | Local<JSValueRef> | ��   | ָ��error��Ϣ��  |

**����ֵ��**

| ���� | ˵��       |
| ---- | ---------- |
| void | �޷���ֵ�� |

**ʾ����**

```C++
Local<JSValueRef> error = Exception::Error(vm, StringRef::NewFromUtf8(vm, "Error Message"));
JSNApi::ThrowException(vm, error);
```

### NotifyNativeCalling

static void NotifyNativeCalling(const EcmaVM *vm, const void *nativeAddress);

void JSNApi::NotifyNativeCalling([[maybe_unused]] const EcmaVM *vm, [[maybe_unused]] const void *nativeAddress)��

���ض�������֪ͨԭ�������¼���

**������**

| ������        | ����                            | ���� | ˵��                |
| ------------- | ------------------------------- | ---- | ------------------- |
| vm            | [[maybe_unused]] const EcmaVM * | ��   | ���������        |
| nativeAddress | [[maybe_unused]] const void *   | ��   | C++ԭ������ĵ�ַ�� |

**����ֵ��**

��

**ʾ����**

```c++
void *par = new int(0);
JSNApi::NotifyNativeCalling(vm_, par);
```

### function

std::function<bool(std::string dirPath, uint8_t **buff, size_t *buffSize)> cb);

һ���������󣬿������ڴ洢�����ݺ͵���һ���ɵ��ö����纯����lambda���ʽ�������󣩣��Ա�����Ҫʱִ����Ӧ�Ĳ�����

**������**

| ������   | ����        | ���� | ˵��                                            |
| -------- | ----------- | ---- | ----------------------------------------------- |
| dirPath  | std::string | ��   | �ַ������͵Ĳ�������ʾĿ¼·����                |
| buff     | uint8_t **  | ��   | ָ��uint8_t����ָ���ָ�룬���ڴ洢���������ݡ� |
| buffSize | size_t *    | ��   | ָ��size_t���͵�ָ�룬���ڴ洢��������С��      |

**����ֵ��**

| ���� | ˵��                                                  |
| ---- | ----------------------------------------------------- |
| bool | ����ֵ��true �� false����ʾ�ú���ִ�еĲ����Ƿ�ɹ��� |

**ʾ����**

```c++
static *void *SetHostResolveBufferTracker(EcmaVM **vm*, std::function<*bool*(std::string dirPath, *uint8_t* **buff, *size_t* *buffSize)> *cb*);
```

### SetProfilerState

static void JSNApi::SetProfilerState(const EcmaVM *vm, bool value);

���������������EcmaVM����CPUProfiler����״̬��

**������**

| ������ | ����           | ���� | ˵��                    |
| ------ | -------------- | ---- | ----------------------- |
| vm     | const EcmaVM * | ��   | ���������            |
| value  | bool           | ��   | ����ֵ��true �� false�� |

**����ֵ��**

��

**ʾ����**

```c++
bool ret = true;
JSNApi::SetProfilerState(vm_, ret);
```

### IsProfiling

static bool JSNApi::IsProfiling(EcmaVM *vm);

���ڼ���������EcmaVM���Ƿ���CPUProfiling����״̬��

**������**

| ������ | ����     | ���� | ˵��         |
| ------ | -------- | ---- | ------------ |
| vm     | EcmaVM * | ��   | ��������� |

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ����������EcmaVM������CPUProfiling����״̬������true�����򣬷���false�� |

**ʾ����**

```c++
bool b = JSNApi::IsProfiling(vm_);
```

### SynchronizVMInfo

static void JSNApi::SynchronizVMInfo(EcmaVM *vm, const EcmaVM *hostVM);

��һ��EcmaVM�������Ϣ���Ƶ���һ��EcmaVM�����У�ʹ���Ǿ�����ͬ����Ϣ�����ԡ�

**������**

| ������ | ����     | ���� | ˵��                       |
| ------ | -------- | ---- | -------------------------- |
| vm     | EcmaVM * | ��   | ��������󣨸���Դ����     |
| hostVM | EcmaVM * | ��   | ��������󣨸���Ŀ�ĵأ��� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::SynchronizVMInfo(vm_, hostVM_);
```

### SetLoop

static void JSNApi::SetLoop(EcmaVM *vm, void *loop);

��loopָ�봫�ݸ�EcmaVM����vm��SetLoop������

**������**

| ������ | ����     | ���� | ˵��           |
| ------ | -------- | ---- | -------------- |
| vm     | EcmaVM * | ��   | ���������   |
| loop   | void *   | ��   | ѭ���ص������� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::SetLoop(vm_, loop);
```

### SetAssetPath

static void JSNApi::SetAssetPath(EcmaVM *vm, const std::string &assetPath);

��assetPath�ַ���ת��ΪC�����ַ����������䴫�ݸ�EcmaVM����vm��SetAssetPath������

**������**

| ������    | ����                | ���� | ˵��             |
| --------- | ------------------- | ---- | ---------------- |
| vm        | EcmaVM *            | ��   | ���������     |
| assetPath | const std::string & | ��   | Ҫ���õ���Դ·�� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::SetAssetPath(vm_, assetPath);
```

### SetBundle

static void JSNApi::SetBundle(EcmaVM *vm, bool value);

��value���ݸ�EcmaVM����vm��SetIsBundlePack������

**������**

| ������ | ����     | ���� | ˵��                    |
| ------ | -------- | ---- | ----------------------- |
| vm     | EcmaVM * | ��   | ���������            |
| value  | bool     | ��   | ����ֵ��true �� false�� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::SetBundle(vm_, value);
```

### SwitchCurrentContext

static void JSNApi::SwitchCurrentContext(EcmaVM *vm, EcmaContext *context);

����ǰ�������л���ָ����context��

**������**

| ������  | ����          | ���� | ˵��               |
| ------- | ------------- | ---- | ------------------ |
| vm      | EcmaVM *      | ��   | ���������       |
| context | EcmaContext * | ��   | Ҫ�л����������ġ� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::SwitchCurrentContext(vm_, context);
```

### LoadAotFile

static void JSNApi::LoadAotFile(EcmaVM *vm, const std::string &moduleName);

����һ��Ahead-of-Time��AOT���ļ���

**������**

| ������     | ����                | ���� | ˵��                      |
| ---------- | ------------------- | ---- | ------------------------- |
| vm         | EcmaVM *            | ��   | ���������              |
| moduleName | const std::string & | ��   | Ҫ���ص� AOT �ļ������ơ� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::LoadAotFile(vm_, moduleName);
```

### DestroyJSVM

static void JSNApi::DestroyJSVM(EcmaVM *ecmaVm);

����һ��ָ����ECMA�������EcmaVM����

**������**

| ������ | ����     | ���� | ˵��                 |
| ------ | -------- | ---- | -------------------- |
| ecmaVm | EcmaVM * | ��   | Ҫ���ٵ���������� |

**����ֵ��**

��

**ʾ����**

```c++
 JSNApi::DestroyJSVM(ecmaVm);
```

### DeleteSerializationData

void JSNApi::DeleteSerializationData(void *data);

ɾ��һ�����л����ݶ���

**������**

| ������ | ����   | ���� | ˵��                 |
| ------ | ------ | ---- | -------------------- |
| data   | void * | ��   | Ҫɾ�������л����ݡ� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::DeleteSerializationData(data);
```

### DeserializeValue

Local<JSValueRef> JSNApi::DeserializeValue(const EcmaVM *vm, void *recoder, void *hint);

�����л����ݷ����л�ΪJavaScriptֵ��

**������**

| ������  | ����           | ���� | ˵��                     |
| ------- | -------------- | ---- | ------------------------ |
| vm      | const EcmaVM * | ��   | ���������             |
| recoder | void *         | ��   | ���ڷ����л��ı�������� |
| hint    | void *         | ��   | ��ѡ����ʾ����         |

**����ֵ;**

| ����              | ˵��                                       |
| ----------------- | ------------------------------------------ |
| Local<JSValueRef> | ����ת���� JSValueRef ���͵ķ����л����ݡ� |

**ʾ����**

```c++
 Local<JSValueRef> value = JSNApi::DeserializeValue(vm_, recoder, hint);
```

### Execute

bool JSNApi::Execute(EcmaVM *vm, const uint8_t *data, int32_t size, const std::string &entry,const std::string &filename, bool needUpdate);

ִ��һ��Ark�������ļ���

**������**

| ������     | ����                | ���� | ˵��                                                        |
| ---------- | ------------------- | ---- | ----------------------------------------------------------- |
| vm         | EcmaVM *            | ��   | ���������                                                |
| data       | const uint8_t *     | ��   | JavaScript������ֽ����顣                                  |
| size       | int32_t             | ��   | JavaScript����Ĵ�С�����ֽ�Ϊ��λ����                      |
| entry      | const std::string & | ��   | JavaScript������ڵ�����ơ�                                |
| filename   | const std::string & | ��   | JavaScript�����ļ�����                                      |
| needUpdate | bool                | ��   | ����ֵ��true �� false������ʾ�Ƿ���Ҫ��������������״̬�� |

**����ֵ��**

| ���� | ˵��                                               |
| ---- | -------------------------------------------------- |
| bool | ִ��Ark�������ļ��ɹ�������true�����򣬷���false�� |

**ʾ����**

```c++
bool b = JSNApi::Execute(vm_, data, size, entry,filename, needUpdate);
```

### Execute

bool JSNApi::Execute(EcmaVM *vm, const std::string &fileName, const std::string &entry, bool needUpdate);

ִ��һ��Ark�ļ���

**������**

| ������     | ����                | ���� | ˵��                                                        |
| ---------- | ------------------- | ---- | ----------------------------------------------------------- |
| vm         | EcmaVM *            | ��   | ���������                                                |
| fileName   | const std::string & | ��   | JavaScript�����ļ�����                                      |
| entry      | const std::string & | ��   | JavaScript������ڵ�����ơ�                                |
| needUpdate | bool                | ��   | ����ֵ��true �� false������ʾ�Ƿ���Ҫ��������������״̬�� |

**����ֵ��**

| ���� | ˵��                                         |
| ---- | -------------------------------------------- |
| bool | ִ��Ark�ļ��ɹ�������true�����򣬷���false�� |

**ʾ����**

```c++
bool b = JSNApi::Execute(vm_, fileName, entry, needUpdate);
```

### SetHostPromiseRejectionTracker

void JSNApi::SetHostPromiseRejectionTracker(EcmaVM *vm, void *cb, void* data);

����������JavaScript�������EcmaVM�������������Promise�ܾ���������Host Promise Rejection Tracker����

**������**

| ������ | ����     | ���� | ˵��                                          |
| ------ | -------- | ---- | --------------------------------------------- |
| vm     | EcmaVM * | ��   | ���������                                  |
| cb     | void     | ��   | Ҫ����Ϊ��ǰECMA�����ĵ�Promise�ܾ��ص������� |
| data   | void     | ��   | Ҫ����Ϊ��ǰECMA�����ĵ����ݡ�                |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::SetHostPromiseRejectionTracker(vm_, cb, data);
```

### ExecuteModuleFromBuffer

static bool ExecuteModuleFromBuffer(EcmaVM *vm, const void *data, int32_t size, const std::string &file);

�Ӹ��������ݻ�������ִ��һ��ģ�顣

**������**

| ������ | ����              | ���� | ˵��                   |
| ------ | ----------------- | ---- | ---------------------- |
| vm     | EcmaVM *          | ��   | ���������             |
| data   | const void *      | ��   | Ҫִ�е�ģ������       |
| size   | int32_t           | ��   | Ҫִ�е�ģ�����ݵĴ�С |
| file   | const std::string | ��   | Ҫִ�е�ģ���ļ������� |

**����ֵ��**

| ����    | ˵��                                          |
| :------ | :-------------------------------------------- |
| boolean | ���ִ��ģ��ɹ����򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
const void *data = "TestData";
int32_t size = 8;
const std::string file = "TestFile";
bool result = JSNApi::ExecuteModuleFromBuffer(vm_, data, size, file);
```

### GetExportObject

static Local<ObjectRef> GetExportObject(EcmaVM *vm, const std::string &file, const std::string &key);

����ָ�����ļ���keyֵ��ȡ����������

**������**

| ������ | ����              | ���� | ˵��           |
| ------ | ----------------- | ---- | -------------- |
| vm     | EcmaVM *          | ��   | ���������     |
| file   | const std::string | ��   | ָ�����ļ����� |
| key    | const std::string | ��   | ָ����keyֵ    |

**����ֵ��**

| ����             | ˵��                   |
| :--------------- | :--------------------- |
| Local<ObjectRef> | ���ػ�ȡ�������Ķ��� |

**ʾ����**

```c++
const std::string file = "TestFile";
const std::string key = "TestKey";
Local<ObjectRef> result = GetExportObject(vm_, file, key);
```

### GetExportObjectFromBuffer

static Local<ObjectRef> GetExportObjectFromBuffer(EcmaVM *vm, const std::string &file, const std::string &key);

�Ӹ����Ļ������и���ָ�����ļ���keyֵ��ȡ����������

**������**

| ������ | ����              | ���� | ˵��           |
| ------ | ----------------- | ---- | -------------- |
| vm     | EcmaVM *          | ��   | ���������     |
| file   | const std::string | ��   | ָ�����ļ����� |
| key    | const std::string | ��   | ָ����keyֵ    |

**����ֵ��**

| ����             | ˵��                                   |
| :--------------- | :------------------------------------- |
| Local<ObjectRef> | ���شӸ����Ļ������л�ȡ�������Ķ��� |

**ʾ����**

```c++
const std::string file = "TestFile";
const std::string key = "TestKey";
Local<ObjectRef> result = GetExportObjectFromBuffer(vm_, file, key);
```

### GetAndClearUncaughtException

static Local<ObjectRef> GetAndClearUncaughtException(const EcmaVM *vm);

��ȡ������δ������쳣��

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����             | ˵��                   |
| :--------------- | :--------------------- |
| Local<ObjectRef> | ����δ������쳣���� |

**ʾ����**

```c++
Local<ObjectRef> result = JSNApi::GetAndClearUncaughtException(vm_);
```

### GetUncaughtException

static Local<ObjectRef> GetUncaughtException(const EcmaVM *vm);

��ȡδ������쳣��

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����             | ˵��                   |
| :--------------- | :--------------------- |
| Local<ObjectRef> | ����δ������쳣���� |

**ʾ����**

```c++
Local<ObjectRef> result = JSNApi::GetUncaughtException(vm_);
```

### HasPendingException

static bool HasPendingException(const EcmaVM *vm);

����Ƿ��д�������쳣��

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����    | ˵��                                            |
| :------ | :---------------------------------------------- |
| boolean | ����д�������쳣���򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
bool result = JSNApi::HasPendingException(vm_);
```

### HasPendingJob

static bool HasPendingJob(const EcmaVM *vm);

����Ƿ��д����������

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����    | ˵��                                            |
| :------ | :---------------------------------------------- |
| boolean | ����д�����������򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
bool result = JSNApi::HasPendingJob(vm_);
```

###  DestroyJSContext

static void DestroyJSContext(EcmaVM *vm, EcmaContext *context);

�ú������������������������������������������Դ��ͨ�� `EcmaContext::CheckAndDestroy` ����ʵ�ּ������ٹ��̡�

**������**

| ������  | ����          | ���� | ˵��                   |
| ------- | ------------- | ---- | ---------------------- |
| vm      | EcmaVM *      | ��   | ���������           |
| context | EcmaContext * | ��   | ��ʾEcma�����ĵ�ָ�롣 |

**����ֵ��**

��

**ʾ����**

```c++
EcmaContext *context1 = JSNApi::CreateJSContext(vm);
JSNApi::DestroyJSContext(vm, context1);
```

###  SetMockModuleList

static void SetMockModuleList(EcmaVM *vm, const std::map<std::string, std::string> &list);

��������ģ��ģ���б����õ�ָ����Ecma������С�

**������**

| ������ | ����                                       | ���� | ˵��                                                         |
| ------ | ------------------------------------------ | ---- | ------------------------------------------------------------ |
| vm     | EcmaVM *                                   | ��   | ���������                                                 |
| list   | const std::map<std::string, std::string> & | ��   | ��ʾһ�����ַ������ַ�����ӳ�䣬<br/>���е�һ���ַ�����ģ������ƣ��ڶ����ַ�����ģ������ݡ� |

**����ֵ��**

��

**ʾ����**

```c++
std::map<std::string, std::string> mockModuleList;
mockModuleList["Module1"] = "Module1Context";
mockModuleList["Module2"] = "Module2Context";
JSNApi::SetMockModuleList(vm, mockModuleList);
```

###  SetHostPromiseRejectionTracker

static void SetHostPromiseRejectionTracker(EcmaVM *vm, void *cb, void *data);

�ú����������� Ecma �����ĵ����� Promise �ܾ����������Լ���صĻص����������ݡ�

`SetHostPromiseRejectionTracker` ������������ Promise �ܾ��������Ļص�������

`SetPromiseRejectCallback` �������� Promise �ܾ��Ļص�������

`SetData` ���������ض������ݡ�

**������**

| ������ | ����     | ���� | ˵��                                    |
| ------ | -------- | ---- | --------------------------------------- |
| vm     | EcmaVM * | ��   | ���������                            |
| cb     | Void *   | ��   | ��ʾ���� Promise �ܾ��������Ļص������� |
| data   | Void *   | ��   | ��ʾ��Ҫ���õ����ݡ�                    |

**����ֵ��**

��

**ʾ����**

```c++
void *data = reinterpret_cast<void *>(builtins::BuiltinsFunction::FunctionPrototypeInvokeSelf);
JSNApi::SetHostPromiseRejectionTracker(vm, data, data);
```

###  SetHostResolveBufferTracker

static void SetHostResolveBufferTracker(EcmaVM vm,std::function<bool(std::string dirPath, uint8_t*buff, size_t buffSize)> cb);

�ú����������� Ecma ����������������������������ص�������

�ص�����ͨ�� `std::function` ���ݣ�����Ŀ¼·����`dirPath`���ͻ�����ָ�루`uint8_t** buff`�������Сָ�루`size_t* buffSize`����Ϊ������������һ������ֵ����ʾ�Ƿ�ɹ�������������

**������**

| ������ | ����                                                         | ���� | ˵��                                |
| ------ | ------------------------------------------------------------ | ---- | ----------------------------------- |
| vm     | EcmaVM *                                                     | ��   | ���������                        |
| cb     | std::function<bool(std::string dirPath, uint8_t **buff, size_t *buffSize)> | ��   | ��������������������<br/>�Ļص����� |

**����ֵ��**

��

**ʾ����**

```c++
std::function<bool(std::string dirPath, uint8_t * *buff, size_t * buffSize)> cb = [](const std::string &inputPath,
    uint8_t **buff, size_t *buffSize) -> bool {
    if (inputPath.empty() || buff == nullptr || buffSize == nullptr) {
        return false;
    }
    return false;
};
JSNApi::SetHostResolveBufferTracker(vm, cb);
```

###  SetUnloadNativeModuleCallback

static void SetUnloadNativeModuleCallback(EcmaVM *vm, const std::function<bool(const std::string &moduleKey)> &cb);

�Զ���ж�ر���ģ��ʱ����Ϊ��

**������**

| ������ | ����                                              | ���� | ˵��                                                         |
| ------ | ------------------------------------------------- | ---- | ------------------------------------------------------------ |
| vm     | EcmaVM *                                          | ��   | ���������                                                 |
| cb     | std::function<bool(const std::string &moduleKey)> | ��   | �ص���������һ�� `moduleKey` �������ַ������ͣ�<br/>������һ������ֵ����ʾ�Ƿ�ɹ�ж�ر���ģ�� |

**����ֵ��**

��

**ʾ����**

```c++
bool UnloadNativeModuleCallback(const std::string &moduleKey) {
    return true;
}
JSNApi::SetUnloadNativeModuleCallback(vm, UnloadNativeModuleCallback);
```

###  SetNativePtrGetter

static void SetNativePtrGetter(EcmaVM *vm, void *cb);

�ú����������� Ecma ������ı���ָ���ȡ���Ļص�������

**������**

| ������ | ����     | ���� | ˵��           |
| ------ | -------- | ---- | -------------- |
| vm     | EcmaVM * | ��   | ���������   |
| cb     | Void *   | ��   | �ص�����ָ�롣 |

**����ֵ��**

��

**ʾ����**

```c++
void *cb = reinterpret_cast<void *>(NativePtrGetterCallback);
JSNApi::SetNativePtrGetter(vm, cb);
```

###  SetSourceMapTranslateCallback

static void SetSourceMapTranslateCallback(EcmaVM *vm, SourceMapTranslateCallback cb);

�ú����������� Ecma �������Դӳ�䷭��ص�������

**������**

| ������   | ����                       | ���� | ˵��                               |
| -------- | -------------------------- | ---- | ---------------------------------- |
| vm       | EcmaVM *                   | ��   | ���������                       |
| callback | SourceMapTranslateCallback | ��   | �ص�������������Դӳ�䷭��Ļص��� |

**����ֵ��**

��

**ʾ����**

```c++
bool SourceMapTranslateCallback(const std::string& sourceLocation, std::string& translatedLocation) {
    return true; 
}
JSNApi::SetSourceMapTranslateCallback(vm, SourceMapTranslateCallback);
```

###  DestroyMemMapAllocator

static void DestroyMemMapAllocator();

�ú������������ڴ�ӳ���������MemMapAllocator����ʵ����

**������**

��

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::DestroyMemMapAllocator();
```

###  DestroyPGOProfiler

static void DestroyPGOProfiler();

�ú��������������ܷ�����PGO��Profile-Guided Optimization���ķ�����������ʵ����

ͨ������ `ecmascript::pgo::PGOProfilerManager::GetInstance()`��ȡ PGOProfilerManager �ĵ���ʵ���������� `Destroy` �����������١�

**������**

��

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::DestroyPGOProfiler();
```

###  ExecutePendingJob

static void ExecutePendingJob(const EcmaVM *vm);

�ú�������ִ�е�ǰ Ecma �������й���� Promise ����

��ִ��ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION_WITHOUT_RETURN` �����Ƿ����Ǳ�ڵ��쳣��

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::ExecutePendingJob(vm);
```

###  PreFork

static void PreFork(EcmaVM *vm);

����������ִ�� fork ����֮ǰ��ִ���� Ecma �������ص�Ԥ�����衣

**������**

| ������ | ����     | ���� | ˵��         |
| ------ | -------- | ---- | ------------ |
| vm     | EcmaVM * | ��   | ��������� |

**����ֵ��**

��

**ʾ����**

```c++
JSRuntimeOptions option;
EcmaVM *workerVm = JSNApi::CreateEcmaVM(option);
JSNApi::PreFork(workerVm);
```

###  PostFork

static void PostFork(EcmaVM *vm, const RuntimeOption &option);

�ú���������ִ�� fork ����֮��ִ���� Ecma �������صĺ����衣

**������**

| ������ | ����                  | ���� | ˵��                 |
| ------ | --------------------- | ---- | -------------------- |
| vm     | EcmaVM *              | ��   | ���������         |
| option | const RuntimeOption & | ��   | ����ʱѡ������õ��� |

**����ֵ��**

��

**ʾ����**

```c++
RuntimeOption option;
JSNApi::PostFork(vm, option);
```

###  ExecuteModuleBuffer

static bool ExecuteModuleBuffer(EcmaVM *vm, const uint8_t *data, int32_t size, const std::string &filename = "",bool needUpdate = false);

�ú�������ִ�д����ģ�����ݡ�

��ִ��ǰ�����������Ƿ����Ǳ�ڵ��쳣��`CHECK_HAS_PENDING_EXCEPTION` �꣩��

���ģ��ִ��ʧ�ܣ������������־������ `false`�����򷵻� `true`��

**������**

| ������     | ����               | ���� | ˵��                   |
| ---------- | ------------------ | ---- | ---------------------- |
| vm         | EcmaVM *           | ��   | ���������           |
| data       | const uint8_t*     | ��   | ��ʾģ�����ݵ�ָ�롣   |
| size       | int32_t            | ��   | ��ʾģ�����ݵĴ�С��   |
| filename   | const std::string& | ��   | ��ʾģ����ļ�����     |
| needUpdate | bool               | ��   | ��ʾ�Ƿ���Ҫ����ģ�顣 |

**����ֵ��**

| ���� | ˵��                   |
| ---- | ---------------------- |
| bool | ��ʾģ��ִ���Ƿ�ɹ��� |

**ʾ����**

```c++
const char *fileName = "__JSNApiTests_ExecuteModuleBuffer.abc";
const char *data = R"(
    .language ECMAScript
    .function any func_main_0(any a0, any a1, any a2) {
        ldai 1
        return
    }
)";
bool res =
    JSNApi::ExecuteModuleBuffer(vm, reinterpret_cast<const uint8_t *>(data), sizeof(data), fileName);
```

###  TriggerGC

static void TriggerGC(const EcmaVM *vm, TRIGGER_GC_TYPE gcType = TRIGGER_GC_TYPE::SEMI_GC);

�ú������ڴ����������գ�GC������������ѡ��ͬ�������������͡�

ͨ������������� CollectGarbage ����ִ���������ղ�����

�ڴ�����������֮ǰ��ͨ�� CHECK_HAS_PENDING_EXCEPTION_WITHOUT_RETURN �����Ƿ����Ǳ�ڵ��쳣��

**������**

| ������ | ����            | ���� | ˵��                                           |
| ------ | --------------- | ---- | ---------------------------------------------- |
| vm     | const EcmaVM *  | ��   | ���������                                   |
| gcType | TRIGGER_GC_TYPE | ��   | ��һ��ö�����ͣ���ʾ����GC���������գ������͡� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::TriggerGC(vm);
```

###  ThrowException

static void ThrowException(const EcmaVM *vm, Local<JSValueRef> error);

�ú��������� Ecma ��������׳�һ���쳣��

���׳�֮ǰ�����ȼ���Ƿ��Ѿ����ڹ�����쳣������������¼��־�����أ��Ա�����ǰ���쳣��Ϣ��

ͨ�� SetException ������������쳣��������Ϊ��ǰ�̵߳��쳣��

**������**

| ������ | ����              | ���� | ˵��         |
| ------ | ----------------- | ---- | ------------ |
| vm     | const EcmaVM *    | ��   | ��������� |
| error  | Local<JSValueRef> | ��   | �쳣����     |

**����ֵ��**

��

**ʾ����**

```c++
Local<StringRef> message = StringRef::NewFromUtf8(vm, "ErrorTest");
Local<JSValueRef> error = Exception::Error(vm,message);
JSNApi::ThrowException(vm, error);
```

###  PrintExceptionInfo

static void PrintExceptionInfo(const EcmaVM *vm);

�ú������ڴ�ӡ��ǰǱ�ڵ��쳣��Ϣ����ȡ��ǰǱ�ڵ��쳣������������쳣��ֱ�ӷ��ء�

����쳣�� `JSError` ���ͣ������������� `PrintJSErrorInfo` ������ӡ��ϸ������Ϣ��

���򣬽��쳣ת��Ϊ�ַ������������־�С�

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

��

**ʾ����**

```c++
 JSNApi::PrintExceptionInfo(vm);
```

## JSValueRef

JSValueRef��һ�����ڱ�ʾJSֵ���ࡣ���ṩ��һЩ��ʽ�������ͷ���JS�еĸ����������ͣ����ַ��������֡�����ֵ����������ȡ�ͨ��ʹ��JSValueRef�������Ի�ȡ������JSֵ�����Ժͷ�����ִ�к������ã�ת���������͵ȡ�

### Undefined

Local<PrimitiveRef> JSValueRef::Undefined(const EcmaVM *vm)��

���ڻ�ȡһ����ʾδ����ֵ��`Value`�����������ͨ���ڴ���JavaScript��C++֮�������ת��ʱʹ�á�

**������**

| ������ | ����           | ���� | ˵��             |
| :----: | -------------- | ---- | ---------------- |
|   vm   | const EcmaVM * | ��   | ָ����������� |

**����ֵ��**

| ����                | ˵��             |
| ------------------- | ---------------- |
| Local<PrimitiveRef> | ����Ϊԭ������ |

**ʾ����**

```C++
Local<PrimitiveRef> primitive =JSValueRef::Undefined(vm);
```

### Null

Local<PrimitiveRef> JSValueRef::Null(const EcmaVM *vm)��

���ڻ�ȡһ����ʾΪNull��`Value`�����������ͨ���ڴ���JavaScript��C++֮�������ת��ʱʹ�á�

**������**

| ������ | ����           | ���� | ˵��             |
| :----: | -------------- | ---- | ---------------- |
|   vm   | const EcmaVM * | ��   | ָ����������� |

**����ֵ��**

| ����                | ˵��             |
| ------------------- | ---------------- |
| Local<PrimitiveRef> | ����Ϊԭ������ |

**ʾ����**

```C++
Local<PrimitiveRef> primitive = JSValueRef::Null(vm);
```

### IsGeneratorObject

bool JSValueRef::IsGeneratorObject()��

�ж��Ƿ�Ϊ����������

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ���� | ˵��                                  |
| ---- | ------------------------------------- |
| bool | �����������󷵻�true�����򷵻�false�� |

**ʾ����**

```C++
ObjectFactory *factory = vm->GetFactory();
auto env = vm->GetGlobalEnv();
JSHandle<JSTaggedValue> genFunc = env->GetGeneratorFunctionFunction();
JSHandle<JSGeneratorObject> genObjHandleVal = factory->NewJSGeneratorObject(genFunc);
JSHandle<JSHClass> hclass = JSHandle<JSHClass>::Cast(env->GetGeneratorFunctionClass());
JSHandle<JSFunction> generatorFunc = JSHandle<JSFunction>::Cast(factory->NewJSObject(hclass));
JSFunction::InitializeJSFunction(vm->GetJSThread(), generatorFunc, FunctionKind::GENERATOR_FUNCTION);
JSHandle<GeneratorContext> generatorContext = factory->NewGeneratorContext();
generatorContext->SetMethod(vm->GetJSThread(), generatorFunc.GetTaggedValue());
JSHandle<JSTaggedValue> generatorContextVal = JSHandle<JSTaggedValue>::Cast(generatorContext);
genObjHandleVal->SetGeneratorContext(vm->GetJSThread(), generatorContextVal.GetTaggedValue());
JSHandle<JSTaggedValue> genObjTagHandleVal = JSHandle<JSTaggedValue>::Cast(genObjHandleVal);
Local<GeneratorObjectRef> genObjectRef = JSNApiHelper::ToLocal<GeneratorObjectRef>(genObjTagHandleVal);
bool b = genObjectRef->IsGeneratorObject();
```

### IsUint8Array

bool JSValueRef::IsUint8Array();

�ж�һ��JSValueRef�����Ƿ�ΪUint8Array���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ���JSValueRef����ΪUint8Array���ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm_, 5);
Local<Uint8ArrayRef> object = Uint8ArrayRef::New(vm_, buffer, 4, 5);
bool b = object->IsUint8Array();
```

### IsInt8Array

bool JSValueRef::IsInt8Array();

�ж�һ��JSValueRef�����Ƿ�ΪInt8Array���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ���JSValueRef����ΪInt8Array���ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm_, 5);
Local<ObjectRef> object = Int8ArrayRef::New(vm_, buffer, 4, 5);
bool b = object->IsInt8Array();
```

### IsError

bool JSValueRef::IsError();

�ж�һ��JSValueRef�����Ƿ�Ϊ�������͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                      |
| ---- | --------------------------------------------------------- |
| bool | ���JSValueRef����Ϊ�������ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
Local<StringRef> message = StringRef::NewFromUtf8(vm_, "ErrorTest");
bool b = message->IsError();
```

### InstanceOf

bool JSValueRef::InstanceOf(const EcmaVM *vm, Local<JSValueRef> value);

���һ���ض���JSValueRef�Ƿ�����һ���ض���EcmaVM����

**������**

| ������ | ����              | ���� | ˵��          |
| ------ | ----------------- | ---- | ------------- |
| vm     | const EcmaVM *    | ��   | ���������  |
| value  | Local<JSValueRef> | ��   | ָ����value�� |

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ����ض���JSValueRef�����ض���EcmaVM���󣬷���true�����򣬷���false�� |

**ʾ����**

```c++
Local<ObjectRef> origin = ObjectRef::New(vm_);
Local<JSValueRef> ConstructObjectJSValueRef(const EcmaVM *vm){
    // ����һ��JSValueRef���͵Ķ���value
    // ...
    return value; // ���ز���value
}
bool b = origin->InstanceOf(vm_, ConstructObjectJSValueRef(vm_));
```

### IsPromise

bool JSValueRef::IsPromise();

�ж�һ��JSValueRef�����Ƿ�ΪPromise���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ���JSValueRef����ΪPromiseRef���ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
Local<JSValueRef> tag = PromiseCapabilityRef::New(vm_)->GetPromise(vm_);
bool b = tag->IsPromise();
```

### IsDate

bool JSValueRef::IsDate();

�ж�һ��JSValueRef�����Ƿ�Ϊ�������͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                      |
| ---- | --------------------------------------------------------- |
| bool | ���JSValueRef����Ϊ�������ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
double timeRef = 1.1;
Local<DateRef> dateRef = DateRef::New(vm_, timeRef);
bool b = dateRef->IsDate();
```

### IsTypedArray

bool JSValueRef::IsTypedArray();

���һ��JSValueRef�����Ƿ�Ϊ���ͻ����飨TypedArray����

**������**

��

**����ֵ��**

| ���� | ˵��                                                        |
| ---- | ----------------------------------------------------------- |
| bool | ���JSValueRef����Ϊ���ͻ����飬����true�����򣬷���false�� |

**ʾ����**

```c++
int input = 123;
Local<JSValueRef> res = IntegerRef::New(vm_, input);
bool b = res->IsTypedArray();
```

### IsDataView

bool JSValueRef::IsDataView();

�ж�һ��JSValueRef�����Ƿ�ΪDataView���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ���JSValueRef����ΪDataViewRef���ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
Local<JSValueRef> tag = DataViewRef::New(vm_,ArrayBufferRef::New(vm_,0),0,0);
bool b = tag->IsDataView();
```

### IsBuffer

bool JSValueRef::IsBuffer();

�ж�һ��JSValueRef�����Ƿ�ΪBuffer���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                        |
| ---- | ----------------------------------------------------------- |
| bool | ���JSValueRef����ΪBuffer���ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
int32_t length = 15;
Local<BufferRef> bufferRef = BufferRef::New(vm_, length);
bool b = bufferRef->IsBuffer();
```

### IsArrayBuffer

bool JSValueRef::IsArrayBuffer();

�ж�һ��JSValueRef�����Ƿ�ΪArrayBuffer���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ���JSValueRef����ΪArrayBuffer���ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
Local<ArrayBufferRef> ConstructObjectArrBufRef(const EcmaVM *vm, const int32_t length_){
    // ����һ��ArrayBufferRef���͵Ķ���arrayBuffer
    // ...
    return arrayBuffer; // ����arrayBuffer
}
Local<ArrayBufferRef> arrayBuf = ConstructObjectArrBufRef(vm_, 15);
bool b = arrayBuf->IsArrayBuffer();
```

### IsArray

bool JSValueRef::IsArray(const EcmaVM *vm);

�ж�һ��JSValueRef�����Ƿ�Ϊ�������͡�

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ���� | ˵��                                                      |
| ---- | --------------------------------------------------------- |
| bool | ���JSValueRef�������������ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
uint32_t length = 3;
Local<ArrayRef> arrayObject = ArrayRef::New(vm_, length);
bool b = arrayObject->IsArray(vm_);
```

### ToObject

Local<ObjectRef> JSValueRef::ToObject(const EcmaVM *vm);

��һ��JSValueRef����ת��ΪObjectRef����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����             | ˵��                                |
| ---------------- | ----------------------------------- |
| Local<ObjectRef> | ����ת����� ObjectRef ���͵Ķ��� |

**ʾ����**

```c++
int input = 0;
Local<IntegerRef> intValue = IntegerRef::New(vm_, input);
Local<ObjectRef> object = intValue->ToObject(vm_);
```

### ToNumber

Local<NumberRef> JSValueRef::ToNumber(const EcmaVM *vm);

��һ��JSValueRef����ת��ΪNumberRef����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����             | ˵��                                |
| ---------------- | ----------------------------------- |
| Local<NumberRef> | ����ת����� NumberRef ���͵Ķ��� |

**ʾ����**

```c++
Local<StringRef> toString = StringRef::NewFromUtf8(vm_, "-123.3");
Local<JSValueRef> toValue(toString);
Local<NumberRef> number = toString->ToNumber(vm_);
```

### ToBoolean

Local<BooleanRef> JSValueRef::ToBoolean(const EcmaVM *vm);

��һ��JSValueRef����ת��Ϊ����ֵ��BooleanRef����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����              | ˵��                                 |
| ----------------- | ------------------------------------ |
| Local<BooleanRef> | ����ת����� BooleanRef ���͵Ķ��� |

**ʾ����**

```c++
Local<IntegerRef> intValue = IntegerRef::New(vm_, 0);
Local<BooleanRef> boo = intValue->ToBoolean(vm_);
```

### IsObject

bool JSValueRef::IsObject();

�ж�һ��JSValueRef�����Ƿ�ΪObject���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                        |
| ---- | ----------------------------------------------------------- |
| bool | ���JSValueRef������Object���ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
Local<JSValueRef> res = ObjectRef::New(vm_);
bool b = res->IsObject();
```

### IsBigInt

bool JSValueRef::IsBigInt();

�ж�һ��JSValueRef�����Ƿ�ΪBigInt���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                        |
| ---- | ----------------------------------------------------------- |
| bool | ���JSValueRef����ΪBigInt���ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
int input = 2147483646;
Local<IntegerRef> intValue = IntegerRef::New(vm_, input);
bool b = intValue->IsBigInt();
```

### IsSymbol

bool JSValueRef::IsSymbol();

�ж�һ��JSValueRef�����Ƿ�ΪSymbol���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                        |
| ---- | ----------------------------------------------------------- |
| bool | ���JSValueRef������Symbol���ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
int input = 123;
Local<JSValueRef> res = IntegerRef::New(vm_,input);
bool b = res->IsSymbol();
```

### IsUndefined

bool JSValueRef::IsUndefined();

�ж�һ��JSValueRef�����Ƿ�Ϊδ�������͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                        |
| ---- | ----------------------------------------------------------- |
| bool | ���JSValueRef����Ϊδ�������ͣ�����true�����򣬷���false�� |

**ʾ����**

```c++
Local<JSValueRef> tag = JSValueRef::Undefined(vm_);
bool b = tag->IsUndefined();
```



### IsBoolean

bool JSValueRef::IsBoolean();

�ж�һ��JSValueRef�����Ƿ�Ϊ����ֵ��

**������**

��

**����ֵ��**

| ���� | ˵��                                                    |
| ---- | ------------------------------------------------------- |
| bool | ���JSValueRef����Ϊ����ֵ������true�����򣬷���false�� |

**ʾ����**

```c++
Local<JSValueRef> tag = BooleanRef::New(vm_,false);
bool b = tag->IsBoolean();
```

### IsNull

bool JSValueRef::IsNull();

�ж�һ��JSValueRef�����Ƿ�Ϊ�ա�

**������**

��

**����ֵ��**

| ���� | ˵��                                                |
| ---- | --------------------------------------------------- |
| bool | ���JSValueRef����Ϊ�գ�����true�����򣬷���false�� |

**ʾ����**

```c++
Local<JSValueRef> tag = JSValueRef::Null(vm_);
bool b = tag->IsNull();
```



### IsNativePointer

bool JSValueRef::IsNativePointer();

�ж�һ��JSValueRef�����Ƿ�Ϊ����ָ�롣

**������**

��

**����ֵ��**

| ���� | ˵��                                                      |
| ---- | --------------------------------------------------------- |
| bool | ���JSValueRef����Ϊ����ָ�룬����true�����򣬷���false�� |

**ʾ����**

```c++
void *vps = static_cast<void *>(new std::string("test"));
void *vps1 = static_cast<void *>(new std::string("test"));
Local<NativePointerRef> res_vps = 
    NativePointerRef::New(vm_, vps, NativeAreaAllocator::FreeBufferFunc, vps1, 0);
bool b = res_vps->IsNativePointer();
```

### IsFunction

bool JSValueRef::IsFunction();

�ж�һ��JSValueRef�����Ƿ�Ϊ������

**������**

��

**����ֵ��**

| ���� | ˵��                                                  |
| ---- | ----------------------------------------------------- |
| bool | ���JSValueRef����Ϊ����������true�����򣬷���false�� |

**ʾ����**

```c++
Local<FunctionRef> ConstructObjectFunctionRef(EcmaVM *vm, size_t nativeBindingsize){
    // ����һ��FunctionRef���Ͷ���obj
    // ...
    return obj; // ����obj
}
Local<FunctionRef> object = ConstructObjectFunctionRef(vm_, 15);
bool b = object->IsFunction();
```

### IsString

bool JSValueRef::IsString();

�ж�һ��JSValueRef�����Ƿ�Ϊ�ַ�����

**������**

��

**����ֵ��**

| ���� | ˵��                                                    |
| ---- | ------------------------------------------------------- |
| bool | ���JSValueRef����Ϊ�ַ���������true�����򣬷���false�� |

**ʾ����**

```c++
Local<JSValueRef> tag = StringRef::NewFromUtf8(vm_,"abc");
bool b = tag->IsString();
```

### IsNumber

bool JSValueRef::IsNumber();

�ж�һ��JSValueRef�����Ƿ�Ϊ���֡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                  |
| ---- | ----------------------------------------------------- |
| bool | ���JSValueRef����Ϊ���֣�����true�����򣬷���false�� |

**ʾ����**

```c++
Local<NumberRef> resUnit32 = NumberRef::New(vm_, inputUnit32);
bool b = resUnit32->IsNumber();
```

### True

static Local<PrimitiveRef> True(const EcmaVM *vm);

���һ��ֵ�Ƿ�ΪTrue����

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����                | ˵��                                                         |
| :------------------ | :----------------------------------------------------------- |
| Local<PrimitiveRef> | ����һ����ʾ����ֵtrue��JSValueRef����ת��ΪPrimitiveRef���͵ı������ã������ء� |

**ʾ����**

```c++
Local<BooleanRef> boolValue = BooleanRef::New(vm_, true);
Local<PrimitiveRef> result = boolValue->True(vm_);
```

### False

static Local<PrimitiveRef> False(const EcmaVM *vm);

���һ��ֵ�Ƿ�ΪFalse����

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����                | ˵��                                                         |
| :------------------ | :----------------------------------------------------------- |
| Local<PrimitiveRef> | ����һ����ʾ����ֵfalse��JSValueRef����ת��ΪPrimitiveRef���͵ı������ã������ء� |

**ʾ����**

```c++
Local<BooleanRef> boolValue = BooleanRef::New(vm_, false);
Local<PrimitiveRef> result = boolValue->True(vm_);
```

### IsFalse

bool IsFalse();

�����жϸö����Ƿ�ΪFalse����

**������**

��

**����ֵ��**

| ����    | ˵��                                           |
| :------ | :--------------------------------------------- |
| boolean | ����ö���Ϊfalse���򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
Local<BooleanRef> boolValue = BooleanRef::New(vm_, false);
bool result = boolValue->IsFalse();
```

### IsJSArray

bool IsJSArray(const EcmaVM *vm);

�����ж��Ƿ�ΪJS���������͡�

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����    | ˵��                                                  |
| :------ | :---------------------------------------------------- |
| boolean | ����ö�����JS���������ͣ��򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSArray *arr = JSArray::ArrayCreate(thread_, JSTaggedNumber(number)).GetObject<JSArray>();
JSHandle<JSTaggedValue> obj(thread_, arr);
Local<JSValueRef> JSArrayObject = JSNApiHelper::ToLocal<JSValueRef>(obj);
bool result = JSArrayObject->IsJSArray(vm_);
```

### IsConstructor

bool IsConstructor();

�����ж��Ƿ�Ϊ���캯�����͡�

**������**

��

**����ֵ��**

| ����    | ˵��                                                  |
| :------ | :---------------------------------------------------- |
| boolean | ����ö����ǹ��캯�����ͣ��򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
ObjectFactory *factory = vm_->GetFactory();
JSHandle<GlobalEnv> env = vm_->GetGlobalEnv();
JSHandle<JSFunction> func = factory->NewJSFunction(env, static_cast<void *>(nullptr), FunctionKind::BASE_CONSTRUCTOR);
JSHandle<JSObject> nullHandle(thread_, JSTaggedValue::Null());
JSHandle<JSObject> obj = JSObject::ObjectCreate(thread_, nullHandle);
JSHandle<JSTaggedValue> objValue(obj);
[[maybe_unused]] bool makeConstructor = JSFunction::MakeConstructor(thread_, func, objValue);
JSHandle<JSTaggedValue> funcHandle(func);
Local<JSValueRef> funConstructor = JSNApiHelper::ToLocal<JSValueRef>(funcHandle);
bool result = funConstructor->IsConstructor();
```

### IsWeakRef

bool IsWeakRef();

�����ж��Ƿ�ΪWeakRef����

**������**

��

**����ֵ��**

| ����    | ˵��                                             |
| :------ | :----------------------------------------------- |
| boolean | ����ö�����WeakRef���򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSFunction *jsFunc = vm_->GetGlobalEnv()->GetObjectFunction().GetObject<JSFunction>();
JSHandle<JSTaggedValue> jsFuncTagValue(thread, jsFunc);
JSHandle<JSObject> newObj = vm_->GetFactory()->NewJSObjectByConstructor(JSHandle<JSFunction>(jsFunc), jsFuncTagValue);
JSTaggedValue value(newObj);
value.CreateWeakRef();
bool result = JSNApiHelper::ToLocal<JSValueRef>(value);->IsWeakRef();
```

### IsArrayIterator

bool IsArrayIterator();

�����жϸö����Ƿ�Ϊ�����������

**������**

��

**����ֵ��**

| ����    | ˵��                                                |
| :------ | :-------------------------------------------------- |
| boolean | ����ö�����������������򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
ObjectFactory *factory = vm_->GetFactory();
JSHandle<TaggedArray> handleTaggedArrayFrom = vm_->GetFactory()->NewTaggedArray(arrayLength);
JSHandle<JSObject> handleJSObjectTaggedArrayFrom = JSArray::CreateArrayFromList(thread, handleTaggedArrayFrom); 
JSHandle<JSArrayIterator> handleJSArrayIter = 
    factory->NewJSArrayIterator(handleJSObjectTaggedArrayFrom,IterationKind::KEY);
JSHandle<JSTaggedValue> jsTagValue = JSHandle<JSTaggedValue>::Cast(handleJSArrayIter);
bool result = JSNApiHelper::ToLocal<JSValueRef>(jsTagValue)->IsStringIterator();
```

### IsStringIterator

bool IsStringIterator();

�����жϸö����Ƿ�Ϊ�ַ�����������

**������**

��

**����ֵ��**

| ����    | ˵��                                                    |
| :------ | :------------------------------------------------------ |
| boolean | ����ö������ַ����ĵ��������򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSHandle<EcmaString> recordName = vm_->GetFactory()->NewFromUtf8("646458");
JSHandle<JSStringIterator> jsStringIter = JSStringIterator::CreateStringIterator(vm_->GetJSThread(), recordName);
JSHandle<JSTaggedValue> setTag = JSHandle<JSTaggedValue>::Cast(jsStringIter);
bool result = JSNApiHelper::ToLocal<StringRef>(setTag)->IsStringIterator();
```

### IsJSPrimitiveInt

bool IsJSPrimitiveInt();

�����ж��Ƿ�ΪJS��ԭʼ�������͡�

**������**

��

**����ֵ��**

| ����    | ˵��                                                      |
| :------ | :-------------------------------------------------------- |
| boolean | ����ö�����JS��ԭʼ�������ͣ��򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSHandle<JSTaggedValue> jsTagValue;
JSHandle<JSPrimitiveRef> jsPrimitive = 
    vm_->GetFactory()->NewJSPrimitiveRef(PrimitiveType::PRIMITIVE_BIGINT, jsTagValue);
JSHandle<JSTaggedValue> jsPriTagValue = JSHandle<JSTaggedValue>::Cast(jsPrimitive);
bool result = JSNApiHelper::ToLocal<JSValueRef>(jsPriTagValue)->IsJSPrimitiveInt();
```

### IsTreeMap

bool IsTreeMap();

�����жϸö����Ƿ�ΪTreeMap���͡�

**������**

��

**����ֵ��**

| ����    | ˵��                                                 |
| :------ | :--------------------------------------------------- |
| boolean | ����ö�����TreeMap���ͣ��򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSHandle<JSTaggedValue> proto = vm_->GetGlobalEnv()->GetObjectFunctionPrototype();
JSHandle<JSHClass> mapClass = vm_->GetFactory()->NewEcmaHClass(JSAPITreeMap::SIZE, JSType::JS_API_TREE_MAP, proto);
JSHandle<JSAPITreeMap> jsTreeMap = JSHandle<JSAPITreeMap>::Cast(factory->NewJSObjectWithInit(mapClass));
JSHandle<TaggedTreeMap> treeMap(thread, TaggedTreeMap::Create(thread));
jsTreeMap->SetTreeMap(thread, treeMap);
JSHandle<JSTaggedValue> treeMapTagValue = JSHandle<JSTaggedValue>::Cast(jsTreeMap);
bool result = JSNApiHelper::ToLocal<ArrayRef>(treeMapTagValue)->IsTreeMap();
```

### IsTreeSet

bool IsTreeSet();

�����жϸö����Ƿ�Ϊ������

**������**

��

**����ֵ��**

| ����    | ˵��                                          |
| :------ | :-------------------------------------------- |
| boolean | ����ö������������򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSHandle<JSTaggedValue> proto = vm_->GetGlobalEnv()->GetObjectFunctionPrototype();
JSHandle<JSHClass> setClass = vm_->GetFactory()->NewEcmaHClass(JSAPITreeSet::SIZE, JSType::JS_API_TREE_SET, proto);
JSHandle<JSAPITreeSet> jsTreeSet = JSHandle<JSAPITreeSet>::Cast(factory->NewJSObjectWithInit(setClass));
JSHandle<TaggedTreeSet> treeSet(thread, TaggedTreeSet::Create(thread));
jsTreeSet->SetTreeSet(thread, treeSet);
JSHandle<JSTaggedValue> treeSetTagValue = JSHandle<JSTaggedValue>::Cast(jsTreeSet);
bool result = JSNApiHelper::ToLocal<ArrayRef>(treeSetTagValue)->IsTreeSet();
```

### IsVector

bool IsVector();

�����жϸö����Ƿ�ΪVector������

**������**

��

**����ֵ��**

| ����    | ˵��                                                |
| :------ | :-------------------------------------------------- |
| boolean | ����ö�����Vector�������򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSHandle<JSTaggedValue> proto = vm_->GetGlobalEnv()->GetFunctionPrototype();
JSHandle<JSHClass> vectorClass = vm_->GetFactory()->NewEcmaHClass(JSAPIVector::SIZE, JSType::JS_API_VECTOR, proto);
JSHandle<JSAPIVector> jsVector = JSHandle<JSAPIVector>::Cast(factory->NewJSObjectWithInit(vectorClass));
jsVector->SetLength(length);
JSHandle<JSTaggedValue> vectorTagValue = JSHandle<JSTaggedValue>::Cast(jsVector);
bool result = JSNApiHelper::ToLocal<ArrayRef>(vectorTagValue)->IsVector();
```

### IsArgumentsObject

bool IsArgumentsObject();

�����жϸö����Ƿ�Ϊ��������

**������**

��

**����ֵ��**

| ����    | ˵��                                              |
| :------ | :------------------------------------------------ |
| boolean | ����ö����ǲ��������򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSHandle<JSArguments> obj = vm_->GetFactory()->NewJSArguments();
JSHandle<JSTaggedValue> argumentTag = JSHandle<JSTaggedValue>::Cast(obj);
bool result = JSNApiHelper::ToLocal<ObjectRef>(argumentTag)->IsArgumentsObject();
```

### IsAsyncFunction

bool IsAsyncFunction();

�����ж��Ƿ�Ϊ�첽������

**������**

��

**����ֵ��**

| ����    | ˵��                                        |
| :------ | :------------------------------------------ |
| boolean | ������첽�������򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSHandle<JSAsyncFuncObject> asyncFuncObj = vm_->GetFactory()->NewJSAsyncFuncObject();
JSHandle<JSTaggedValue> asyncFuncTag = JSHandle<JSTaggedValue>::Cast(asyncFuncObj);
bool result = JSNApiHelper::ToLocal<ObjectRef>(asyncFuncTag)->IsAsyncFunction();
```

### IsGeneratorFunction

bool IsGeneratorFunction();

�����ж��Ƿ�Ϊ������������

**������**

��

**����ֵ��**

| ����    | ˵��                                                |
| :------ | :-------------------------------------------------- |
| boolean | ����ö������������������򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSHandle<JSTaggedValue> genFunc = vm_->GetGlobalEnv()->GetGeneratorFunctionFunction();
JSHandle<JSGeneratorObject> genObjHandleVal = vm_->GetFactory()->NewJSGeneratorObject(genFunc);
JSHandle<JSTaggedValue> genObjTagHandleVal = JSHandle<JSTaggedValue>::Cast(genObjHandleVal);
Local<GeneratorObjectRef> genObjectRef = JSNApiHelper::ToLocal<GeneratorObjectRef>(genObjTagHandleVal);
Local<JSValueRef> object = genObjectRef->GetGeneratorFunction(vm_);
bool result = res->IsGeneratorFunction();
```

### IsBigInt64Array

bool IsBigInt64Array();

�����жϸö����Ƿ�Ϊ���ڴ洢���ⳤ�ȵ��з���64λ�������顣

**������**

��

**����ֵ��**

| ����    | ˵��                                                         |
| :------ | :----------------------------------------------------------- |
| boolean | ����ö��������ⳤ�ȵ��з���64λ�������飬�򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm_, bufferLength);
Local<ObjectRef> object = BigInt64ArrayRef::New(vm_, buffer, bufferOffset, offsetLength);
bool result = object->IsBigInt64Array();
```

### IsBigUint64Array

bool IsBigUint64Array();

�����жϸö����Ƿ�Ϊ���ڴ洢���ⳤ�ȵ��޷���64λ�������顣

**������**

��

**����ֵ��**

| ����    | ˵��                                                         |
| :------ | :----------------------------------------------------------- |
| boolean | ����ö��������ⳤ�ȵ��޷���64λ�������飬�򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm_, 5);
Local<ObjectRef> object = BigUint64ArrayRef::New(vm_, buffer, bufferOffset, offsetLength);
bool result = object->IsBigUint64Array();
```

### IsSharedArrayBuffer

bool IsSharedArrayBuffer();

�����жϸö����Ƿ�Ϊ�����ArrayBuffer����

**������**

��

**����ֵ��**

| ����    | ˵��                                                       |
| :------ | :--------------------------------------------------------- |
| boolean | ����ö����ǹ����ArrayBuffer���򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
JSHandle<JSArrayBuffer> jsArrayBuffer = vm_->GetFactory()->NewJSSharedArrayBuffer(bufferLength);
JSHandle<JSTaggedValue> jsTagValueBuffer = JSHandle<JSTaggedValue>::Cast(jsArrayBuffer);
bool result = JSNApiHelper::ToLocal<ArrayRef>(jsTagValueBuffer)->IsSharedArrayBuffer();
```

### IsUint8ClampedArray

bool JSValueRef::IsUint8ClampedArray();

�ж϶����Ƿ�ΪUint8ClampedArray���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                       |
| ---- | ---------------------------------------------------------- |
| bool | ������ö��������ΪUint8ClampedArray����True���򷵻�False |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm, arrayLenth);
Local<JSValueRef> val = Uint8ClampedArrayRef::New(vm, buffer, Offset, OffsetLenth);
bool res = val->IsUint8ClampedArray();
```

### IsInt16Array

bool JSValueRef::IsInt16Array();

�ж϶����Ƿ�ΪInt16Array���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                |
| ---- | --------------------------------------------------- |
| bool | ������ö��������ΪInt16Array����True���򷵻�False |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm, arrayLenth);
Local<JSValueRef> val = Int16ArrayRef::New(vm, buffer, Offset, OffsetLenth);
bool res = val->IsInt16Array();
```

### IsUint16Array

bool JSValueRef::IsUint16Array();

�ж϶����Ƿ�ΪUint16Array���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                 |
| ---- | ---------------------------------------------------- |
| bool | ������ö��������ΪUint16Array����True���򷵻�False |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm, arrayLenth);
Local<JSValueRef> val = Uint16ArrayRef::New(vm, buffer, Offset, OffsetLenth);
bool res = val->IsUint16Array();
```

### IsInt32Array

bool JSValueRef::IsInt32Array();

�ж϶����Ƿ�ΪInt32Array���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                |
| ---- | --------------------------------------------------- |
| bool | ������ö��������ΪInt32Array����True���򷵻�False |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm, 30);
Local<JSValueRef> val = Int32ArrayRef::New(vm, buffer, 4, 6);
bool res = val->IsInt32Array();
```

### IsUint32Array

bool JSValueRef::IsUint32Array();

�ж϶����Ƿ�ΪUint32Array���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                 |
| ---- | ---------------------------------------------------- |
| bool | ������ö��������ΪUint32Array����True���򷵻�False |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm, 30);
Local<JSValueRef> val = Uint32ArrayRef::New(vm, buffer, 4, 6);
bool res = val->IsUint32Array();
```

### IsFloat32Array

bool JSValueRef::IsFloat32Array();

�ж϶����Ƿ�ΪFloat32Array���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                  |
| ---- | ----------------------------------------------------- |
| bool | ������ö��������ΪFloat32Array����True���򷵻�False |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm, 30);
Local<JSValueRef> val = Float32ArrayRef::New(vm, buffer, 4, 6);
bool res = val->IsFloat32Array();
```

### IsFloat64Array

bool JSValueRef::IsFloat64Array();

�ж϶����Ƿ�ΪFloat64Array���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                  |
| ---- | ----------------------------------------------------- |
| bool | ������ö��������ΪFloat64Array����True���򷵻�False |

**ʾ����**

```c++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm, arrayLength);
Local<JSValueRef> val = Float64ArrayRef::New(vm, buffer, eOffset, eOffsetLength);
bool res = val->IsFloat64Array();
```

### IsJSPrimitiveBoolean

bool JSValueRef::IsJSPrimitiveBoolean();

�ж϶����Ƿ�ΪJSPrimitiveBoolean���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                        |
| ---- | ----------------------------------------------------------- |
| bool | ������ö��������ΪJSPrimitiveBoolean����True���򷵻�False |

**ʾ����**

```c++
ObjectFactory *factory = vm->GetFactory();
    JSHandle<JSTaggedValue> jstagvalue;
    JSHandle<JSPrimitiveRef> jsprimitive = factory->NewJSPrimitiveRef(PrimitiveType::PRIMITIVE_BOOLEAN, jstagvalue);
    JSHandle<JSTaggedValue> jspri = JSHandle<JSTaggedValue>::Cast(jsprimitive);
    Local<JSValueRef> object = JSNApiHelper::ToLocal<JSValueRef>(jspri);
bool res = object->IsJSPrimitiveBoolean();
```

### IsMapIterator

bool JSValueRef::IsMapIterator();

�ж϶����Ƿ�ΪMapIterator���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                 |
| ---- | ---------------------------------------------------- |
| bool | ������ö��������ΪMapIterator����True���򷵻�False |

**ʾ����**

```c++
JSHandle<GlobalEnv> env = thread_->GetEcmaVM()->GetGlobalEnv();
ObjectFactory *factory = thread_->GetEcmaVM()->GetFactory();
JSHandle<JSTaggedValue> builtinsMapFunc = env->GetBuiltinsMapFunction();
JSHandle<JSMap> jsMap(factory->NewJSObjectByConstructor(JSHandle<JSFunction>(builtinsMapFunc), builtinsMapFunc));
JSHandle<JSTaggedValue> linkedHashMap(LinkedHashMap::Create(thread_));
jsMap->SetLinkedMap(thread_, linkedHashMap);
JSHandle<JSTaggedValue> mapIteratorVal =
    JSMapIterator::CreateMapIterator(thread_, JSHandle<JSTaggedValue>::Cast(jsMap), IterationKind::KEY);
Local<MapIteratorRef> object = JSNApiHelper::ToLocal<MapIteratorRef>(mapIteratorVal);
bool res = object->IsMapIterator();
```

### IsSetIterator

bool JSValueRef::IsSetIterator();

�ж϶����Ƿ�ΪSetIterator���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                 |
| ---- | ---------------------------------------------------- |
| bool | ������ö��������ΪSetIterator����True���򷵻�False |

**ʾ����**

```c++
ObjectFactory *factory = vm->GetFactory();
JSHandle<JSTaggedValue> proto = thread_->GetEcmaVM()->GetGlobalEnv()->GetFunctionPrototype();
JSHandle<JSHClass> setClass = factory->NewEcmaHClass(JSSet::SIZE, JSType::JS_SET, proto);
JSHandle<JSSet> jsSet = JSHandle<JSSet>::Cast(factory->NewJSObjectWithInit(setClass));
JSHandle<LinkedHashSet> linkedSet(LinkedHashSet::Create(thread_));
jsSet->SetLinkedSet(thread_, linkedSet);
JSHandle<JSSetIterator> jsSetIter = factory->NewJSSetIterator(jsSet, IterationKind::KEY);
JSHandle<JSTaggedValue> setIter = JSHandle<JSTaggedValue>::Cast(jsSetIter);
bool res = JSNApiHelper::ToLocal<JSValueRef>(setiter)->IsSetIterator();
```

### IsModuleNamespaceObject

bool JSValueRef::IsModuleNamespaceObject();

�ж϶����Ƿ�ΪModuleNamespaceObject���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ������ö��������ΪModuleNamespaceObject����True���򷵻�False |

**ʾ����**

```c++
ObjectFactory *factory = vm->GetFactory();
JSHandle<ModuleNamespace> moduleNamespace = factory->NewModuleNamespace();
JSHandle<JSTaggedValue> modname = JSHandle<JSTaggedValue>::Cast(moduleNamespace);
JSNApiHelper::ToLocal<ObjectRef>(modname)->IsModuleNamespaceObject();
bool res =object->IsModuleNamespaceObject()
```

### IsProxy

bool JSValueRef::IsProxy();

�ж϶����Ƿ�ΪProxy���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                           |
| ---- | ---------------------------------------------- |
| bool | ������ö��������ΪProxy����True���򷵻�False |

**ʾ����**

```c++
Local<ProxyRef> tag = ProxyRef::New(vm);
bool res = tag->IsProxy();
```

### IsRegExp

bool JSValueRef::IsRegExp();

�ж϶����Ƿ�ΪRegExp���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                            |
| ---- | ----------------------------------------------- |
| bool | ������ö��������ΪRegExp����True���򷵻�False |

**ʾ����**

```c++
Local<RegExp> val = RegExp::New(vm);
bool res = val->IsRegExp();
```

### IsJSPrimitiveNumber

bool JSValueRef::IsJSPrimitiveNumber();

�ж϶����Ƿ�ΪJSPrimitiveNumber���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                       |
| ---- | ---------------------------------------------------------- |
| bool | ������ö��������ΪJSPrimitiveNumber����True���򷵻�False |

**ʾ����**

```c++
ObjectFactory *factory = vm->GetFactory();
JSHandle<JSTaggedValue> jstagvalue;
JSHandle<JSPrimitiveRef> jsprimitive = factory->NewJSPrimitiveRef(PrimitiveType::PRIMITIVE_NUMBER, jstagvalue);
JSHandle<JSTaggedValue> jspri = JSHandle<JSTaggedValue>::Cast(jsprimitive);
Local<JSValueRef> object = JSNApiHelper::ToLocal<JSValueRef>(jspri);
bool res = object->IsJSPrimitiveNumber();
```

### IsMap

bool JSValueRef::IsMap();

�ж϶����Ƿ�ΪMap���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                         |
| ---- | -------------------------------------------- |
| bool | ������ö��������ΪMap����True���򷵻�False |

**ʾ����**

```c++
Local<MapRef> mapRef = MapRef::New(vm);
bool res = mapRef->IsMap();
```

### IsSet

bool JSValueRef::IsSet();

�ж϶����Ƿ�ΪSet���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                         |
| ---- | -------------------------------------------- |
| bool | ������ö��������ΪSet����True���򷵻�False |

**ʾ����**

```c++
Local<SetRef> setRef = SetRef::New(vm);
bool res = setRef->IsSet();
```

### IsJSPrimitiveString

bool JSValueRef::IsJSPrimitiveString();

�ж϶����Ƿ�ΪJSPrimitiveString���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                       |
| ---- | ---------------------------------------------------------- |
| bool | ������ö��������ΪJSPrimitiveString����True���򷵻�False |

**ʾ����**

```c++
ObjectFactory *factory = vm->GetFactory();
JSHandle<JSTaggedValue> jstagValue;
JSHandle<JSPrimitiveRef> jsprimitive = factory->NewJSPrimitiveRef(PrimitiveType::PRIMITIVE_STRING, jstagvalue);
JSHandle<JSTaggedValue> jspString = JSHandle<JSTaggedValue>::Cast(jsprimitive);
Local<JSValueRef> object = JSNApiHelper::ToLocal<JSValueRef>(jspString);
bool res = object->IsJSPrimitiveNumber();
```

### IsJSPrimitiveSymbol

bool JSValueRef::IsJSPrimitiveSymbol();

�ж϶����Ƿ�ΪJSPrimitiveSymbol���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                                       |
| ---- | ---------------------------------------------------------- |
| bool | ������ö��������ΪJSPrimitiveSymbol����True���򷵻�False |

**ʾ����**

```c++
ObjectFactory *factory = vm->GetFactory();
JSHandle<JSTaggedValue> jstagValue;
JSHandle<JSPrimitiveRef> jsprimitive = factory->NewJSPrimitiveRef(PrimitiveType::PRIMITIVE_SYMBOL, jstagvalue);
JSHandle<JSTaggedValue> jspSymbol = JSHandle<JSTaggedValue>::Cast(jsprimitive);
Local<JSValueRef> object = JSNApiHelper::ToLocal<JSValueRef>(jspSymbol);
bool res = object->IsJSPrimitiveNumber();
```

### IsWeakMap

bool JSValueRef::IsWeakMap();

�ж϶����Ƿ�ΪWeakMap���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                             |
| ---- | ------------------------------------------------ |
| bool | ������ö��������ΪWeakMap����True���򷵻�False |

**ʾ����**

```c++
Local<JSValueRef> tag = WeakMapRef::New(vm);
bool res = tag->IsWeakMap();
```

### IsWeakSet

bool JSValueRef::IsWeakSet();

�ж϶����Ƿ�ΪWeakSet���͡�

**������**

��

**����ֵ��**

| ���� | ˵��                                             |
| ---- | ------------------------------------------------ |
| bool | ������ö��������ΪWeakSet����True���򷵻�False |

**ʾ����**

```c++
Local<JSValueRef> tag = WeakSetRef::New(vm);
bool res = tag->IsWeakSet();
```



## ObjectRef

�̳���JSValueRef���ṩ��һЩ���������ڻ�ȡ��������һЩJSValueRef���͵�ֵ��

### New

static Local<ObjectRef> New(const EcmaVM *vm);

���ڹ���һ��ObjectRef��Ķ���

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����             | ˵��                              |
| :--------------- | :-------------------------------- |
| Local<ObjectRef> | ���ع���ɹ���ObjectRef��Ķ��� |

**ʾ����**

```c++
 Local<ObjectRef> result = ObjectRef::New(vm_);
```

### GetPrototype

Local<JSValueRef> GetPrototype(const EcmaVM *vm);

��֤�Ƿ���ȷ���غ���������ԭ�ͣ�����֤���ص�ԭ���Ƿ�Ϊ�������͡�

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����              | ˵��                                           |
| :---------------- | :--------------------------------------------- |
| Local<JSValueRef> | ����ȡ���Ķ���ԭ��ת��ΪJSValueRef���Ͳ����ء� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
Local<JSValueRef> result = object->GetPrototype(vm_);
```

### GetOwnPropertyNames

Local<ArrayRef> GetOwnPropertyNames(const EcmaVM *vm);

���ڻ�ȡ�ö������е������������ơ�

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����            | ˵��                                   |
| :-------------- | :------------------------------------- |
| Local<ArrayRef> | ���ش洢�ö������������������Ƶ����顣 |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
Local<JSValueRef> value = ObjectRef::New(vm_);
PropertyAttribute attribute(value, true, true, true);
Local<ArrayRef> result = object->GetOwnPropertyNames(vm_);
```

### Set

bool Set(const EcmaVM *vm, uint32_t key, Local<JSValueRef> value);

��������ObjectRef���������ֵ��

**������**

| ������ | ����              | ���� | ˵��               |
| ------ | ----------------- | ---- | ------------------ |
| vm     | const EcmaVM *    | ��   | ���������         |
| key    | uint32_t          | ��   | ָ����keyֵ        |
| value  | Local<JSValueRef> | ��   | keyֵ��Ӧ��valueֵ |

**����ֵ��**

| ����    | ˵��                                                       |
| :------ | :--------------------------------------------------------- |
| boolean | ObjectRef���������ֵ���óɹ����򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
Local<FunctionRef> object = ObjectRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = ObjectRef::New(vm_);
bool result = object->Set(vm_, key, value);
```

### Has

bool Has(const EcmaVM *vm, Local<JSValueRef> key);

���ڼ��ObjectRef�����Ƿ����ָ���ļ���

**������**

| ������ | ����              | ���� | ˵��        |
| ------ | ----------------- | ---- | ----------- |
| vm     | const EcmaVM *    | ��   | ���������  |
| key    | Local<JSValueRef> | ��   | ָ����keyֵ |

**����ֵ��**

| ����    | ˵��                                                         |
| :------ | :----------------------------------------------------------- |
| boolean | �����ObjectRef�������ָ���ļ����򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
bool result = object->Has(vm_, key);
```

### Delete

bool Delete(const EcmaVM *vm, Local<JSValueRef> key);

���ڸ���ָ���ļ�ֵɾ��ObjectRef���������ֵ��

**������**

| ������ | ����              | ���� | ˵��        |
| ------ | ----------------- | ---- | ----------- |
| vm     | const EcmaVM *    | ��   | ���������  |
| key    | Local<JSValueRef> | ��   | ָ����keyֵ |

**����ֵ��**

| ����    | ˵��                                                         |
| :------ | :----------------------------------------------------------- |
| boolean | ObjectRef���������ֵ�ɹ���ɾ�����򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_, reinterpret_cast<void *>(detach1), reinterpret_cast<void *>(attach1));
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
bool result = object->Delete(vm_, key);
```

### Get

Local<JSValueRef> Get(const EcmaVM *vm, int32_t key);

����ָ����keyֵ��ȡ�����valueֵ��

**������**

| ������ | ����           | ���� | ˵��        |
| ------ | -------------- | ---- | ----------- |
| vm     | const EcmaVM * | ��   | ���������  |
| key    | int32_t        | ��   | ָ����keyֵ |

**����ֵ��**

| ����              | ˵��                                   |
| :---------------- | :------------------------------------- |
| Local<JSValueRef> | ����ֵΪ����ָ����keyֵ��ȡ��valueֵ�� |

**ʾ����**

```c++
Local<FunctionRef> object = ObjectRef::New(vm_);
int32_t key = 123;
Local<JSValueRef> result = object->Get(vm_, key);
```

### Delete

bool Delete(const EcmaVM *vm, uint32_t key);

���ڸ���ָ���ļ�ֵɾ��ObjectRef���������ֵ��

**������**

| ������ | ����           | ���� | ˵��        |
| ------ | -------------- | ---- | ----------- |
| vm     | const EcmaVM * | ��   | ���������  |
| key    | uint32_t       | ��   | ָ����keyֵ |

**����ֵ��**

| ����    | ˵��                                                         |
| :------ | :----------------------------------------------------------- |
| boolean | ObjectRef���������ֵ�ɹ���ɾ�����򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_, reinterpret_cast<void *>(detach1), reinterpret_cast<void *>(attach1));
uint32_t key = 123;
bool result = object->Delete(vm_, key);
```

### DefineProperty

bool DefineProperty(const EcmaVM *vm, Local<JSValueRef> key, PropertyAttribute attribute);

��������Keyֵ����Ӧ������ֵ��

**������**

| ������    | ����              | ���� | ˵��           |
| --------- | ----------------- | ---- | -------------- |
| vm        | const EcmaVM *    | ��   | ���������     |
| key       | Local<JSValueRef> | ��   | ָ����keyֵ    |
| attribute | PropertyAttribute | ��   | Ҫ���õ�����ֵ |

**����ֵ��**

| ����    | ˵��                                                       |
| :------ | :--------------------------------------------------------- |
| boolean | ObjectRef���������ֵ���óɹ����򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_, reinterpret_cast<void *>(detach1), reinterpret_cast<void *>(attach1));
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = ObjectRef::New(vm_);
PropertyAttribute attribute(value, true, true, true);
bool result = object->DefineProperty(vm_, key, attribute);
```

### GetAllPropertyNames

Local<ArrayRef> GetAllPropertyNames(const EcmaVM *vm, uint32_t filter);

���ڻ�ȡ�����������������������һ��Local<ArrayRef>���͵Ľ����

**������**

| ������ | ����           | ���� | ˵��           |
| ------ | -------------- | ---- | -------------- |
| vm     | const EcmaVM * | ��   | ���������     |
| filter | uint32_t       | ��   | ָ���Ĺ������� |

**����ֵ��**

| ����            | ˵��                         |
| :-------------- | :--------------------------- |
| Local<ArrayRef> | ����ֵΪ��ȡ���������������� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
uint32_t filter = 123;
Local<ArrayRef> result = object->GetAllPropertyNames(vm_, filter);
```

### GetOwnEnumerablePropertyNames

Local<ArrayRef> GetOwnEnumerablePropertyNames(const EcmaVM *vm);

��ȡ������������п�ö����������������һ��Local<ArrayRef>���͵Ľ����

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����            | ˵��                               |
| :-------------- | :--------------------------------- |
| Local<ArrayRef> | ����ֵΪ��ȡ�������п�ö���������� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
Local<ArrayRef> result = object->GetOwnEnumerablePropertyNames(vm_);
```

### Get

Local<JSValueRef> ObjectRef::Get(const EcmaVM *vm, Local<JSValueRef> key);

��һ��JavaScript�����л�ȡһ�����Ե�ֵ��

**������**

| ������ | ����              | ���� | ˵��                                                       |
| ------ | ----------------- | ---- | ---------------------------------------------------------- |
| vm     | const EcmaVM *    | ��   | ���������                                               |
| key    | Local<JSValueRef> | ��   | Ҫ��ȡ�����Եļ���������������ַ��������ֻ��������͵�ֵ�� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ����ȡ��������ֵת��ΪJSValueRef���ͣ���ͨ��scope.Escape(JSNApiHelper::ToLocal<JSValueRef>(ret.GetValue()))����ת�ؾֲ������������ظ�ֵ�� |

**ʾ����**

```c++
Local<FunctionRef> object = ObjectRef::New(vm_);
int32_t key = 123;
Local<JSValueRef> value = object->Get(vm_, key);
```

### GetOwnProperty

��ȡ��������ԡ�

bool ObjectRef::GetOwnProperty(const EcmaVM *vm, Local<JSValueRef> key, PropertyAttribute &property);

**������**

| ������   | ����                | ���� | ˵��                         |
| -------- | ------------------- | ---- | ---------------------------- |
| vm       | const EcmaVM *      | ��   | ���������                 |
| key      | Local<JSValueRef>   | ��   | Ҫ��ȡ�����Եļ���           |
| property | PropertyAttribute & | ��   | ���ڴ洢��ȡ�������Ե���Ϣ�� |

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ����ɹ���ȡ�������������������Ὣ����ֵ��getter��setter����д�ԡ���ö���ԺͿ������Ե���Ϣ���õ�property�����С���󣬷���`true`��ʾ�ɹ���ȡ���ԡ����򣬷���false�� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = ObjectRef::New(vm_);
PropertyAttribute attribute(value, true, true, true);
bool b = object->GetOwnProperty(vm_, key, attribute);
```

### SetAccessorProperty

bool ObjectRef::SetAccessorProperty(const EcmaVM *vm, Local<JSValueRef> key, Local<FunctionRef> getter, Local<FunctionRef> setter, PropertyAttribute attribute);

���ö�������ԡ�

**������**

| ������    | ����               | ���� | ˵��                                             |
| --------- | ------------------ | ---- | ------------------------------------------------ |
| vm        | const EcmaVM *     | ��   | ���������                                     |
| key       | Local<JSValueRef>  | ��   | Ҫ���õ����Եļ���                               |
| getter    | Local<FunctionRef> | ��   | ��ʾ���Ե�getter������                           |
| setter    | Local<FunctionRef> | ��   | ��ʾ���Ե�setter������                           |
| attribute | PropertyAttribute  | ��   | ��ʾ���Ե����ԣ����д�ԡ���ö���ԺͿ������ԣ��� |

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ��ʾ�Ƿ�ɹ����������ԡ�������óɹ�������true�����򣬷���false�� |

**ʾ����**

```c++
Local<FunctionRef> object = ObjectRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<FunctionRef> target1 = FunctionRef::New(vm_, nullptr);
Local<FunctionRef> target2 = FunctionRef::New(vm_, nullptr);
bool b = object->SetAccessorProperty(vm_, key, target1, target2);
```

### GetNativePointerField

void *ObjectRef::GetNativePointerField(int32_t index);

��ȡһ�������ָ����������ԭ��ָ���ֶΡ�

**������**

| ������ | ����    | ���� | ˵��                                 |
| ------ | ------- | ---- | ------------------------------------ |
| index  | int32_t | ��   | ����ָ��Ҫ��ȡ��ԭ��ָ���ֶε������� |

**����ֵ��**

| ����   | ˵��                             |
| ------ | -------------------------------- |
| void * | ��ʾ����һ��ָ���������͵�ָ�롣 |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
NativePointerCallback callBack = nullptr;
void *vp1 = static_cast<void *>(new std::string("test"));
void *vp2 = static_cast<void *>(new std::string("test"));
object->SetNativePointerField(33, vp1, callBack, vp2);
void *ptr = object.GetNativePointerField(33);
```

### SetNativePointerFieldCount

void ObjectRef::SetNativePointerFieldCount(int32_t count);

����һ������ı���ָ���ֶε�������

**������**

| ������ | ����    | ���� | ˵��                             |
| ------ | ------- | ---- | -------------------------------- |
| count  | int32_t | ��   | ָ��Ҫ���õı���ָ���ֶε������� |

**����ֵ��**

��

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
int32_t input = 34;
object->SetNativePointerFieldCount(input);
```

### set

bool Set(const EcmaVM *vm, Local<JSValueRef> key, Local<JSValueRef> value);

bool Set(const EcmaVM *vm, uint32_t key, Local<JSValueRef> value);

�ڵ�ǰ `ObjectRef` ���������ü�ֵ�ԡ�

������֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `ObjectRef` ����ת��Ϊ JavaScript �е� `JSTaggedValue` ����

ʹ�� `JSNApiHelper::ToJSHandle(key)` �� `JSNApiHelper::ToJSHandle(value)` ������ֵת��Ϊ JavaScript �е� `JSTaggedValue` ����

���� `JSTaggedValue::SetProperty` �����ڶ��������ü�ֵ�ԡ�

**������**

| ������  | ����              | ���� | ˵��             |
| ------- | ----------------- | ---- | ---------------- |
| vm      | const EcmaVM *    | ��   | ���������     |
| key   | Local<JSValueRef> | ��   | ��ʾҪ���õļ��� |
| key   | uint32_t        | ��   | ��ʾҪ���õļ��� |
| value | Local<JSValueRef> | ��   | ��ʾҪ���õ�ֵ�� |

**����ֵ��**

| ���� | ˵��                        |
| ---- | --------------------------- |
| bool | ���óɹ�����True��֮False�� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
Local<StringRef> toString = StringRef::NewFromUtf8(vm_, "-123.3");
Local<JSValueRef> toValue(toString);
bool res = object->Set(vm_, key, toValue);
bool res = object->Set(vm_, toValue, toValue);
```

###  Has

bool Has(const EcmaVM *vm, uint32_t key);

 ��鵱ǰ `ObjectRef` �����Ƿ����ָ���������ԡ�

 �ڼ��֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `ObjectRef` ����ת��Ϊ JavaScript �е� `JSTaggedValue` ����

���� `JSTaggedValue::HasProperty` �����������Ƿ����ָ���������ԡ�

**������**

| ������ | ����           | ���� | ˵��                 |
| ------ | -------------- | ---- | -------------------- |
| vm     | const EcmaVM * | ��   | ���������         |
| key    | uint32_t       | ��   | ��ʾҪ�������Լ��� |

**����ֵ��**

| ���� | ˵��                      |
| ---- | ------------------------- |
| bool | ����з���True��֮False�� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
bool res = object->Has(vm_, key);
```

###  SetNativePointerField

void SetNativePointerField(int32_t index,void *nativePointer = nullptr,NativePointerCallback callBack = nullptr,void *data = nullptr, size_t nativeBindingsize = 0);

�ڵ�ǰ `ObjectRef` ����������ָ�������ı���ָ���ֶΡ�

������֮ǰ��ͨ�� `LOG_IF_SPECIAL` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `ObjectRef` ����ת��Ϊ JavaScript �е� `JSObject` ����

���� `JSObject::SetNativePointerField` �������ñ���ָ���ֶΡ�

**������**

| ������            | ����                  | ���� | ˵��                             |
| ----------------- | --------------------- | ---- | -------------------------------- |
| index             | int32_t               | ��   | ��ʾҪ���õı���ָ���ֶε������� |
| nativePointer     | void*                 | ��   | ��ʾҪ���õı���ָ�롣           |
| callBack          | NativePointerCallback | ��   | ��ʾ����ָ��Ļص�������         |
| data              | void*                 | ��   | ��ʾ�뱾��ָ������������ݡ�     |
| nativeBindingsize | size_t                | ��   | ��ʾ�����󶨵Ĵ�С��             |

**����ֵ��**

��

**ʾ����**

```c++
NativePointerCallback callBack = nullptr;
void *vp1 = static_cast<void*>(new std::string("test"));
void *vp2 = static_cast<void*>(new std::string("test"));
object->SetNativePointerField(index, vp1, callBack, vp2);
```

###  Freeze

static void TriggerGC(const EcmaVM *vm, TRIGGER_GC_TYPE gcType = TRIGGER_GC_TYPE::SEMI_GC);

�ú������ڴ����������գ�GC������������ѡ��ͬ�������������͡�

ͨ������������� CollectGarbage ����ִ���������ղ�����

�ڴ�����������֮ǰ��ͨ�� CHECK_HAS_PENDING_EXCEPTION_WITHOUT_RETURN �����Ƿ����Ǳ�ڵ��쳣��

**������**

| ������   | ����            | ���� | ˵��                                           |
| -------- | --------------- | ---- | ---------------------------------------------- |
| vm       | const EcmaVM *  | ��   | ���������                                   |
| gcType | TRIGGER_GC_TYPE | ��   | ��һ��ö�����ͣ���ʾ����GC���������գ������͡� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::TriggerGC(vm);
```

###  Seal

static void TriggerGC(const EcmaVM *vm, TRIGGER_GC_TYPE gcType = TRIGGER_GC_TYPE::SEMI_GC);

�ú������ڴ����������գ�GC������������ѡ��ͬ�������������͡�

ͨ������������� CollectGarbage ����ִ���������ղ�����

�ڴ�����������֮ǰ��ͨ�� CHECK_HAS_PENDING_EXCEPTION_WITHOUT_RETURN �����Ƿ����Ǳ�ڵ��쳣��

**������**

| ������   | ����            | ���� | ˵��                                           |
| -------- | --------------- | ---- | ---------------------------------------------- |
| vm       | const EcmaVM *  | ��   | ���������                                   |
| gcType | TRIGGER_GC_TYPE | ��   | ��һ��ö�����ͣ���ʾ����GC���������գ������͡� |

**����ֵ��**

��

**ʾ����**

```c++
JSNApi::TriggerGC(vm);
```

###  New

Local<ObjectRef> ObjectRef::New(const EcmaVM *vm)��

����һ���µ� JavaScript ���󣬲����ضԸö���ı������á�

�ڴ���֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION_RETURN_UNDEFINED` �����Ƿ����Ǳ�ڵ��쳣��

��ȡ������Ĺ��������ȫ�ֻ�����

ʹ��ȫ�ֻ����е� `GetObjectFunction` ������ȡ�����캯����

���ù����� `NewJSObjectByConstructor` ���������µ� JavaScript ����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����             | ˵��                       |
| ---------------- | -------------------------- |
| Local<ObjectRef> | �����µ� JavaScript ���� |

**ʾ����**

```c++
Local<ObjectRef> myObject = ObjectRef::New(vm);
```

###   SetPrototype

bool ObjectRef::SetPrototype(const EcmaVM *vm, Local<ObjectRef> prototype)

���õ�ǰ `ObjectRef` �����ԭ��Ϊָ���� `prototype` ����

������֮ǰ��ͨ�� `LOG_IF_SPECIAL` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `ObjectRef` ����ת��Ϊ JavaScript �е� `JSObject` ����

ʹ�� `JSNApiHelper::ToJSHandle(prototype)` ������� `prototype` ����ת��Ϊ JavaScript �е� `JSObject` ����

���� `JSTaggedValue::SetPrototype` �������ö����ԭ�͡�

**������**

| ������    | ����             | ���� | ˵��                   |
| --------- | ---------------- | ---- | ---------------------- |
| vm        | const EcmaVM *   | ��   | ���������           |
| prototype | Local<ObjectRef> | ��   | ��ʾҪ���õ�ԭ�Ͷ��� |

**����ֵ��**

| ���� | ˵��                        |
| ---- | --------------------------- |
| bool | ���óɹ�����True��֮False�� |

**ʾ����**

```c++
Local<ObjectRef> object = ObjectRef::New(vm_);
Local<ObjectRef> prototype = object->GetPrototype(vm_);
object->SetPrototype(vm_, prototype);
```

 static Local<PromiseCapabilityRef> New(const EcmaVM *vm);



## FunctionRef

�ṩ������������װΪһ�������Լ��Է�װ�����ĵ��á�

### New

Local<FunctionRef> FunctionRef::New(EcmaVM *vm, FunctionCallback nativeFunc, NativePointerCallback deleter, void *data, bool callNapi, size_t nativeBindingsize)��

����һ���µĺ�������

**������**

|      ������       | ����             | ���� | ˵��                                                         |
| :---------------: | ---------------- | ---- | ------------------------------------------------------------ |
|        vm         | const EcmaVM *   | ��   | ָ�����������                                             |
|    nativeFunc     | FunctionCallback | ��   | һ���ص���������JS����������غ���ʱ������������ص�����     |
|      deleter      | NativePointerCallback          | ��   | һ��ɾ���������������ڲ�����Ҫ`FunctionRef`����ʱ�ͷ�����Դ�� |
|       data        | void *           | ��   | һ����ѡ��ָ�룬���Դ��ݸ��ص�������ɾ����������             |
|     callNapi      | bool             | ��   | һ������ֵ����ʾ�Ƿ��ڴ���`FunctionRef`����ʱ�������ûص����������Ϊ`true`�����ڴ�������ʱ�������ûص����������Ϊ`false`������Ҫ�ֶ����ûص������� |
| nativeBindingsize | size_t           | ��   | ��ʾnativeFunc�����Ĵ�С��0��ʾδ֪��С��                    |

**����ֵ��**

| ����               | ˵��                     |
| ------------------ | ------------------------ |
| Local<FunctionRef> | ����Ϊһ���µĺ������� |

**ʾ����**

```C++
Local<JSValueRef> FunCallback(JsiRuntimeCallInfo *info)
{
    EscapeLocalScope scope(info->GetVM());
    return scope.Escape(ArrayRef::New(info->GetVM(), info->GetArgsNumber()));
}
Local<FunctionRef> callback = FunctionRef::New(vm, FunCallback);
```

### NewClassFunction

Local<FunctionRef> FunctionRef::NewClassFunction(EcmaVM *vm, FunctionCallback nativeFunc, NativePointerCallback deleter, void *data, bool callNapi, size_t nativeBindingsize)��

����һ���µ��ຯ������

**������**

|      ������       | ����             | ���� | ˵��                                                         |
| :---------------: | ---------------- | ---- | ------------------------------------------------------------ |
|        vm         | const EcmaVM *   | ��   | ָ�����������                                             |
|    nativeFunc     | FunctionCallback | ��   | һ���ص���������JS����������غ���ʱ������������ص�����     |
|      deleter      | NativePointerCallback          | ��   | һ��ɾ���������������ڲ�����Ҫ`FunctionRef`����ʱ�ͷ�����Դ�� |
|       data        | void *           | ��   | һ����ѡ��ָ�룬���Դ��ݸ��ص�������ɾ����������             |
|     callNapi      | bool             | ��   | һ������ֵ����ʾ�Ƿ��ڴ���`FunctionRef`����ʱ�������ûص����������Ϊ`true`�����ڴ�������ʱ�������ûص����������Ϊ`false`������Ҫ�ֶ����ûص������� |
| nativeBindingsize | size_t           | ��   | ��ʾnativeFunc�����Ĵ�С��0��ʾδ֪��С��                    |

**����ֵ��**

| ����               | ˵��                     |
| ------------------ | ------------------------ |
| Local<FunctionRef> | ����Ϊһ���µĺ������� |

**ʾ����**

```C++
Local<JSValueRef> FunCallback(JsiRuntimeCallInfo *info)
{
    EscapeLocalScope scope(info->GetVM());
    return scope.Escape(ArrayRef::New(info->GetVM(), info->GetArgsNumber()));
}
NativePointerCallback deleter = nullptr;
void *cb = reinterpret_cast<void *>(BuiltinsFunction::FunctionPrototypeInvokeSelf);
bool callNative = true;
size_t nativeBindingSize = 15;
Local<FunctionRef> obj(FunctionRef::NewClassFunction(vm, FunCallback, deleter, cb, callNative, nativeBindingSize));
```

### Call

Local<JSValueRef> FunctionRef::Call(const EcmaVM *vm, Local<JSValueRef> thisObj, const Local<JSValueRef> argv[], int32_t length)��

����ָ���������FunctionRef�������õĻص�������

**������**

| ������  | ����                    | ���� | ˵��                            |
| :-----: | ----------------------- | ---- | ------------------------------- |
|   vm    | const EcmaVM *          | ��   | ָ�����������                |
| thisObj | FunctionCallback        | ��   | ָ�����ûص������Ķ���        |
| argv[]  | const Local<JSValueRef> | ��   | Local<JSValueRef>�������顣     |
| length  | int32_t                 | ��   | Local<JSValueRef>�������鳤�ȡ� |

**����ֵ��**

| ����              | ˵��                     |
| ----------------- | ------------------------ |
| Local<JSValueRef> | ���ڷ��غ���ִ�еĽ���� |

**ʾ����**

```C++
Local<JSValueRef> FunCallback(JsiRuntimeCallInfo *info)
{
    EscapeLocalScope scope(info->GetVM());
    return scope.Escape(ArrayRef::New(info->GetVM(), info->GetArgsNumber()));
}
Local<IntegerRef> intValue = IntegerRef::New(vm, 0);
std::vector<Local<JSValueRef>> argumentsInt;
argumentsInt.emplace_back(intValue);
Local<FunctionRef> callback = FunctionRef::New(vm, FunCallback);
callback->Call(vm, JSValueRef::Undefined(vm), argumentsInt.data(), argumentsInt.size());
```

### GetSourceCode

Local<StringRef> GetSourceCode(const EcmaVM *vm, int lineNumber)��

��ȡ���ô˺�����CPP�ļ��ڣ�ָ���кŵ�Դ���롣

**������**

|   ������   | ����           | ���� | ˵��             |
| :--------: | -------------- | ---- | ---------------- |
|     vm     | const EcmaVM * | ��   | ָ����������� |
| lineNumber | int            | ��   | ָ���кš�       |

**����ֵ��**

| ����             | ˵��                  |
| ---------------- | --------------------- |
| Local<StringRef> | ����ΪStringRef���� |

**ʾ����**

```C++
Local<JSValueRef> FunCallback(JsiRuntimeCallInfo *info)
{
    EscapeLocalScope scope(info->GetVM());
    return scope.Escape(ArrayRef::New(info->GetVM(), info->GetArgsNumber()));
}
Local<FunctionRef> callback = FunctionRef::New(vm, FunCallback);
Local<StringRef> src = callback->GetSourceCode(vm, 10);
```

### Constructor

Local<JSValueRef> FunctionRef::Constructor(const EcmaVM *vm, const Local<JSValueRef> argv[], int32_t length)��

����һ����������Ĺ��졣

**������**

| ������ | ����                    | ���� | ˵��                 |
| :----: | ----------------------- | ---- | -------------------- |
|   vm   | const EcmaVM *          | ��   | ָ�����������     |
|  argv  | const Local<JSValueRef> | ��   | �������顣           |
| length | int32_t                 | ��   | argv�����������С�� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ����һ��FunctionRef��������תΪLocal<JSValueRef>���ͣ���Ϊ��������ֵ�� |

**ʾ����**

```C++
Local<FunctionRef> cls = FunctionRef::NewClassFunction(vm, FunCallback, nullptr, nullptr);
Local<JSValueRef> argv[1];          
int num = 3;                      
argv[0] = NumberRef::New(vm, num);
Local<JSValueRef>functionObj = cls->Constructor(vm, argv, 1); 
```

### GetFunctionPrototype

Local<JSValueRef> FunctionRef::GetFunctionPrototype(const EcmaVM *vm)��

��ȡprototype���������������к�����ԭ�ͷ�������Щ�������Ա����еĺ���ʵ���������д��

**������**

| ������ | ����           | ���� | ˵��             |
| :----: | -------------- | ---- | ---------------- |
|   vm   | const EcmaVM * | ��   | ָ����������� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ��prototype����תΪLocal<JSValueRef>���ͣ�����Ϊ�˺����ķ���ֵ�� |

**ʾ����**

```C++
Local<JSValueRef> FunCallback(JsiRuntimeCallInfo *info)
{
    EscapeLocalScope scope(info->GetVM());
    return scope.Escape(ArrayRef::New(info->GetVM(), info->GetArgsNumber()));
}
Local<FunctionRef> res = FunctionRef::New(vm, FunCallback);
Local<JSValueRef> prototype = res->GetFunctionPrototype(vm);
```

## TypedArrayRef

һ�����ڴ�����������ݵ����ö�������������ͨ���飬��ֻ�ܴ洢�Ͳ����ض����͵����ݡ�

### ByteLength

uint32_t TypedArrayRef::ByteLength([[maybe_unused]] const EcmaVM *vm)��

�˺������ش�ArrayBufferRef�������ĳ��ȣ����ֽ�Ϊ��λ����

**������**

| ������ | ����           | ���� | ˵��         |
| :----: | -------------- | ---- | ------------ |
|   vm   | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����     | ˵��                              |
| -------- | --------------------------------- |
| uint32_t | ��uint32_t ���ͷ���buffer�ĳ��ȡ� |

**ʾ����**

```c++
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, 10);
Local<DataViewRef> dataView = DataViewRef::New(vm, arrayBuffer, 5, 7);
uint32_t len = dataView->ByteLength();
```

### ByteOffset

uint32_t TypedArrayRef::ByteOffset([[maybe_unused]] const EcmaVM *vm);

��ȡ��ǰ `TypedArrayRef` �����ڹ����ĵײ����黺�����е��ֽ�ƫ��λ�á�

�ڻ�ȡ�ֽ�ƫ��֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `TypedArrayRef` ����ת��Ϊ JavaScript �е� `JSTypedArray` ����

���� `GetByteOffset` ������ȡʵ�ʵ� TypedArray �� ArrayBuffer �е��ֽ�ƫ��λ�á�

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����    | ˵��                                                      |
| ------- | --------------------------------------------------------- |
| int32_t | ��ʾ TypedArray �����ڹ��� ArrayBuffer �е��ֽ�ƫ��λ�á� |

**ʾ����**

```c++
Local<ArrayBufferRef> arrayOffsetBuffer = ArrayBufferRef::New(vm, length);
Local<Int8ArrayRef> objOffset = Int8ArrayRef::New(vm, arrayOffsetBuffer, Offset, length);
uint32_t byteOffset = obj->ByteOffset(vm);
```

### ArrayLength

uint32_t TypedArrayRef::ArrayLength(const EcmaVM *vm);

��ȡ��ǰ `TypedArrayRef` ��������鳤�ȣ������д洢��Ԫ�ص�������

�ڻ�ȡ���鳤��֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `TypedArrayRef` ����ת��Ϊ JavaScript �е� `JSTypedArray` ����

���� `GetArrayLength` ������ȡʵ�ʵ����鳤�ȡ�

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����    | ˵��                                         |
| ------- | -------------------------------------------- |
| int32_t | ��ʾ TypedArray ��������鳤�ȣ�Ԫ���������� |

**ʾ����**

```c++
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, length);
Local<Int8ArrayRef> obj = Int8ArrayRef::New(vm, arrayBuffer, 5, 6);
uint32_t byteOffset = obj->ArrayLength(vm);
```

### GetArrayBuffer

Local<ArrayBufferRef> TypedArrayRef::GetArrayBuffer(const EcmaVM *vm);

��ȡ��ǰ `TypedArrayRef` ��������� ArrayBufferRef ����

�ڻ�ȡ ArrayBufferRef ֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION_RETURN_UNDEFINED` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `TypedArrayRef` ����ת��Ϊ JavaScript �е� `JSTypedArray` ����

���� `JSTypedArray::GetOffHeapBuffer` ������ȡʵ�ʵ� ArrayBuffer ����

ʹ�� `JSNApiHelper::ToLocal<ArrayBufferRef>(arrayBuffer)` 
�� JavaScript �е� ArrayBuffer ����ת��Ϊ���ص� `ArrayBufferRef` ����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����                  | ˵��                          |
| --------------------- | ----------------------------- |
| Local<ArrayBufferRef> | `ArrayBufferRef` ���͵Ķ��� |

**ʾ����**

```c++
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm, length);
Local<Int8ArrayRef> obj = Int8ArrayRef::New(vm, arrayBuffer, 5, 6);
Local<ArrayBufferRef> byteOffset = obj->GetArrayBuffer(vm);
```



## Exception

�ṩ��һЩ��̬���������ڸ��ݲ�ͬ�Ĵ������ʹ���һ����Ӧ��JS�쳣���󣬲�����һ��ָ��ö�������á�

### AggregateError

static Local<JSValueRef> AggregateError(const EcmaVM *vm, Local<StringRef> message)��

����Ҫ����������װ��һ��������ʱ��AggregateError�����ʾһ������

**������**

| ������  | ����             | ���� | ˵��         |
| :-----: | ---------------- | ---- | ------------ |
|   vm    | const EcmaVM *   | ��   | ��������� |
| message | Local<StringRef> | ��   | ������Ϣ��   |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ����������װΪAggregateError���󣬲�����תΪLocal<JSValueRef>���ͣ���Ϊ�����ķ���ֵ�� |

**ʾ����**

```C++
Local<JSValueRef> error = Exception::AggregateError(vm, StringRef::NewFromUtf8(vm, "test aggregate error"));
```

### EvalError

static Local<JSValueRef> EvalError(const EcmaVM *vm, Local<StringRef> message)��

���ڱ�ʾ��ִ�� `eval()` ����ʱ�����Ĵ��󡣵� `eval()` �����޷�������ִ�д�����ַ�������ʱ�����׳�һ�� `EvalError` �쳣��

**������**

| ������  | ����             | ���� | ˵��         |
| :-----: | ---------------- | ---- | ------------ |
|   vm    | const EcmaVM *   | ��   | ��������� |
| message | Local<StringRef> | ��   | ������Ϣ��   |

**����ֵ��**

| ����              | ˵�� |
| ----------------- | ---- |
| Local<JSValueRef> |      |

**ʾ����**

```C++
Local<JSValueRef> error = Exception::EvalError(vm, StringRef::NewFromUtf8(vm, "test eval error"));
```



### OOMError

static Local<JSValueRef> OOMError(const EcmaVM *vm, Local<StringRef> message);

���ڴ治���������׳�һ��������󡣣���������һ����̬��Ա��������˿���ֱ��ͨ���������ã������贴�����ʵ������

**������**

| ������  | ����             | ���� | ˵��                           |
| ------- | ---------------- | ---- | ------------------------------ |
| vm      | const EcmaVM *   | ��   | ���������                   |
| message | Local<StringRef> | ��   | ���ݸ�OOMError�����Ĵ�����Ϣ�� |

**����ֵ��**

| ����              | ˵��                                                       |
| ----------------- | ---------------------------------------------------------- |
| Local<JSValueRef> | ����һ�� JSValueRef ���󣬱�ʾJavaScript�е�ֵ�������Ϣ�� |

**ʾ����**

```c++
Local<JSValueRef> value = Exception::OOMError(vm_, message);
```

### TypeError

static Local<JSValueRef> TypeError(const EcmaVM *vm, Local<StringRef> message);

�ڸ������������EcmaVM���д���һ����ʾ���ʹ���� JavaScript ֵ���ã�JSValueRef����

**������**

| ������  | ����             | ���� | ˵��                            |
| ------- | ---------------- | ---- | ------------------------------- |
| vm      | const EcmaVM *   | ��   | ���������                    |
| message | Local<StringRef> | ��   | ���ݸ�TypeError�����Ĵ�����Ϣ�� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ����һ�� JSValueRef ���󣬱�ʾJavaScript�еĴ�����Ϣ���������ڽ�һ������򴫵ݸô������ |

**ʾ����**

```c++
Local<JSValueRef> value = Exception::TypeError(vm_, message);
```

### Error

static Local<JSValueRef> Error(const EcmaVM *vm, Local<StringRef> message);

���ڼ��error������һ���µĴ������

**������**

| ������  | ����             | ���� | ˵��           |
| ------- | ---------------- | ---- | -------------- |
| vm      | const EcmaVM *   | ��   | ���������     |
| message | Local<StringRef> | ��   | ����Ĵ�����Ϣ |

**����ֵ��**

| ����              | ˵��                 |
| :---------------- | :------------------- |
| Local<JSValueRef> | ���ش����Ĵ������ |

**ʾ����**

```c++
Local<StringRef> message = StringRef::NewFromUtf8(vm_, "test error");
Local<JSValueRef> result = Exception::Error(vm_, message);
```

### RangeError

static Local<JSValueRef> RangeError(const EcmaVM *vm, Local<StringRef> message);

���ڼ�ⷶΧ���󲢴���һ���µĴ������

**������**

| ������  | ����             | ���� | ˵��           |
| ------- | ---------------- | ---- | -------------- |
| vm      | const EcmaVM *   | ��   | ���������     |
| message | Local<StringRef> | ��   | ����Ĵ�����Ϣ |

**����ֵ��**

| ����              | ˵��                 |
| :---------------- | :------------------- |
| Local<JSValueRef> | ���ش����Ĵ������ |

**ʾ����**

```c++
Local<StringRef> message = StringRef::NewFromUtf8(vm_, "test error");
Local<JSValueRef> result = Exception::RangeError(vm_, message);
```

### ReferenceError

static Local<JSValueRef> ReferenceError(const EcmaVM *vm, Local<StringRef> message);

���ڼ�����ô��󲢴���һ���µĴ������

**������**

| ������  | ����             | ���� | ˵��           |
| ------- | ---------------- | ---- | -------------- |
| vm      | const EcmaVM *   | ��   | ���������     |
| message | Local<StringRef> | ��   | ����Ĵ�����Ϣ |

**����ֵ��**

| ����              | ˵��                 |
| :---------------- | :------------------- |
| Local<JSValueRef> | ���ش����Ĵ������ |

**ʾ����**

```c++
Local<StringRef> message = StringRef::NewFromUtf8(vm_, "test error");
Local<JSValueRef> result = Exception::ReferenceError(vm_, message);
```

### SyntaxError

static Local<JSValueRef> SyntaxError(const EcmaVM *vm, Local<StringRef> message);

����һ�� SyntaxError ���͵� JavaScript �쳣����

������ڹ�����쳣�������֮��

��ȡ `message` �� `EcmaString` �������ڱ�ʾ�쳣�������Ϣ��

�� `ObjectFactory` ��ȡ����ʵ��������һ���µ� JavaScript SyntaxError ����

���ؽ����Ϊ�������á�

**������**

| ������  | ����             | ���� | ˵��                 |
| ------- | ---------------- | ---- | -------------------- |
| vm      | const EcmaVM *   | ��   | ���������         |
| message | Local<StringRef> | ��   | ��ʾ�쳣�������Ϣ�� |

**����ֵ��**

| ����                     | ˵��                  |
| ------------------------ | --------------------- |
| static Local<JSValueRef> | ArrayRef ���͵Ķ��� |

**ʾ����**

```c++
Local<StringRef> errorMessage = StringRef::NewFromUtf8(vm, "Invalid syntax");
Local<JSValueRef> syntaxError = SyntaxError(vm, errorMessage);
```



## MapIteratorRef

���ڱ�ʾ�Ͳ���JS Map����ĵ��������õ��࣬���̳���ObjectRef�࣬���ṩ��һЩ����JS Map������������

### GetKind

Local<JSValueRef> GetKind(const EcmaVM *vm)��

��ȡMapIterator����Ԫ�ص����ͣ��ֱ�Ϊkey��value��keyAndValue��

**������**

| ������ | ����           | ���� | ˵��         |
| :----: | -------------- | ---- | ------------ |
|   vm   | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ��ȡ�����������Ͳ�����תΪLocal<JSValueRef>����Ϊ�����ķ���ֵ�� |

**ʾ����**

```c++
JSHandle<GlobalEnv> env = vm->GetGlobalEnv();
ObjectFactory *factory = vm->GetFactory();
JSHandle<JSTaggedValue> builtinsMapFunc = env->GetBuiltinsMapFunction();
JSHandle<JSMap> jsMap(factory->NewJSObjectByConstructor(JSHandle<JSFunction>(builtinsMapFunc), builtinsMapFunc));
JSHandle<JSTaggedValue> linkedHashMap(LinkedHashMap::Create(vm->GetJSThread()));
jsMap->SetLinkedMap(vm->GetJSThread(), linkedHashMap);
JSHandle<JSTaggedValue> mapValue(jsMap);
JSHandle<JSTaggedValue> mapIteratorVal = JSMapIterator::CreateMapIterator(vm->GetJSThread(), mapValue, IterationKind::KEY);
JSHandle<JSMapIterator> mapIterator = JSHandle<JSMapIterator>::Cast(mapIteratorVal);
mapIterator->SetIterationKind(IterationKind::VALUE);
mapIterator->SetIterationKind(IterationKind::KEY_AND_VALUE);
Local<MapIteratorRef> object = JSNApiHelper::ToLocal<MapIteratorRef>(mapIteratorVal);
Local<JSValueRef> type = object->GetKind(vm);
```

### GetIndex

int32_t GetIndex();

���ڻ�ȡMap������������ֵ����ʹ�ö�����֤����Ƿ�Ϊ0������ʼ����ֵ��

**������**

��

**����ֵ��**

| ����    | ˵��                          |
| :------ | :---------------------------- |
| int32_t | ���ػ�ȡ��Map������������ֵ�� |

**ʾ����**

```c++
ObjectFactory *factory = vm_->GetFactory();
JSHandle<JSTaggedValue> builtinsMapFunc = vm_->GetGlobalEnv()->GetBuiltinsMapFunction();
JSHandle<JSMap> jsMap(factory->NewJSObjectByConstructor(JSHandle<JSFunction>(builtinsMapFunc), builtinsMapFunc));
JSHandle<JSTaggedValue> linkedHashMap(LinkedHashMap::Create(thread_));
jsMap->SetLinkedMap(thread_, linkedHashMap);
JSHandle<JSTaggedValue> mapValue(jsMap);
JSHandle<JSTaggedValue> mapIteratorVal = JSMapIterator::CreateMapIterator(thread_, mapValue, IterationKind::KEY);
JSHandle<JSMapIterator> mapIterator = JSHandle<JSMapIterator>::Cast(mapIteratorVal);
mapIterator->SetNextIndex(index);
Local<MapIteratorRef> object = JSNApiHelper::ToLocal<MapIteratorRef>(mapIteratorVal);
int32_t result = object->GetIndex();
```



## PrimitiveRef

����Ϊԭʼ���󣬰���Undefined��Null��Boolean��Number��String��Symbol��BigInt ��ЩPrimitive���͵�ֵ�ǲ��ɱ�ģ���һ�������Ͳ����޸ġ�

### GetValue

Local<JSValueRef> GetValue(const EcmaVM *vm)��

��ȡԭʼ�����ֵ��

**������**

| ������ | ����           | ���� | ˵��         |
| :----: | -------------- | ---- | ------------ |
|   vm   | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ��ȡֵ����ת��Ϊ Local<JSValueRef>���Ͷ��󣬲���Ϊ�����ķ���ֵ�� |

**ʾ����**

```C++
Local<IntegerRef> intValue = IntegerRef::New(vm, 10);
Local<JSValueRef> jsValue = intValue->GetValue(vm);
```

## IntegerRef

���ڱ�ʾһ����������ͨ�����ڴ����������㣬IntegerRef���Դ洢����������������Դ洢16��������

### New

static Local<IntegerRef> New(const EcmaVM *vm, int input)��

����һ���µ�IntegerRef����

**������**

| ������ | ����           | ���� | ˵��                 |
| :----: | -------------- | ---- | -------------------- |
|   vm   | const EcmaVM * | ��   | ���������         |
| input  | int            | ��   | IntegerRef�����ֵ�� |

**����ֵ��**

| ����              | ˵��                         |
| ----------------- | ---------------------------- |
| Local<IntegerRef> | ����һ���µ�IntegerRef���� |

**ʾ����**

```C++
Local<IntegerRef> intValue = IntegerRef::New(vm, 0);
```

### NewFromUnsigned

static Local<IntegerRef> NewFromUnsigned(const EcmaVM *vm, unsigned int input)��

�����޷��ŵ�IntegerRef����

**������**

| ������ | ����           | ���� | ˵��                 |
| :----: | -------------- | ---- | -------------------- |
|   vm   | const EcmaVM * | ��   | ���������         |
| input  | int            | ��   | IntegerRef�����ֵ�� |

**����ֵ��**

| ����              | ˵��                         |
| ----------------- | ---------------------------- |
| Local<IntegerRef> | ����һ���µ�IntegerRef���� |

**ʾ����**

```C++
Local<IntegerRef> intValue = IntegerRef::NewFromUnsigned(vm, 0);
```

### Value

int IntegerRef::Value();

��ȡһ��IntegerRef���������ֵ��

**������**

��

**����ֵ��**

| ���� | ˵��                    |
| ---- | ----------------------- |
| int  | ����һ��int���͵���ֵ�� |

**ʾ����**

```c++
int num = 0;
Local<IntegerRef> intValue = IntegerRef::New(vm_, num);
int i = intValue->Value();
```





## PromiseRef

���ڴ����첽����������ʾһ����δ��ɵ�Ԥ����δ������ɵĲ��������ҷ���һ��ֵ��Promise������״̬��pending�������У���fulfilled���ѳɹ�����rejected����ʧ�ܣ���

### Catch

Local<PromiseRef> Catch(const EcmaVM *vm, Local<FunctionRef> handler)��

���ڲ����첽�����еĴ��󣬵�һ��Promise��rejectedʱ������ʹ��catch�������������

**������**

| ������  | ����               | ���� | ˵��                                                         |
| :-----: | ------------------ | ---- | ------------------------------------------------------------ |
|   vm    | const EcmaVM *     | ��   | ���������                                                 |
| handler | Local<FunctionRef> | ��   | ָ��FunctionRef���͵ľֲ���������ʾ�����쳣�Ļص�����������Promise�����з����쳣ʱ�����á� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<PromiseRef> | ����ڵ��ù����з����жϣ��򷵻�δ���壨undefined�������򣬽����ת��ΪPromiseRef���Ͳ����ء� |

**ʾ����**

```C++
Local<JSValueRef> FunCallback(JsiRuntimeCallInfo *info)
{
    EscapeLocalScope scope(info->GetVM());
    return scope.Escape(ArrayRef::New(info->GetVM(), info->GetArgsNumber()));
}
Local<PromiseCapabilityRef> capability = PromiseCapabilityRef::New(vm);
Local<PromiseRef> promise = capability->GetPromise(vm);
Local<FunctionRef> reject = FunctionRef::New(vm, FunCallback);
Local<PromiseRef> result = promise->Catch(vm, reject);
```

### Then

Local<PromiseRef> Then(const EcmaVM *vm, Local<FunctionRef> handler)��

��Promise����һ���ص�������Promise�����ö�ʱִ�еĺ�����

Local<PromiseRef> Then(const EcmaVM *vm, Local<FunctionRef> onFulfilled, Local<FunctionRef> onRejected)��

��Promise����һ���ص�������Promise�����ö�ִ��onFulfilled��Promise����ܾ�ִ��onRejected��

**������**

|   ������    | ����               | ���� | ˵��                        |
| :---------: | ------------------ | ---- | --------------------------- |
|     vm      | const EcmaVM *     | ��   | ���������                |
| onFulfilled | Local<FunctionRef> | ��   | Promise�����ö�ִ�еĺ����� |
| onRejected  | Local<FunctionRef> | ��   | Promise����ܾ�ִ�еĺ����� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<PromiseRef> | ������Ϊ Local<JSValueRef>���Ͷ��󣬲���Ϊ�����ķ���ֵ�������ж��첽�����Ƿ����óɹ��� |

**ʾ����**

```C++
Local<JSValueRef> FunCallback(JsiRuntimeCallInfo *info)
{
    EscapeLocalScope scope(info->GetVM());
    return scope.Escape(ArrayRef::New(info->GetVM(), info->GetArgsNumber()));
}
Local<PromiseCapabilityRef> capability = PromiseCapabilityRef::New(vm);
Local<PromiseRef> promise = capability->GetPromise(vm);
Local<FunctionRef> callback = FunctionRef::New(vm, FunCallback);
Local<PromiseRef> result = promise->Then(vm, callback, callback);
```

### Finally

Local<PromiseRef> Finally(const EcmaVM *vm, Local<FunctionRef> handler)��

����Promise�����ö����Ǿܾ�����ִ�еĺ�����

**������**

| ������  | ����               | ���� | ˵��             |
| :-----: | ------------------ | ---- | ---------------- |
|   vm    | const EcmaVM *     | ��   | ���������     |
| handler | Local<FunctionRef> | ��   | ��Ҫִ�еĺ����� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<PromiseRef> | ������Ϊ Local<JSValueRef>���Ͷ��󣬲���Ϊ�����ķ���ֵ�������ж��첽�����Ƿ����óɹ��� |

**ʾ����**

```C++
Local<JSValueRef> FunCallback(JsiRuntimeCallInfo *info)
{
    EscapeLocalScope scope(info->GetVM());
    return scope.Escape(ArrayRef::New(info->GetVM(), info->GetArgsNumber()));
}
Local<PromiseCapabilityRef> capability = PromiseCapabilityRef::New(vm);
Local<PromiseRef> promise = capability->GetPromise(vm);
Local<FunctionRef> callback = FunctionRef::New(vm, FunCallback);
Local<PromiseRef> result = promise->Finally(vm, callback);
```

## TryCatch

�쳣�����࣬������JS�в���ʹ���һЩ�쳣��

### GetAndClearException

Local<ObjectRef> GetAndClearException()��

��ȡ��������񵽵��쳣����

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ����             | ˵��                                                         |
| ---------------- | ------------------------------------------------------------ |
| Local<ObjectRef> | ��ȡ���񵽵��쳣��������ת��Ϊ Local<ObjectRef>���Ͷ��󣬲�����Ϊ�����ķ���ֵ�� |

**ʾ����**

```C++
Local<StringRef> message = StringRef::NewFromUtf8(vm, "ErrorTest");
JSNApi::ThrowException(vm, Exception::Error(vm, message););
TryCatch tryCatch(vm);
Local<ObjectRef> error = tryCatch.GetAndClearException();
```

### TryCatch

explicit TryCatch(const EcmaVM *ecmaVm) : ecmaVm_(ecmaVm) {};

���ڹ���TryCatch��Ķ���

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| ecmaVm | const EcmaVM * | ��   | ��������� |

**����ֵ��**

��

**ʾ����**

```c++
TryCatch(vm_);
```

### HasCaught

bool HasCaught() const;

���ڼ���Ƿ����쳣���󱻲���

**������**

��

**����ֵ��**

| ����    | ˵��                                              |
| :------ | :------------------------------------------------ |
| boolean | ������쳣���󱻲����򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
Local<StringRef> message = StringRef::NewFromUtf8(vm_, "ErrorTest");
Local<JSValueRef> error = Exception::Error(vm_, message);
JSNApi::ThrowException(vm_, error);
TryCatch tryCatch(vm_);
bool result = tryCatch.HasCaught();
```

### Rethrow

void Rethrow();

������쳣�����񲢴���ͨ�����rethrow_��ֵ��ȷ���Ƿ���Ҫ�����׳��쳣��

**������**

��

**����ֵ��**

�ޡ�

**ʾ����**

```c++
TryCatch tryCatch(vm_);
tryCatch.Rethrow();
```



## Uint32ArrayRef

���ڱ�ʾһ���޷���32λ������������ã��̳���TypedArrayRef�����ṩ�˴���һ���µ�Uint32Array����ķ�����

### New

static Local<Uint32ArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset, int32_t length);

���ڹ���һ��ʹ��ArrayBuffer����ָ��ƫ�����ͳ���ת����Uint32Array����

**������**

| ������     | ����                  | ���� | ˵��                                 |
| ---------- | --------------------- | ---- | ------------------------------------ |
| vm         | const EcmaVM *        | ��   | ���������                           |
| buffer     | Local<ArrayBufferRef> | ��   | Ҫת��ΪUint32Array��ArrayBuffer���� |
| byteOffset | int32_t               | ��   | ArrayBuffer�����ָ��λ��ƫ����      |
| length     | int32_t               | ��   | ArrayBuffer�����ָ������            |

**����ֵ��**

| ����                  | ˵��                        |
| :-------------------- | :-------------------------- |
| Local<Uint32ArrayRef> | ���ع����Uint32Array���� |

**ʾ����**

```c++
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm_, bufferLength);
Local<Uint32ArrayRef> result = Uint32ArrayRef::New(vm_,arrayBuffer,bufferOffset,offsetLength);
```



## Uint8ArrayRef

���ڱ�ʾһ���޷���8λ������������ã��̳���TypedArrayRef�����ṩ�˴���һ���µ�Uint8Array����ķ�����

### New

static Local<Uint8ArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset, int32_t length);

���ڹ���һ��ʹ��ArrayBuffer����ָ��ƫ�����ͳ���ת����Uint8Array����

**������**

| ������     | ����                  | ���� | ˵��                                |
| ---------- | --------------------- | ---- | ----------------------------------- |
| vm         | const EcmaVM *        | ��   | ���������                          |
| buffer     | Local<ArrayBufferRef> | ��   | Ҫת��ΪUint8Array��ArrayBuffer���� |
| byteOffset | int32_t               | ��   | ArrayBuffer�����ָ��λ��ƫ����     |
| length     | int32_t               | ��   | ArrayBuffer�����ָ������           |

**����ֵ��**

| ����                  | ˵��                        |
| :-------------------- | :-------------------------- |
| Local<Uint32ArrayRef> | ���ع����Uint32Array���� |

**ʾ����**

```c++
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm_, 30);
Local<Uint8ArrayRef> result = Uint8ArrayRef::New(vm_,arrayBuffer,10,10);
```



## MapRef

���ڱ�ʾ�Ͳ���JS Map��������ã����̳���ObjectRef�࣬���ṩ��һЩ����JSMap����ķ�����

### New

static Local<MapRef> New(const EcmaVM *vm);

���ڴ���һ��Map����

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����          | ˵��                |
| :------------ | :------------------ |
| Local<MapRef> | ���ش�����Map���� |

**ʾ����**

```c++
Local<MapRef> result = MapRef::New(vm_);
```

### GetSize

int32_t GetSize();

 ���� `MapRef` �����м�ֵ�Ե��������� `Map` ����Ĵ�С��

ͨ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `MapRef` ����ת��Ϊ JavaScript �е� `JSMap` ����

���� `JSMap::GetSize` ������ȡ `Map` ����Ĵ�С��

**������**

��

**����ֵ��**

| ����    | ˵��                   |
| ------- | ---------------------- |
| int32_t | ����`Map` ����Ĵ�С�� |

**ʾ����**

```c++
Local<MapRef> mapRef = MapRef::New(vm_);
int32_t size = mapRef->GetSize();
```

### GetTotalElements

int32_t GetTotalElements();

���� `MapRef` ����������Ԫ�ص�����������ʵ�ʴ��ڵ�Ԫ�غ���ɾ����Ԫ�ء�

ͨ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `MapRef` ����ת��Ϊ JavaScript �е� `JSMap` ����

���� `JSMap::GetSize` ������ȡ `Map` ����Ĵ�С��������ɾ��Ԫ�ص�������

**������**

��

**����ֵ��**

| ����    | ˵��                   |
| ------- | ---------------------- |
| int32_t | ����������Ԫ�ص������� |

**ʾ����**

```c++
Local<MapRef> mapRef = MapRef::New(vm_);
int32_t totalElements = mapRef->GetTotalElements();
```

### Get

Local<JSValueRef> Get(const EcmaVM *vm, Local<JSValueRef> key);

���� `MapRef` ������ָ������ֵ��

�ڻ�ȡ֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION_RETURN_UNDEFINED` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `MapRef` ����ת��Ϊ JavaScript �е� `JSMap` ����

���� `map->Get` ������ȡ���������Ӧ��ֵ��

**������**

| ������ | ����              | ���� | ˵��             |
| ------ | ----------------- | ---- | ---------------- |
| vm     | const EcmaVM *    | ��   | ���������     |
| key    | Local<JSValueRef> | ��   | ��ʾҪ��ȡ�ļ��� |

**����ֵ��**

| ����              | ˵��                         |
| ----------------- | ---------------------------- |
| Local<JSValueRef> | ���ر�ʾ�������͵��ַ���ֵ�� |

**ʾ����**

```c++
Local<MapRef> mapRef = MapRef::New(vm_);
Local<JSValueRef> myValue = MapRef->Get(myEcmaVM, myKey);
```

### GetKey

Local<JSValueRef> GetKey(const EcmaVM *vm, int entry);

��ȡ Map ������ָ���������ļ���

�ڻ�ȡ֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION_RETURN_UNDEFINED` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `MapRef` ����ת��Ϊ JavaScript �е� `JSMap` ����

 ���� `map->GetKey(entry)` ������ȡ Map ������ָ���������ļ����������ת��Ϊ `Local<JSValueRef>`��

**������**

| ������ | ����           | ���� | ˵��                                          |
| ------ | -------------- | ---- | --------------------------------------------- |
| vm     | const EcmaVM * | ��   | ���������                                  |
| entry  | int            | ��   | ��ʾ Map �����е���Ŀ���������ڻ�ȡ��Ӧ�ļ��� |

**����ֵ��**

| ����              | ˵��                              |
| ----------------- | --------------------------------- |
| Local<JSValueRef> | ������ Map ������ָ���������ļ��� |

**ʾ����**

```c++
Local<MapRef> mapRef = MapRef::New(vm_);
Local<JSValueRef> myKey = MapRef->GetKey(myEcmaVM, myEntry);
```

### GetValue

Local<JSValueRef> GetValue(const EcmaVM *vm, int entry);

��ȡ Map ������ָ����������ֵ��

�ڻ�ȡ֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION_RETURN_UNDEFINED` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `MapRef` ����ת��Ϊ JavaScript �е� `JSMap` ����

���� `map->GetValue(entry)` ������ȡ Map ������ָ����������ֵ���������ת��Ϊ `Local<JSValueRef>`��

**������**

| ������ | ����           | ���� | ˵��               |
| ------ | -------------- | ---- | ------------------ |
| vm     | const EcmaVM * | ��   | ���������       |
| entry  | int            | ��   | ���ڻ�ȡ��Ӧ��ֵ�� |

**����ֵ��**

| ����              | ˵��                              |
| ----------------- | --------------------------------- |
| Local<JSValueRef> | ������ Map ������ָ����������ֵ�� |

**ʾ����**

```c++
Local<MapRef> mapRef = MapRef::New(vm_);
Local<JSValueRef> myValue = MapRef->Get(myEcmaVM, myEntry);
```

### Set

void Set(const EcmaVM *vm, Local<JSValueRef> key, Local<JSValueRef> value);

 ��ǰ `MapRef` ���������ü�ֵ�ԡ�

������֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION_WITHOUT_RETURN` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `MapRef` ����ת��Ϊ JavaScript �е� `JSMap` ����

���� `JSMap::Set` ������ Map ���������ü�ֵ�ԡ�

**������**

| ������ | ����              | ���� | ˵��             |
| ------ | ----------------- | ---- | ---------------- |
| vm     | const EcmaVM *    | ��   | ���������     |
| key    | Local<JSValueRef> | ��   | ��ʾҪ���õļ��� |
| value  | Local<JSValueRef> | ��   | ��ʾҪ���õ�ֵ�� |

**����ֵ��**

��

**ʾ����**

```c++
myMap.Set(myEcmaVM, myKey, myValue);
```



## WeakMapRef

���ڱ�ʾ�Ͳ���JS WeakMap������࣬���̳���ObjectRef�࣬���ṩ��һЩ����JS WeakMap����ķ�����

### GetSize

int32_t GetSize();

���ڻ�ȡWeakMap�Ĵ�С��

**������**

��

**����ֵ��**

| ����    | ˵��                        |
| :------ | :-------------------------- |
| int32_t | ���ػ�ȡ����WeakMap�Ĵ�С�� |

**ʾ����**

```c++
Local<WeakMapRef> object = WeakMapRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = StringRef::NewFromUtf8(vm_, "TestValue");
object->Set(vm_, key, value);
int32_t result = object->GetSize();
```

### GetTotalElements

int32_t GetTotalElements();

���ڻ�ȡWeakMap��Ԫ�ظ�����

**������**

��

**����ֵ��**

| ����    | ˵��                            |
| :------ | :------------------------------ |
| int32_t | ���ػ�ȡ����WeakMap��Ԫ�ظ����� |

**ʾ����**

```c++
Local<WeakMapRef> object = WeakMapRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = StringRef::NewFromUtf8(vm_, "TestValue");
object->Set(vm_, key, value);
int32_t result = object->GetTotalElements();
```

### GetKey

Local<JSValueRef> GetKey(const EcmaVM *vm, int entry);

ͨ��ָ�������λ�û�ȡWeakMap��ָ���ļ���

**������**

| ������ | ����           | ���� | ˵��                 |
| ------ | -------------- | ---- | -------------------- |
| vm     | const EcmaVM * | ��   | ���������           |
| entry  | int            | ��   | Ҫ��ȡ�ļ������λ�� |

**����ֵ��**

| ����              | ˵��                   |
| :---------------- | :--------------------- |
| Local<JSValueRef> | ���ػ�ȡ����ָ���ļ��� |

**ʾ����**

```c++
Local<WeakMapRef> object = WeakMapRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = StringRef::NewFromUtf8(vm_, "TestValue");
object->Set(vm_, key, value);
Local<JSValueRef> result = object->GetKey(vm_, entry);
```

### GetValue

Local<JSValueRef> GetValue(const EcmaVM *vm, int entry);

ͨ��ָ�������λ�û�ȡWeakMap��ָ����Valueֵ��

**������**

| ������ | ����           | ���� | ˵��                      |
| ------ | -------------- | ---- | ------------------------- |
| vm     | const EcmaVM * | ��   | ���������                |
| entry  | int            | ��   | Ҫ��ȡ��Valueֵ�����λ�� |

**����ֵ��**

| ����              | ˵��                        |
| :---------------- | :-------------------------- |
| Local<JSValueRef> | ���ػ�ȡ����ָ����Valueֵ�� |

**ʾ����**

```c++
Local<WeakMapRef> object = WeakMapRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = StringRef::NewFromUtf8(vm_, "TestValue");
object->Set(vm_, key, value);
Local<JSValueRef> result = object->GetValue(vm_, entry);
```



## WeakSetRef

���ڱ�ʾ�Ͳ���JS WeakSet������࣬���̳���ObjectRef�࣬���ṩ��һЩ����JS WeakSet����ķ�����

### GetSize

int32_t GetSize();

���ڻ�ȡWeakSet�Ĵ�С��

**������**

��

**����ֵ��**

| ����    | ˵��                        |
| :------ | :-------------------------- |
| int32_t | ���ػ�ȡ����WeakSet�Ĵ�С�� |

**ʾ����**

```c++
Local<WeakSetRef> object = WeakSetRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = StringRef::NewFromUtf8(vm_, "TestValue");
object->Set(vm_, key, value);
int32_t result = object->GetSize();
```

### GetTotalElements

int32_t GetTotalElements();

���ڻ�ȡWeakSet��Ԫ�ظ�����

**������**

��

**����ֵ��**

| ����    | ˵��                            |
| :------ | :------------------------------ |
| int32_t | ���ػ�ȡ����WeakSet��Ԫ�ظ����� |

**ʾ����**

```c++
Local<WeakSetRef> object = WeakSetRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = StringRef::NewFromUtf8(vm_, "TestValue");
object->Set(vm_, key, value);
int32_t result = object->GetTotalElements();
```

### GetValue

Local<JSValueRef> GetValue(const EcmaVM *vm, int entry);

ͨ��ָ�������λ�û�ȡWeakSet��ָ����Valueֵ��

**������**

| ������ | ����           | ���� | ˵��                      |
| ------ | -------------- | ---- | ------------------------- |
| vm     | const EcmaVM * | ��   | ���������                |
| entry  | int            | ��   | Ҫ��ȡ��Valueֵ�����λ�� |

**����ֵ��**

| ����              | ˵��                        |
| :---------------- | :-------------------------- |
| Local<JSValueRef> | ���ػ�ȡ����ָ����Valueֵ�� |

**ʾ����**

```c++
Local<WeakSetRef> object = WeakSetRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = StringRef::NewFromUtf8(vm_, "TestValue");
object->Set(vm_, key, value);
Local<JSValueRef> result = object->GetValue(vm_, 0);
```



## JSExecutionScope

���ڱ�ʾJSִ����������࣬����JS�������ض�ִ�л����е������ġ�

### JSExecutionScope

explicit JSExecutionScope(const EcmaVM *vm);

���ڹ���һ�� JSExecutionScope���͵Ķ���

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

��

**ʾ����**

```c++
JSExecutionScope(vm_);
```



## NativePointerRef

�̳���JSValueRef���ṩ�˹���ԭ��ָ��ķ�����

### New

static Local<NativePointerRef> New(const EcmaVM *vm, void *nativePointer, size_t nativeBindingsize = 0);

static Local<NativePointerRef> New(const EcmaVM *vm, void *nativePointer, NativePointerCallback callBack, void *data, size_t nativeBindingsize = 0);

���ڹ���һ��ԭ��ָ�����͵Ķ���

**������**

| ������            | ����                  | ���� | ˵��                             |
| ----------------- | --------------------- | ---- | -------------------------------- |
| vm                | const EcmaVM *        | ��   | ���������                       |
| nativePointer     | void *                | ��   | ԭ��ָ��                         |
| nativeBindingsize | size_t                | ��   | ԭ���󶨵Ĵ�С                   |
| callBack          | NativePointerCallback | ��   | ԭ��ָ��Ļص�����               |
| data              | void *                | ��   | ��������ָ�룬��Ϊ�ص������Ĳ��� |

**����ֵ��**

| ����                    | ˵��                                       |
| :---------------------- | :----------------------------------------- |
| Local<NativePointerRef> | ���ع���ɹ���NativePointerRef���͵Ķ��� |

**ʾ����**

```c++
void  *vps = static_cast<void *>(new std::string("test"));
Local<NativePointerRef> result = NativePointerRef::New(vm_, vps, 0);
NativePointerCallback callBack = nullptr;
void *vps = static_cast<void *>(new std::string("test"));
void *vpsdata = static_cast<void *>(new std::string("test"));
Local<NativePointerRef> result =  NativePointerRef::New(vm_, vps, callBack, vpsdata, 0);
```

### Value

void *Value();

��ȡһ���ⲿָ�룬������ָ��һ������ָ�벢���ء�

**������**

��

**����ֵ��**

| ����   | ˵��                         |
| :----- | :--------------------------- |
| void * | ����ֵΪ��ȡԭ�������ָ�롣 |

**ʾ����**

```c++
void *vps = static_cast<void *>(new std::string("test"));
void *vps1 = static_cast<void *>(new std::string("test"));
Local<NativePointerRef> res_vps = NativePointerRef::New(vm_, vps, NativeAreaAllocator::FreeBufferFunc, vps1, 0);
void *result = res_vps->Value();
```



## BigInt64ArrayRef

���ڱ�ʾһ��64λ�������飬��ͨ�����ڴ�����������㣬��Ϊ��ͨ��Number������JavaScript��ֻ�ܾ�ȷ��ʾ��53λ����

### New

static Local<BigInt64ArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset, int32_t length)��

����һ��BigInt64ArrayRef����

**������**

| ������     | ����                  | ���� | ˵��                                  |
| ---------- | --------------------- | ---- | ------------------------------------- |
| vm         | const EcmaVM *        | ��   | ���������                          |
| buffer     | Local<ArrayBufferRef> | ��   | һ�� ArrayBuffer �������ڴ洢���ݡ� |
| byteOffset | int32_t               | ��   | ��ʾ�ӻ��������ĸ��ֽڿ�ʼ��ȡ���ݡ�  |
| length     | int32_t               | ��   | ��ʾҪ��ȡ��Ԫ��������                |

**����ֵ��**

| ����                    | ˵��                           |
| ----------------------- | ------------------------------ |
| Local<BigInt64ArrayRef> | һ���µ�BigInt64ArrayRef���� |

**ʾ����**

```C++
Local<ArrayBufferRef> buffer = ArrayBufferRef::New(vm, 5);
Local<ObjectRef> object = BigInt64ArrayRef::New(vm, buffer, 0, 5);
```

## BigIntRef

���ڱ�ʾ���������������ṩ��һ�ַ�����������Number�����ܱ�ʾ��������Χ�����֡�

### New

static Local<BigIntRef> New(const EcmaVM *vm, int64_t input)��

����һ���µ�BigIntRef����

**������**

| ������ | ����           | ���� | ˵��                          |
| ------ | -------------- | ---- | ----------------------------- |
| vm     | const EcmaVM * | ��   | ���������                  |
| input  | int64_t        | ��   | ��ҪתΪBigIntRef�������ֵ�� |

**����ֵ��**

| ����             | ˵��                    |
| ---------------- | ----------------------- |
| Local<BigIntRef> | һ���µ�BigIntRef���� |

**ʾ����**

```C++
int64_t maxInt64 = std::numeric_limits<int64_t>::max();
Local<BigIntRef> valie = BigIntRef::New(vm, maxInt64);
```

### BigIntToInt64

void BigIntRef::BigIntToInt64(const EcmaVM *vm, int64_t *cValue, bool *lossless)��

��BigInt����ת��Ϊ64λ�з����������Ƿ��ܹ���ȷ��������ת����

**������**

| ������   | ����           | ���� | ˵��                                    |
| -------- | -------------- | ---- | --------------------------------------- |
| vm       | const EcmaVM * | ��   | ���������                            |
| cValue   | int64_t *      | ��   | ���ڴ洢ת��ΪInt64��ֵ�ı�����         |
| lossless | bool *         | ��   | �����жϳ������Ƿ��ܹ�ת��ΪInt64���͡� |

**����ֵ��**

| ���� | ˵��       |
| ---- | ---------- |
| void | �޷���ֵ�� |

**ʾ����**

```C++
uint64_t maxUint64 = std::numeric_limits<uint64_t>::max();
Local<BigIntRef> maxBigintUint64 = BigIntRef::New(vm, maxUint64);
int64_t toNum;
bool lossless = true;
maxBigintUint64->BigIntToInt64(vm, &toNum, &lossless);
```

### BigIntToUint64

void BigIntRef::BigIntToUint64(const EcmaVM *vm, uint64_t *cValue, bool *lossless)��

��BigInt����ת��Ϊ64λ�޷�������������ת���Ƿ������ȷ����

**������**

| ������   | ����           | ���� | ˵��                                    |
| -------- | -------------- | ---- | --------------------------------------- |
| vm       | const EcmaVM * | ��   | ���������                            |
| cValue   | uint64_t *     | ��   | ���ڴ洢ת��Ϊuint64_t��ֵ�ı�����      |
| lossless | bool *         | ��   | �����жϳ������Ƿ��ܹ�ת��ΪInt64���͡� |

**����ֵ��**

| ���� | ˵��       |
| ---- | ---------- |
| void | �޷���ֵ�� |

**ʾ����**

```C++
uint64_t maxUint64 = std::numeric_limits<uint64_t>::max();
Local<BigIntRef> maxBigintUint64 = BigIntRef::New(vm, maxUint64);
int64_t toNum;
bool lossless = true;
maxBigintUint64->BigIntToInt64(vm, &toNum, &lossless);
```

### CreateBigWords

Local<JSValueRef> BigIntRef::CreateBigWords(const EcmaVM *vm, bool sign, uint32_t size, const uint64_t* words)��

��һ��uint64_t�����װΪһ��BigIntRef����

**������**

| ������ | ����            | ���� | ˵��                                 |
| ------ | --------------- | ---- | ------------------------------------ |
| vm     | const EcmaVM *  | ��   | ���������                         |
| sign   | bool            | ��   | ȷ�����ɵ� `BigInt` ���������Ǹ����� |
| size   | uint32_t        | ��   | uint32_t �����С��                  |
| words  | const uint64_t* | ��   | uint32_t ���顣                      |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ��uint32_t ת��ΪBigIntRef���󣬲�����ת��ΪLocal<JSValueRef>���ͣ���Ϊ�����ķ���ֵ�� |

**ʾ����**

```C++
bool sign = false;
uint32_t size = 3;
const uint64_t words[3] = {
    std::numeric_limits<uint64_t>::min() - 1,
    std::numeric_limits<uint64_t>::min(),
    std::numeric_limits<uint64_t>::max(),
};
Local<JSValueRef> bigWords = BigIntRef::CreateBigWords(vm, sign, size, words);
```

### GetWordsArraySize

uint32_t GetWordsArraySize()��

��ȡBigIntRef�����װuint64_t����Ĵ�С��

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ����     | ˵��                                      |
| -------- | ----------------------------------------- |
| uint32_t | ����BigIntRef�����װuint64_t����Ĵ�С�� |

**ʾ����**

```C++
bool sign = false;
uint32_t size = 3;
const uint64_t words[3] = {
    std::numeric_limits<uint64_t>::min() - 1,
    std::numeric_limits<uint64_t>::min(),
    std::numeric_limits<uint64_t>::max(),
};
Local<JSValueRef> bigWords = BigIntRef::CreateBigWords(vm, sign, size, words);
Local<BigIntRef> bigWordsRef(bigWords);
uint32_t cnt = bigWordsRef->GetWordsArraySize();
```

### GetWordsArray

void BigIntRef::GetWordsArray(bool *signBit, size_t wordCount, uint64_t *words); 

��ȡһ��BigIntRef�����ֵ���������ʾΪһ��64λ�޷����������顣ͬʱ������������һ������ֵsignBit����ʾ��BigIntRef����ķ��ţ�������������

**������**

| ������    | ����       | ���� | ˵��                                                         |
| --------- | ---------- | ---- | ------------------------------------------------------------ |
| signBit   | bool *     | ��   | ָ�򲼶�ֵ��ָ�룬���ڴ洢BigIntRef����ķ��ţ������������� |
| wordCount | size_t     | ��   | �޷�����������ʾҪ��ȡ��64λ�޷�����������ĳ��ȡ�           |
| words     | uint64_t * | ��   | ָ��64λ�޷���������ָ�룬���ڴ洢BigIntRef�����ֵ��        |

**����ֵ��**

��

**ʾ����**

```c++
bool sign = false;
uint32_t size = 3;
const uint64_t words[3] = {
    std::numeric_limits<uint64_t>::min() - 1,
    std::numeric_limits<uint64_t>::min(),
    std::numeric_limits<uint64_t>::max(),
};
Local<JSValueRef> bigWords = BigIntRef::CreateBigWords(vm_, sign, size, words);
Local<BigIntRef> bigWordsRef(bigWords);
bool resultSignBit = true;
uint64_t *resultWords = new uint64_t[3]();
bigWordsRef->GetWordsArray(&resultSignBit, size, resultWords);
```



## StringRef

�̳���PrimitiveRef�����ڱ�ʾ�ַ����������ݵ����ã��ṩ��һЩ���ַ����Ĳ���������

### NewFromUtf8

Local<StringRef> StringRef::NewFromUtf8(const EcmaVM *vm, const char *utf8, int length)��

����utf8���͵�StringRef����

**������**

| ������ | ����           | ���� | ˵��             |
| ------ | -------------- | ---- | ---------------- |
| vm     | const EcmaVM * | ��   | ���������     |
| utf8   | char *         | ��   | char�����ַ����� |
| int    | length         | ��   | �ַ������ȡ�     |

**����ֵ��**

| ����             | ˵��                    |
| ---------------- | ----------------------- |
| Local<StringRef> | һ���µ�StringRef���� |

**ʾ����**

```C++
std::string testUtf8 = "Hello world";
Local<StringRef> description = StringRef::NewFromUtf8(vm, testUtf8.c_str());
```

### NewFromUtf16

Local<StringRef> StringRef::NewFromUtf16(const EcmaVM *vm, const char16_t *utf16, int length)��

����utf16���͵�StringRef����

**������**

| ������ | ����           | ���� | ˵��                  |
| ------ | -------------- | ---- | --------------------- |
| vm     | const EcmaVM * | ��   | ���������          |
| utf16  | char16_t *     | ��   | char16_t �����ַ����� |
| int    | length         | ��   | �ַ������ȡ�          |

**����ֵ��**

| ����             | ˵��                    |
| ---------------- | ----------------------- |
| Local<StringRef> | һ���µ�StringRef���� |

**ʾ����**

```C++
char16_t data = 0Xdf06;
Local<StringRef> obj = StringRef::NewFromUtf16(vm, &data);
```

### Utf8Length

int32_t StringRef::Utf8Length(const EcmaVM *vm)��

��utf8���Ͷ�ȡStringRef��ֵ���ȡ�

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����    | ˵��                   |
| ------- | ---------------------- |
| int32_t | utf8�����ַ����ĳ��ȡ� |

**ʾ����**

```C++
std::string testUtf8 = "Hello world";
Local<StringRef> stringObj = StringRef::NewFromUtf8(vm, testUtf8.c_str());
int32_t lenght = stringObj->Utf8Length(vm);
```

### WriteUtf8

int StringRef::WriteUtf8(char *buffer, int length, bool isWriteBuffer)��

��StringRef��ֵд��char���黺������

**������**

| ������        | ����   | ���� | ˵��                                  |
| ------------- | ------ | ---- | ------------------------------------- |
| buffer        | char * | ��   | ��Ҫд��Ļ�������                    |
| length        | int    | ��   | ��Ҫд�뻺�����ĳ��ȡ�                |
| isWriteBuffer | bool   | ��   | �Ƿ���Ҫ��StringRef��ֵд�뵽�������� |

**����ֵ��**

| ���� | ˵��                              |
| ---- | --------------------------------- |
| int  | ��StringRef��ֵתΪUtf8��ĳ��ȡ� |

**ʾ����**

```C++
Local<StringRef> local = StringRef::NewFromUtf8(vm, "abcdefbb");
char cs[16] = {0};
int length = local->WriteUtf8(cs, 6);
```

### WriteUtf16

int StringRef::WriteUtf16(char16_t *buffer, int length)��

��StringRef��ֵд��char���黺������

**������**

| ������ | ����   | ���� | ˵��                   |
| ------ | ------ | ---- | ---------------------- |
| buffer | char * | ��   | ��Ҫд��Ļ�������     |
| length | int    | ��   | ��Ҫд�뻺�����ĳ��ȡ� |

**����ֵ��**

| ���� | ˵��                              |
| ---- | --------------------------------- |
| int  | ��StringRef��ֵתΪUtf8��ĳ��ȡ� |

**ʾ����**

```c++
Local<StringRef> local = StringRef::NewFromUtf16(vm, u"���ã���Ϊ��");
char cs[16] = {0};
int length = local->WriteUtf16(cs, 3);
```

### Length

uint32_t StringRef::Length()��

��ȡStringRef��ֵ�ĳ��ȡ�

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ���� | ˵��                  |
| ---- | --------------------- |
| int  | StringRef��ֵ�ĳ��ȡ� |

**ʾ����**

```c++
Local<StringRef> local = StringRef::NewFromUtf8(vm, "abcdefbb");
int len = local->Length()
```

### ToString

std::string StringRef::ToString()��

��StringRef��ֵת��Ϊstd::string��

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ����        | ˵��                                  |
| ----------- | ------------------------------------- |
| std::string | ��StringRef��valueתΪC++string���͡� |

**ʾ����**

```c++
Local<StringRef> stringObj = StringRef::NewFromUtf8(vm, "abc");
std::string str = stringObj->ToString();
```

### GetNapiWrapperString

Local<StringRef> StringRef::GetNapiWrapperString(const EcmaVM *vm);

��ȡһ����ʾNAPI��װ�ַ�����StringRef����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����             | ˵��                                                         |
| ---------------- | ------------------------------------------------------------ |
| Local<StringRef> | ����ʾNAPI��װ�ַ����Ķ���ת��ΪStringRef���󣬲�������Ϊ�����ķ���ֵ�� |

**ʾ����**

```c++
Local<StringRef> local = StringRef::GetNapiWrapperString(vm_);
```

### WriteLatin1

int WriteLatin1(char *buffer, int length);

���ַ���д�뵽ָ���Ļ������С�

**������**

| ������ | ����   | ���� | ˵��             |
| ------ | ------ | ---- | ---------------- |
| buffer | char * | ��   | ָ���Ļ�����     |
| length | int    | ��   | Ҫд������ݳ��� |

**����ֵ��**

| ���� | ˵��                   |
| :--- | :--------------------- |
| int  | ����ֵΪд����ֽ����� |

**ʾ����**

```c++
Local<StringRef> object = StringRef::NewFromUtf8(vm_, "abcdefbb");
char cs[16] = {0};
int result = object->WriteLatin1(cs, length);
```



## NumberRef

�̳���PrimitiveRef�����ڱ�ʾNumber�������ݵ����ã����ṩ�˹���NumberRef����ķ������Լ���Number�������ݵ�һЩ������

### New

static Local<NumberRef> New(const EcmaVM *vm, double input);

static Local<NumberRef> New(const EcmaVM *vm, int32_t input);

static Local<NumberRef> New(const EcmaVM *vm, uint32_t input);

static Local<NumberRef> New(const EcmaVM *vm, int64_t input);

�ýӿ�Ϊ���غ��������ڹ��첻ͬ�������͵�NumberRef����

**������**

| ������ | ����           | ���� | ˵��             |
| ------ | -------------- | ---- | ---------------- |
| vm     | const EcmaVM * | ��   | ���������       |
| input  | double         | ��   | double��������   |
| input  | int32_t        | ��   | int32_t��������  |
| input  | uint32_t       | ��   | uint32_t�������� |
| input  | int64_t        | ��   | int64_t��������  |

**����ֵ��**

| ����             | ˵��                                      |
| :--------------- | :---------------------------------------- |
| Local<NumberRef> | ���ع���ɹ��Ĳ�ͬ�������͵�NumberRef���� |

**ʾ����**

```c++
double doubleValue = 3.14;
Local<NumberRef> result = NumberRef::New(vm_, doubleValue);
int32_t int32Value = 10;
Local<NumberRef> result = NumberRef::New(vm_, int32Value);
uint32_t uint32Value = 10;
Local<NumberRef> result = NumberRef::New(vm_, uint32Value);
int64_t int64Value = 10;
Local<NumberRef> result = NumberRef::New(vm_, int64Value);
```



## ArrayRef

�̳���ObjectRef�����ڹ���һ��������󣬲��ṩ��һЩ��������ķ�����

### Length

uint32_t Length(const EcmaVM *vm);

��ȡ�������ĳ��ȡ�

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����     | ˵��                   |
| :------- | :--------------------- |
| uint32_t | ���ػ�ȡ�������鳤�ȡ� |

**ʾ����**

```c++
Local<ArrayRef> arrayObj = ArrayRef::New(vm_, 3);
uint32_t result = arrayObj->Length(vm_);
```

### SetValueAt

static bool SetValueAt(const EcmaVM *vm, Local<JSValueRef> obj, uint32_t index, Local<JSValueRef> value);

�����ڸ������������������ָ������λ�õ�ֵ��

**������**

| ������ | ����              | ���� | ˵��                 |
| ------ | ----------------- | ---- | -------------------- |
| vm     | const EcmaVM *    | ��   | ���������           |
| obj    | Local<JSValueRef> | ��   | ָ�����������       |
| index  | uint32_t          | ��   | Ҫ���õ�ָ������λ�� |
| value  | Local<JSValueRef> | ��   | Ҫ���õ�ֵ           |

**����ֵ��**

| ����    | ˵��                                                  |
| :------ | :---------------------------------------------------- |
| boolean | ָ������λ�õ�ֵ���óɹ����򷵻�true�����򷵻�false�� |

**ʾ����**

```c++
Local<ArrayRef> arrayObj = ArrayRef::New(vm_, 1);
Local<IntegerRef> intValue = IntegerRef::New(vm_, 0);
bool result = ArrayRef::SetValueAt(vm_, arrayObj, index, intValue);
```

### GetValueAt

static Local<JSValueRef> GetValueAt(const EcmaVM *vm, Local<JSValueRef> obj, uint32_t index);

�����ڸ�������������л�ȡָ������λ�õ�ֵ��

**������**

| ������ | ����              | ���� | ˵��                 |
| ------ | ----------------- | ---- | -------------------- |
| vm     | const EcmaVM *    | ��   | ���������           |
| obj    | Local<JSValueRef> | ��   | ָ�����������       |
| index  | uint32_t          | ��   | Ҫ��ȡ��ָ������λ�� |

**����ֵ��**

| ����              | ˵��                           |
| :---------------- | :----------------------------- |
| Local<JSValueRef> | ���ػ�ȡ����ָ������λ�õ�ֵ�� |

**ʾ����**

```c++
Local<ArrayRef> arrayObj = ArrayRef::New(vm_, 1);
Local<IntegerRef> intValue = IntegerRef::New(vm_, 0);
ArrayRef::SetValueAt(vm_, arrayObj, index, intValue);
Local<JSValueRef> result = ArrayRef::GetValueAt(vm_, arrayObj, 0);
```

### New

Local<ArrayRef> ArrayRef::New(const EcmaVM *vm, uint32_t length = 0);

����һ������ָ�����ȵ� JavaScript �������

�ڴ����������֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION_RETURN_UNDEFINED` �����Ƿ����Ǳ�ڵ��쳣��

ʹ�� `JSArray::ArrayCreate` �������� JavaScript �е��������ָ������ĳ���Ϊ `length`��

ʹ�� `JSNApiHelper::ToLocal<ArrayRef>(array)` �� JavaScript �е��������ת��Ϊ���ص� `ArrayRef` ����

**������**

| ������ | ����           | ���� | ˵��                     |
| ------ | -------------- | ---- | ------------------------ |
| vm     | const EcmaVM * | ��   | ���������             |
| length | uint32_t       | ��   | ��ʾҪ����������ĳ��ȡ� |

**����ֵ��**

| ����            | ˵��                   |
| --------------- | ---------------------- |
| Local<ArrayRef> | ArrayRef  ���͵Ķ��� |

**ʾ����**

```c++
 Local<ArrayRef> myArray = ArrayRef::New(VM, length);
```



## JsiRuntimeCallInfo

��Ҫ���ڴ���JS����ʱ���õ���Ϣ�����ṩ��һЩ������

### GetData

void* GetData();

��ȡJsiRuntimeCallInfo�����ݡ�

**������**

��

**����ֵ��**

| ����   | ˵��                                         |
| :----- | :------------------------------------------- |
| void * | ����ֵΪ��JsiRuntimeCallInfo�л�ȡ�������ݡ� |

**ʾ����**

```c++
JsiRuntimeCallInfo object;
void *result = object.GetData();
```

### GetVM

EcmaVM *GetVM() const;

��ȡ�뵱ǰ `JsiRuntimeCallInfo` ����������� `EcmaVM` ָ�롣

**������**

��

**����ֵ��**

| ����    | ˵��             |
| ------- | ---------------- |
| EcmaVM* | ����EcmaVMָ�롣 |

**ʾ����**

```c++
JsiRuntimeCallInfo callInfo;
EcmaVM *vm = callInfo.GetVM();
```



## PromiseRejectInfo

`PromiseRejectInfo` �����ڴ洢�й� Promise ���ܾ��¼�����Ϣ���������ܾ��� Promise ���󡢾ܾ���ԭ���¼����ͺ����¼���ص����ݡ��ṩ����Ӧ�ķ��ʷ������ڻ�ȡ��Щ��Ϣ��

### GetPromise

Local<JSValueRef> GetPromise() const��

��ȡһ��Promise����

**������**

| ������ | ���� | ���� | ˵�� |
| ------ | ---- | ---- | ---- |
| �޲�   |      |      |      |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ��ȡPromise���󣬲�����ת��ΪLocal<JSValueRef>���ͣ���Ϊ�����ķ���ֵ�� |

**ʾ����**

```C++
Local<JSValueRef> promise(PromiseCapabilityRef::New(vm)->GetPromise(vm));
Local<StringRef> toStringReason = StringRef::NewFromUtf8(vm, "3.14");
Local<JSValueRef> reason(toStringReason);
void *data = static_cast<void *>(new std::string("promisereject"));
PromiseRejectInfo promisereject(promise, reason, PromiseRejectInfo::PROMISE_REJECTION_EVENT::REJECT, data);
Local<JSValueRef> obj = promisereject.GetPromise();
```

### GetData

void* PromiseRejectInfo::GetData() const

���ش洢�����˽�г�Ա����data_�еĶ������ݡ�

**������**

��

**����ֵ��**

| ����   | ˵��                                                         |
| ------ | ------------------------------------------------------------ |
| void * | ����ֵ��һ��ͨ��ָ�루void *������ָ��洢�����˽�г�Ա����data_�еĶ������ݡ� |

**ʾ����**

```c++
Local<StringRef> toStringPromise = StringRef::NewFromUtf8(vm_, "-3.14");
Local<JSValueRef> promise(toStringPromise);
Local<StringRef> toStringReason = StringRef::NewFromUtf8(vm_, "3.14");
Local<JSValueRef> reason(toStringReason);
void *data = static_cast<void *>(new std::string("promisereject"));
PromiseRejectInfo promisereject(promise, reason, PromiseRejectInfo::PROMISE_REJECTION_EVENT::REJECT, data);
void *ptr = promisereject.GetData();
```

### PromiseRejectInfo

PromiseRejectInfo(Local<JSValueRef> promise, Local<JSValueRef> reason,PromiseRejectInfo::PROMISE_REJECTION_EVENT operation, void* data);

����һ�� `PromiseRejectInfo` �������ڱ����� Promise ���ܾ��¼���ص���Ϣ��

������� `promise`��`reason`��`operation` �� `data` �����洢�ڶ���ĳ�Ա�����С�

**������**

| ������    | ����                                       | ���� | ˵��                                  |
| --------- | ------------------------------------------ | ---- | ------------------------------------- |
| promise   | Local<JSValueRef>                          | ��   | ��ʾ���ܾ��� Promise ����           |
| reason    | Local<JSValueRef>                          | ��   | ��ʾ Promise ���ܾ���ԭ��           |
| operation | PromiseRejectInfo::PROMISE_REJECTION_EVENT | ��   | ��ʾ Promise ���ܾ����¼����͡�       |
| data      | void*                                      | ��   | ��ʾ�� Promise ���ܾ��¼���ص����ݡ� |

**����ֵ��**

| ����                                       | ˵��                                       |
| ------------------------------------------ | ------------------------------------------ |
| PromiseRejectInfo::PROMISE_REJECTION_EVENT | `PromiseRejectInfo` ����Ĳ�������ö��ֵ�� |

**ʾ����**

```c++
PromiseRejectInfo::PROMISE_REJECTION_EVENT operationType = myRejectInfo.GetOperation();
```

### GetReason

  Local<JSValueRef> GetReason() const;

��ȡ�洢�� `PromiseRejectInfo` �����еı��ܾ��� Promise ��ԭ��

**������**

��

**����ֵ��**

| ����              | ˵��                                       |
| ----------------- | ------------------------------------------ |
| Local<JSValueRef> | `PromiseRejectInfo` ����Ĳ�������ö��ֵ�� |

**ʾ����**

```c++
Local<JSValueRef> rejectionReason = myRejectInfo.GetReason();
```

### GetOperation

PromiseRejectInfo::PROMISE_REJECTION_EVENT GetOperation() const;

��ȡ��ǰ `PromiseRejectInfo` ����Ĳ������͡�

����ֵ��һ��ö��ֵ����ʾ Promise �ܾ��¼��ľ���������͡�

**������**

��

**����ֵ��**

| ����                                       | ˵��                                       |
| ------------------------------------------ | ------------------------------------------ |
| PromiseRejectInfo::PROMISE_REJECTION_EVENT | `PromiseRejectInfo` ����Ĳ�������ö��ֵ�� |

**ʾ����**

```c++
PromiseRejectInfo::PROMISE_REJECTION_EVENT operationType = myRejectInfo.GetOperation();
```



## PromiseCapabilityRef

`PromiseCapabilityRef` ���� `ObjectRef` ������࣬ר�����ڴ��� Promise ����Ĺ��ܡ����ṩ�˴����µ� PromiseCapability ���󡢽�� Promise���ܾ� Promise �Լ���ȡ Promise �ķ�����

### Resolve

bool Resolve(const EcmaVM *vm, Local<JSValueRef> value)��

�����ö�Promise����

**������**

| ������ | ����              | ���� | ˵��                         |
| ------ | ----------------- | ---- | ---------------------------- |
| vm     | const EcmaVM *    | ��   | ���������                 |
| value  | Local<JSValueRef> | ��   | ִ�лص�����������Ҫ�Ĳ����� |

**����ֵ��**

| ���� | ˵��                                |
| ---- | ----------------------------------- |
| bool | Promise����Ļص������Ƿ�ɹ����á� |

**ʾ����**

```c++
Local<JSValueRef> FunCallback(JsiRuntimeCallInfo *info)
{
    EscapeLocalScope scope(info->GetVM());
    return scope.Escape(ArrayRef::New(info->GetVM(), info->GetArgsNumber()));
}
Local<PromiseCapabilityRef> capability = PromiseCapabilityRef::New(vm);
Local<PromiseRef> promise = capability->GetPromise(vm);
promise->Then(vm, FunctionRef::New(vm, FunCallback), FunctionRef::New(vm, FunCallback));
bool b = capability->Resolve(vm, NumberRef::New(vm, 300.3));
```

### Reject

bool Reject(const EcmaVM *vm, Local<JSValueRef> reason)��

���ھܾ�Promise����

**������**

| ������ | ����              | ���� | ˵��                         |
| ------ | ----------------- | ---- | ---------------------------- |
| vm     | const EcmaVM *    | ��   | ���������                 |
| value  | Local<JSValueRef> | ��   | ִ�лص�����������Ҫ�Ĳ����� |

**����ֵ��**

| ���� | ˵��                                |
| ---- | ----------------------------------- |
| bool | Promise����Ļص������Ƿ�ɹ����á� |

**ʾ����**

```C++
Local<JSValueRef> FunCallback(JsiRuntimeCallInfo *info)
{
    EscapeLocalScope scope(info->GetVM());
    return scope.Escape(ArrayRef::New(info->GetVM(), info->GetArgsNumber()));
}
Local<PromiseCapabilityRef> capability = PromiseCapabilityRef::New(vm);
Local<PromiseRef> promise = capability->GetPromise(vm);
promise->Then(vm, FunctionRef::New(vm, FunCallback), FunctionRef::New(vm, FunCallback));
bool b = capability->Reject(vm, NumberRef::New(vm, 300.3));
```

### GetPromise

Local<PromiseRef> GetPromise(const EcmaVM *vm);

��ȡ�뵱ǰ���������Promise����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<PromiseRef> | ��ȡPromise���󣬲�����ת��Ϊ�������ã�PromiseRef���ͣ����ء� |

**ʾ����**

```c++
Local<PromiseCapabilityRef> capability = PromiseCapabilityRef::New(vm_);
Local<PromiseRef> promise = capability->GetPromise(vm_);
```

### new

static Local<PromiseCapabilityRef> New(const EcmaVM *vm);

����һ���µ� `PromiseCapability` ���󣬲����ضԸö���ı������á�

�ڴ���֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION_RETURN_UNDEFINED` �����Ƿ����Ǳ�ڵ��쳣��

��ȡ��������̡߳����������ȫ�ֻ�����

ʹ��ȫ�ֻ����е� `GetPromiseFunction` ������ȡ `Promise` ���캯����

���� `JSPromise::NewPromiseCapability` ���������µ� `PromiseCapability` ����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����                        | ˵��                                       |
| --------------------------- | ------------------------------------------ |
| Local<PromiseCapabilityRef> | `PromiseRejectInfo` ����Ĳ�������ö��ֵ�� |

**ʾ����**

```c++
Local<PromiseCapabilityRef> myPromiseCapability = PromiseCapabilityRef::New(vm);
```



## SymbolRef : public PrimitiveRef 

�����̳���PrimitiveRef�࣬��Ҫ���ڶ���һ����Ϊ`SymbolRef`�Ĺ���API�ࡣ

### New

static Local<SymbolRef> New(const EcmaVM *vm, Local<StringRef> description = Local<StringRef>());

���ڴ���һ���µ�`SymbolRef`����

**������**

| ������      | ����             | ���� | ˵��                                                         |
| ----------- | ---------------- | ---- | ------------------------------------------------------------ |
| vm          | const EcmaVM *   | ��   | ���������                                                 |
| description | Local<StringRef> | ��   | ��ѡ��`Local<StringRef>`���͵����������û���ṩ��������Ĭ��Ϊ�ա� |

**����ֵ��**

| ����             | ˵��                                      |
| ---------------- | ----------------------------------------- |
| Local<SymbolRef> | ����һ���µ�SymbolRef���Ͷ��󲢽��䷵�ء� |

**ʾ����**

```c++
SymbolRef::New(vm_);
```

### GetDescription

Local<StringRef> GetDescription(const EcmaVM *vm);

��ȡ�������õ�������

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����             | ˵��                                   |
| ---------------- | -------------------------------------- |
| Local<StringRef> | �����������ľ��ת��Ϊ�������ò����ء� |

**ʾ����**

```c++
SymbolRef::GetDescription(vm_);
```



## FunctionCallScope

��������ڹ��������õ�������

### FunctionCallScope

FunctionCallScope::FunctionCallScope(EcmaVM *vm) : vm_(vm);

FunctionCallScope��Ĵ��ι��캯����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

��

**ʾ����**

```c++
FunctionCallScope::FunctionCallScope FC(vm_); 
```



## LocalScope

����������ǹ���ֲ������򣬰�������ǰһ���ͺ�һ���ֲ�������ǰһ���ֲ�������Ľ���λ�á�ǰһ���ֲ�������ľ���洢�����Լ��߳���Ϣ��

### LocalScope

explicit LocalScope(const EcmaVM *vm);

��ʼ�� LocalScope ���󣬲������������ EcmaVMʵ����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

��

**ʾ����**

```c++
LocalScope::LocalScope Scope(vm_);
```

### LocalScope

inline LocalScope(const EcmaVM *vm, JSTaggedType value);

���캯�������ڳ�ʼ�� `LocalScope` ��Ķ���

��ȡ��ǰ�̵߳� `JSThread` ����

ͨ�� `reinterpret_cast` ���̶߳���ת��Ϊ `JSThread*` ���͡�

ʹ�� `EcmaHandleScope::NewHandle` �����ھ���������д���һ���µı��ؾ����

**������**

| ������ | ����           | ���� | ˵��                         |
| ------ | -------------- | ---- | ---------------------------- |
| vm     | const EcmaVM * | ��   | ���������                 |
| value  | JSTaggedType   | ��   | ��ʾҪ�洢�ڱ��ؾ���е�ֵ�� |

**����ֵ��**

��

**ʾ����**

```c++
 LocalScope myLocalScope(myEcmaVM, someJSTaggedValue);
```



## SetRef : public ObjectRef

�����Ǽ̳���ObjectRef������ࡣ���������Ǳ�ʾһ��JavaScript��Set���󣬲��ṩ�˻�ȡ���С����Ԫ�������Լ���ȡָ������λ�õ�Ԫ��ֵ�ķ�����

### GetSize

int32_t SetRef::GetSize();

��ȡһ��SetRef����Ĵ�С��

**������**

��

**����ֵ��**

| ����    | ˵��                                              |
| ------- | ------------------------------------------------- |
| int32_t | ����һ��int32_t���͵���ֵ����ʾSetRef����Ĵ�С�� |

**ʾ����**

```c++
Local<SetRef> object = SetRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = StringRef::NewFromUtf8(vm_, "TestValue");
object->Set(vm_, key, value);
int32_t size = object->GetSize();
```

### GetTotalElements

int32_t SetRef::GetTotalElements();

��ȡһ��SetRef����������Ԫ�ص�������������ɾ����Ԫ�ء�

**������**

��

**����ֵ��**

| ����    | ˵��                                                         |
| ------- | ------------------------------------------------------------ |
| int32_t | ����һ��int32_t���͵���ֵ����ʾSetRef����������Ԫ�ص�������������ɾ����Ԫ�ء� |

**ʾ����**

```c++
Local<SetRef> object = SetRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = StringRef::NewFromUtf8(vm_, "TestValue");
object->Set(vm_, key, value);
int32_t element = object->GetTotalElements();
```

### GetValue

Local<JSValueRef> SetRef::GetValue(const EcmaVM *vm, int entry);

��ȡSetRef������ָ������λ�õ�Ԫ��ֵ��

**������**

| ������ | ����           | ���� | ˵��                                                 |
| ------ | -------------- | ---- | ---------------------------------------------------- |
| vm     | const EcmaVM * | ��   | ���������                                         |
| entry  | int            | ��   | һ����������ʾҪ��ȡ��Ԫ����SetRef�����е�����λ�á� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ��ȡָ������λ�õ�Ԫ��ֵ�󣬽���ת��ΪLocal<JSValueRef>���͵Ķ��󣬲���Ϊ�����ķ���ֵ�� |

**ʾ����**

```c++
Local<SetRef> object = SetRef::New(vm_);
Local<JSValueRef> key = StringRef::NewFromUtf8(vm_, "TestKey");
Local<JSValueRef> value = StringRef::NewFromUtf8(vm_, "TestValue");
object->Set(vm_, key, value);
Local<JSValueRef> value = object->GetValue(vm_, 0);
```



## BigUint64ArrayRef : public TypedArrayRef

����̳���TypedArrayRef�࣬���ڱ�ʾһ��64λ�޷����������顣

### New

static Local<BigUint64ArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset, int32_t length);

��̬��Ա���������ڴ���һ���µ�BigUint64ArrayRef����

**������**

| ������     | ����                  | ���� | ˵��                                                   |
| ---------- | --------------------- | ---- | ------------------------------------------------------ |
| vm         | const EcmaVM *        | ��   | ���������                                           |
| buffer     | Local<ArrayBufferRef> | ��   | һ��ArrayBufferRef���͵ľֲ�������Ҫ�洢���ݵĻ������� |
| byteOffset | int32_t               | ��   | һ��32λ��������ʾ�ڻ������е��ֽ�ƫ������             |
| length     | int32_t               | ��   | һ��32λ��������ʾ����ĳ��ȡ�                         |

**����ֵ��**

| ����                     | ˵��                                          |
| ------------------------ | --------------------------------------------- |
| Local<BigUint64ArrayRef> | ����һ�������õ�BigUint64ArrayRef���͵Ķ��� |

**ʾ����**

```c++
Local<ArrayBufferRef> array = ArrayBufferRef::New(/*....�������....*/);
int32_t byteOffset = 3;
int32_t length = 40;
Local<BigUint64ArrayRef> bu64array = BigUint64ArrayRef::New(vm_, array, byteOffset, length);
```



## Float32ArrayRef : public TypedArrayRef

����̳���TypedArrayRef�࣬�����Ǵ���һ�����������͵��������ã��������ڲ����ͷ��ʸ��������͵����ݡ�

### New

static Local<Float32ArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset, int32_t length);

��̬��Ա���������ڴ���һ���µ�Float32ArrayRef����

**������**

| ������     | ����                  | ���� | ˵��                                                   |
| ---------- | --------------------- | ---- | ------------------------------------------------------ |
| vm         | const EcmaVM *        | ��   | ���������                                           |
| buffer     | Local<ArrayBufferRef> | ��   | һ��ArrayBufferRef���͵ľֲ�������Ҫ�洢���ݵĻ������� |
| byteOffset | int32_t               | ��   | һ��32λ��������ʾ�ڻ������е��ֽ�ƫ������             |
| length     | int32_t               | ��   | һ��32λ��������ʾ����ĳ��ȡ�                         |

**����ֵ��**

| ����                   | ˵��                                                         |
| ---------------------- | ------------------------------------------------------------ |
| Local<Float32ArrayRef> | ����һ��Float32ArrayRef���͵Ķ��󣬱�ʾ�����ĸ����������������á� |

**ʾ����**

```c++
Local<ArrayBufferRef> array = ArrayBufferRef::New(/*....�������....*/);
int32_t byteOffset = 3;
int32_t length = 40;
Local<Float32ArrayRef> fl32array = Float32ArrayRef::New(vm_, array, byteOffset, length);
```



## Float64ArrayRef : public TypedArrayRef

����̳���TypedArrayRef�࣬��ʾһ��64λ���������͵��������á�

### New

static Local<Float64ArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset, int32_t length);

��̬��Ա���������ڴ���һ���µ�Float64ArrayRef����

**������**

| ������     | ����                  | ���� | ˵��                                                   |
| ---------- | --------------------- | ---- | ------------------------------------------------------ |
| vm         | const EcmaVM *        | ��   | ���������                                           |
| buffer     | Local<ArrayBufferRef> | ��   | һ��ArrayBufferRef���͵ľֲ�������Ҫ�洢���ݵĻ������� |
| byteOffset | int32_t               | ��   | һ��32λ��������ʾ�ڻ������е��ֽ�ƫ������             |
| length     | int32_t               | ��   | һ��32λ��������ʾ����ĳ��ȡ�                         |

**����ֵ��**

| ����                   | ˵��                                                         |
| ---------------------- | ------------------------------------------------------------ |
| Local<Float64ArrayRef> | ����һ��Float64ArrayRef���͵Ķ��󣬱�ʾ�����ĸ����������������á� |

**ʾ����**

```c++
Local<ArrayBufferRef> array = ArrayBufferRef::New(/*....�������....*/);
int32_t byteOffset = 3;
int32_t length = 40;
Local<Float64ArrayRef> fl32array = Float64ArrayRef::New(vm_, array, byteOffset, length);
```



## Int8ArrayRef : public TypedArrayRef

����̳��� TypedArrayRef �ࡣ�������Ǵ���һ����ʾ 8 λ�������͵��������á�

### New

static Local<Int8ArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset, int32_t length);

����һ���µ�Int8ArrayRef����

**������**

| ������     | ����                  | ���� | ˵��                                                   |
| ---------- | --------------------- | ---- | ------------------------------------------------------ |
| vm         | const EcmaVM *        | ��   | ���������                                           |
| buffer     | Local<ArrayBufferRef> | ��   | һ��ArrayBufferRef���͵ľֲ�������Ҫ�洢���ݵĻ������� |
| byteOffset | int32_t               | ��   | һ��32λ��������ʾ�ڻ������е��ֽ�ƫ������             |
| length     | int32_t               | ��   | һ��32λ��������ʾ����ĳ��ȡ�                         |

**����ֵ��**

| ����                | ˵��                                         |
| ------------------- | -------------------------------------------- |
| Local<Int8ArrayRef> | ����һ���´�����Int8ArrayRef����ı������á� |

**ʾ����**

```c++
const int32_t length = 15;
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm_, length);
Local<Int8ArrayRef> obj = Int8ArrayRef::New(vm_, arrayBuffer, 5, 6);
```



## Int16ArrayRef : public TypedArrayRef

����̳��� TypedArrayRef �ࡣ�������Ǵ���һ����ʾ 16λ�������͵��������á�

### New

static Local<Int16ArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset, int32_t length);

����һ���µ�Int16ArrayRef����

**������**

| ������     | ����                  | ���� | ˵��                                                   |
| ---------- | --------------------- | ---- | ------------------------------------------------------ |
| vm         | const EcmaVM *        | ��   | ���������                                           |
| buffer     | Local<ArrayBufferRef> | ��   | һ��ArrayBufferRef���͵ľֲ�������Ҫ�洢���ݵĻ������� |
| byteOffset | int32_t               | ��   | һ��32λ��������ʾ�ڻ������е��ֽ�ƫ������             |
| length     | int32_t               | ��   | һ��32λ��������ʾ����ĳ��ȡ�                         |

**����ֵ��**

| ����                 | ˵��                                          |
| -------------------- | --------------------------------------------- |
| Local<Int16ArrayRef> | ����һ���´�����Int16ArrayRef����ı������á� |

**ʾ����**

```c++
const int32_t length = 15;
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm_, length);
Local<Int16ArrayRef> obj = Int16ArrayRef::New(vm_, arrayBuffer, 5, 6);
```



## Int32ArrayRef : public TypedArrayRef

����̳��� TypedArrayRef �ࡣ�������Ǵ���һ����ʾ 32λ�������͵��������á�

### New

static Local<Int32ArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset, int32_t length);

����һ���µ�Int32ArrayRef����

**������**

| ������     | ����                  | ���� | ˵��                                                   |
| ---------- | --------------------- | ---- | ------------------------------------------------------ |
| vm         | const EcmaVM *        | ��   | ���������                                           |
| buffer     | Local<ArrayBufferRef> | ��   | һ��ArrayBufferRef���͵ľֲ�������Ҫ�洢���ݵĻ������� |
| byteOffset | int32_t               | ��   | һ��32λ��������ʾ�ڻ������е��ֽ�ƫ������             |
| length     | int32_t               | ��   | һ��32λ��������ʾ����ĳ��ȡ�                         |

**����ֵ��**

| ����                 | ˵��                                          |
| -------------------- | --------------------------------------------- |
| Local<Int32ArrayRef> | ����һ���´�����Int32ArrayRef����ı������á� |

**ʾ����**

```c++
const int32_t length = 15;
Local<ArrayBufferRef> arrayBuffer = ArrayBufferRef::New(vm_, length);
Local<Int32ArrayRef> obj = Int32ArrayRef::New(vm_, arrayBuffer, 5, 6);
```



## ProxyRef : public ObjectRef

����̳���ObjectRef�࣬��Ҫ���ڴ���JavaScript�������

### GetTarget

Local<JSValueRef> GetTarget(const EcmaVM *vm);

��ȡJavaScript��������Ŀ�����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����              | ˵��                                                         |
| ----------------- | ------------------------------------------------------------ |
| Local<JSValueRef> | ����һ��JSValueRef���͵Ķ�������ʾJavaScript��������Ŀ����� |

**ʾ����**

```c++
JSHandle<GlobalEnv> env = vm_->GetGlobalEnv();
ObjectFactory *factory = thread_->GetEcmaVM()->GetFactory();
JSHandle<JSTaggedValue> hclass(thread_, env->GetObjectFunction().GetObject<JSFunction>());
JSHandle<JSTaggedValue> targetHandle(factory->NewJSObjectByConstructor(JSHandle<JSFunction>::Cast(hclass), hclass));
JSHandle<JSTaggedValue> key(factory->NewFromASCII("x"));
JSHandle<JSTaggedValue> value(thread_, JSTaggedValue(1));
JSObject::SetProperty(thread_, targetHandle, key, value);
JSHandle<JSTaggedValue> handlerHandle(
    factory->NewJSObjectByConstructor(JSHandle<JSFunction>::Cast(hclass), hclass));
JSHandle<JSProxy> proxyHandle = JSProxy::ProxyCreate(thread_, targetHandle, handlerHandle);
JSHandle<JSTaggedValue> proxytagvalue = JSHandle<JSTaggedValue>::Cast(proxyHandle);
Local<ProxyRef> object = JSNApiHelper::ToLocal<ProxyRef>(proxytagvalue);
Local<JSValueRef> value = object->GetTarget(vm_);
```

### IsRevoked

bool ProxyRef::IsRevoked();

���JavaScript��������Ƿ��ѱ�������

**������**

��

**����ֵ��**

| ���� | ˵��                                                         |
| ---- | ------------------------------------------------------------ |
| bool | ����ֵ��true �� false�������������ѱ�����������true�����򣬷���false�� |

**ʾ����**

```c++
JSHandle<GlobalEnv> env = vm_->GetGlobalEnv();
ObjectFactory *factory = thread_->GetEcmaVM()->GetFactory();
JSHandle<JSTaggedValue> hclass(thread_, env->GetObjectFunction().GetObject<JSFunction>());
JSHandle<JSTaggedValue> targetHandle(factory->NewJSObjectByConstructor(JSHandle<JSFunction>::Cast(hclass), hclass));
JSHandle<JSTaggedValue> key(factory->NewFromASCII("x"));
JSHandle<JSTaggedValue> value(thread_, JSTaggedValue(1));
JSObject::SetProperty(thread_, targetHandle, key, value);
JSHandle<JSTaggedValue> handlerHandle(
    factory->NewJSObjectByConstructor(JSHandle<JSFunction>::Cast(hclass), hclass));
JSHandle<JSProxy> proxyHandle = JSProxy::ProxyCreate(thread_, targetHandle, handlerHandle);
JSHandle<JSTaggedValue> proxytagvalue = JSHandle<JSTaggedValue>::Cast(proxyHandle);
Local<ProxyRef> object = JSNApiHelper::ToLocal<ProxyRef>(proxytagvalue);
bool b = object->IsRevoked();
```

### GetHandler

Local<JSValueRef> GetHandler(const EcmaVM *vm);

��ȡ����Ĵ������

**������**

| ������ | ����           | ���� | ˵��       |
| ------ | -------------- | ---- | ---------- |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����              | ˵��                 |
| :---------------- | :------------------- |
| Local<JSValueRef> | ���ض���Ĵ������ |

**ʾ����**

```c++
JSHandle<JSTaggedValue> targetHandle;
JSHandle<JSTaggedValue> handlerHandle;
JSHandle<JSProxy> proxyHandle = JSProxy::ProxyCreate(thread_, targetHandle, handlerHandle);
JSHandle<JSTaggedValue> proxytagvalue = JSHandle<JSTaggedValue>::Cast(proxyHandle);
Local<JSValueRef> result = JSNApiHelper::ToLocal<ProxyRef>(proxytagvalue)->GetHandler(vm_);
```

## EscapeLocalScope��LocalScope

`EscapeLocalScope` ����һ���̳��� `LocalScope` �ľ������࣬���ڹ���ֲ��������������ڡ�
���ṩ��һ������ `Escape`�������ھֲ���Χ����ǰ���ؾֲ������� `Local` ����

### EscapeLocalScope

explicit EscapeLocalScope(const EcmaVM *vm);

����EscapeLocalScope����

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

��

**ʾ����**

```c++
EscapeLocalScope scope(vm);
```

## SetIteratorRef��ObjectRef

SetIteratorRef ����һ���̳��� ObjectRef �ľ������࣬���ڲ��� JavaScript Set ����ĵ�������

### GetIndex

int32_t GetIndex();

��ȡ��ǰ `SetIteratorRef` ��������� Set �������ĵ�ǰ������

ͨ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `SetIteratorRef` ����ת��Ϊ JavaScript �е� `JSSetIterator` ����

���� `JSSetIterator` ����� `GetNextIndex` ������ȡ��ǰ��������������

**������**

��

**����ֵ��**

| ����    | ˵��               |
| ------- | ------------------ |
| int32_t | ��ǰ�������������� |

**ʾ����**

```c++
int32_t currentIndex = mySetIterator.GetIndex();
```

### GetKind

Local<JSValueRef> GetKind(const EcmaVM *vm);

��ȡ��ǰ `SetIteratorRef` ��������� Set �������ĵ������͡�

�ڻ�ȡ֮ǰ��ͨ�� `CHECK_HAS_PENDING_EXCEPTION_RETURN_UNDEFINED` �����Ƿ����Ǳ�ڵ��쳣��

 ͨ�� `JSNApiHelper::ToJSHandle(this)` ����ǰ�� `SetIteratorRef` ����ת��Ϊ JavaScript �е� `JSSetIterator` ����

���� `JSSetIterator` ����� `GetIterationKind` ������ȡ�������͡�

���ݵ�������ѡ����Ӧ���ַ�����Ȼ��ʹ�� `StringRef::NewFromUtf8` ��������һ�� `Local<JSValueRef>` ���󣬱�ʾ�ַ���ֵ��

**������**

| ������ | ����           | ���� | ˵��         |
| ------ | -------------- | ---- | ------------ |
| vm     | const EcmaVM * | ��   | ��������� |

**����ֵ��**

| ����              | ˵��                         |
| ----------------- | ---------------------------- |
| Local<JSValueRef> | ���ر�ʾ�������͵��ַ���ֵ�� |

**ʾ����**

```c++
Local<JSValueRef> iterationType = mySetIterator.GetKind(myEcmaVM);
```

## Uint8ClampedArrayRef��TypedArrayRef

Uint8ClampedArrayRef ����һ���̳��� TypedArrayRef �ľ������࣬���ڲ��� JavaScript �е� Uint8ClampedArray ����

### New
static Local<Uint8ClampedArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset,int32_t length);
����Uint8ClampedArrayRef����

����ú궨�崴�� TypedArray ��ͨ��ģ�塣

**������**

| ������     | ����                  | ���� | ˵��                                                        |
| ---------- | --------------------- | ---- | ----------------------------------------------------------- |
| vm         | const EcmaVM *        | ��   | ���������                                                |
| buffer     | Local<ArrayBufferRef> | ��   | ��ʾ�� `Uint8ClampedArray` ������ ArrayBuffer ����        |
| byteOffset | int32_t               | ��   | ��ʾ�� ArrayBuffer ���ĸ��ֽڿ�ʼ���� `Uint8ClampedArray`�� |
| length     | int32_t               | ��   | ��ʾҪ������ `Uint8ClampedArray` �ĳ��ȡ�                   |

**����ֵ��**

| ����                        | ˵��                                     |
| --------------------------- | ---------------------------------------- |
| Local<Uint8ClampedArrayRef> | ����`Local<Uint8ClampedArrayRef>` ���� |



**ʾ����**

```c++
Local<Uint8ClampedArrayRef> myUint8ClampedArray = Uint8ClampedArrayRef::New(myEcmaVM, myArrayBuffer, Offset, length);
```

## Uint16ArrayRef��TypedArrayRef

Uint16ArrayRef ����һ���̳��� TypedArrayRef �ľ������࣬���ڲ��� JavaScript �е� Uint16Array ����

### New

static Local<Uint16ArrayRef> New(const EcmaVM *vm, Local<ArrayBufferRef> buffer, int32_t byteOffset,int32_t length);

ͨ���ú����� JavaScript �����д���һ���µ� `Uint16Array` ����

**������**

| ������     | ����                  | ���� | ˵��                                                  |
| ---------- | --------------------- | ---- | ----------------------------------------------------- |
| vm         | const EcmaVM *        | ��   | ���������                                          |
| buffer     | Local<ArrayBufferRef> | ��   | ��ʾ�� `Uint16Array` ������ ArrayBuffer ����        |
| byteOffset | int32_t               | ��   | ��ʾ�� ArrayBuffer ���ĸ��ֽڿ�ʼ���� `Uint16Array`�� |
| length     | int32_t               | ��   | ��ʾҪ������ `Uint16Array` �ĳ��ȡ�                   |

**����ֵ��**

| ����                  | ˵��                               |
| --------------------- | ---------------------------------- |
| Local<Uint16ArrayRef> | ����`Local<Uint16ArrayRef>` ���� |

**ʾ����**

```c++
Local<Uint16ArrayRef> myUint16Array = Uint16ArrayRef::New(myEcmaVM, myArrayBuffer, Offset, length);
```
