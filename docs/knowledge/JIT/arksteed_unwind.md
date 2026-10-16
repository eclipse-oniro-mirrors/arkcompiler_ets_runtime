# 回栈与符号化知识文档
> 对应源码 `ecmascript/frames.cpp` 中 `FrameIterator`负责对不同类型的栈帧进行适配
> 对应源码 `ecmascript/dfx/stackinfo/js_stackinfo.cpp` 中 `GetBytecodeOffset`, `GetTypeOffsetAndPrevOffsetFromFrameType` 对不同类型帧进行适配

## 相关名词解释
| 名词 | 解释 |
| --- | --- |
| faultloggerd | 鸿蒙设备段运行的驻留程序，监听应用的异常，可以将应用异常时的瞬间状态抓取出来 |
| hiperf | 鸿蒙应用性能调试工具，可对栈帧进行采样，分析运行函数的耗时，其回栈能力依赖faultloggerd |

## 适用场景
1. 应用崩溃，被faultloggerd监听到，从崩溃位置进行回栈，再符号化
2. 使用hiperf对应用进行采样，抓取到一个栈快照后进行回栈并符号化后记录

## 与本工程的协作
faultloggerd, hiperf都是外部工程，他们不是调用ark_js_vm，而是使用libark_jsruntime.so中导出的函数。主要导出函数在`ecmascript/dfx/stackinfo/js_stackinfo.h`
不管哪种回栈都需要回填参数`ArkUnwindParam`中的fp, sp, pc三个字段
* fp --- 帧头位置，每层调用的门牌，从这个位置的附近槽位可以得到当前帧是什么类型的，执行完回到哪里（PC指向哪里，也叫returnAddress），上一帧的帧头在哪里
* sp --- 帧的水位线，栈底（低地址）当前帧把栈空用到哪儿了，通常来说fp ~ sp 就是一帧使用的地址范围
* pc --- 对于JSFrame，即ArkTS代码产生的帧，不管是不是jit的，返回的是bytecode的地址

### 与faultloggerd的协作
* 走栈，调用`step_ark_with_record_jit`, 传入`ArkUnwindParam` 中fp, sp属于inout参数, 基于它读取当前帧的内容，同时需要把下一帧的数据回填到fp, sp中，而methodId属于回填参数，直接用它的值是0, 需要根据当前帧计算出来, 对于首帧，fp, sp, pc都是崩溃瞬间从寄存器读的值，对于pc来说就是fault pc
* 传入`ArkUnwindParam` 的frameIndex当前恒为0
* faultloggerd会注入一个ReadMemFunc函数，用于跨进程内存的读取，ReadMemFunc在崩溃回栈时本质上使用的是ptrace, 一次读一个word，崩溃场景下目标应用进程是被hang住的，所以用ptrace读大批量数据没有性能问题
* 符号化阶段调用，`ark_create_js_symbol_extractor`，`ark_parse_js_frame_info` 基于字节码PC(bytecodeOffset) + sourceMap还原成JSFunction的name,line
* 符号化完成后，调用`ark_write_jit_code` 把jitCode (存在JsStackInfo::machineCodeMap中) 写到ELF文件中

### 与hiperf的协作
hiperf调用faultloggerd提供的Unwinder类来回栈，因此它是间接依赖libark_jsruntime.so, 但是使用的回栈函数与崩溃场景不同
* 主要调用 ```int step_ark(void *ctx, panda::ecmascript::ReadMemFunc readMem, panda::ecmascript::ArkStepParam *arkStepParam)```来走栈，当前并不处理JIT帧对应的bcOffset

