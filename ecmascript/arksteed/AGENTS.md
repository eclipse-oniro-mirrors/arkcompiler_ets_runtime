# ecmascript/arksteed

## 模块定位
ArkTS轻量化JIT编译器，对中等热度代码执行优化编译，注重编译时间与性能收益的平衡

## 关键源码索引
- arksteed_compiler.cpp ArkSteedCompilerTask::Compile 编译整体流程
- arksteed_bytecode_analysis.cpp BytecodeAnalysis::Run 字节码级活跃性分析（块级 + 字节码级）
- arksteed_graph_builder.cpp GraphBuilder::Run()  逐字节码翻译为 IR Vertex, PGO 反馈驱动内联、类型特化、常量折叠等
- arksteed_compiler.cpp RunPreRegallocProcessors 寄存器分配预处理，IR活跃性计算
- arksteed_regalloc.cpp ArkSteedRegisterAllocator 线性扫描式寄存器分配器
- arksteed_codegen.cpp 汇编生成

## 代码修改约束规则
- **改动点分析** 给出改动点原依赖处的列表，提交人工确认


## 词汇触发路由

| 术语 | 含义 | 指向 |
|------|------| ------ |
| **PGO 反馈** | 解释器采集的类型/调用点反馈，驱动 GraphBuilder 的内联、类型特化、常量折叠。 | 读取 `ArkSteedFeedbackReader`（`arksteed_feedback_reader.h`）；加工后形态 `ProcessedFeedbackKind`（`arksteed_processed_feedback.h`）、`arksteed_pgo_access_info.h` |
| **CFG** | Control Flow Graph，控制流图。节点是基本块，边是跳转/落空关系。 | IR 基本块 `BB`（`arksteed_bb.h`）；字节码级 CFG `BytecodePreprocessor::BasicBlockInfo`（`arksteed_bytecode_preprocessor.h`） |
| **RPO** | Reverse Post Order。保证对于可归约 CFG，前驱块的 RPO 编号小于后继块，是多数数据流分析的正确遍历顺序。 | `MakeRPO()`（`arksteed_bytecode_preprocessor.cpp`） |
| **LiveIn / LiveOut** | 块入口/出口处的活跃变量集合。 | 字节码级：`BytecodeAnalysis` 的 `blockLiveIn_/blockLiveOut_/bcLiveIn_/bcLiveOut_`（`arksteed_bytecode_analysis.h`，迭代求解 `UpdateLiveness` `arksteed_bytecode_analysis.cpp`）；IR 级：`LivenessProcessor`（`arksteed_regalloc_processors.h`） |
| **Phi 节点** | 在控制流汇合点处合并不同前驱传来的值的 IR 节点。 | `PhiVertex`（`arksteed_opcode.h`），GraphBuilder 创建后添加 `BB::AddPhiVertex`（`arksteed_bb.h`） |
| **CompileInfoFacts** | 节点事实缓存 | `CompileInfoFacts`（`arksteed_compile_info_facts.h`）文档 `docs/knowledge/JIT/arksteed_compile_info_facts.md` |
| **Safepoint** | 安全点，GC 可暂停的位置。代码生成阶段为每条可能触发 GC 的指令记录栈映射。 | 表结构 `ArkSteedSafepointHeader`/`ArkSteedSafepointEntry`/`ArkSteedSafepointTable` (`arksteed_safepoint_table.h`) |
| **eager deopt** | 即时去优化：JIT 代码内检查失败点立即退出优化代码 | ABI 定义 `arksteed_deopt_abi.h`（`x64_eager_deopt_abi`/`aarch64_eager_deopt_abi` 不同namespace区分不同架构 `ArkSteedDeoptSnapshot`） |
| **lazy deopt** | 延迟去优化：JIT 帧已在栈上（如调用运行时例程中）时依赖失效，仅 patch 返回地址、不建新帧，返回时回退解释器。 | 返回入口 stub `LazyDeoptEntry`（`ecmascript/stubs/runtime_stubs.h`） |



## 知识索引

| 场景 | 文档位置 |
| --- | --- |
| 栈帧构建，遍历，类型与作用 | `docs/knowledge/JIT/arksteed_frames.md `|
| 堆常量优化 | `docs/knowledge/JIT/arksteed_heap_constant.md` |
| 节点事实缓存 | `docs/knowledge/JIT/arksteed_compile_info_facts.md` |
| 崩溃、性能采样跨进程回栈流程（faultloggerd, hiperf依赖） | `docs/knowledge/JIT/arksteed_unwind.md` |


## 构建验证
构建方法参考ets_runtime项目根目录AGENTS.md

### 功能测试
#### 工程内用例
从OpenHarmony源码根目录切换到arkcompiler/ets_runtime/ecmascript/arksteed/test
```bash
python ./run_arksteed_tests.py -m debug -F # See ./README.md for details of <params>

#arm64 测试
python ./run_arksteed_tests.py -p arm64 -m release --skip-build
```
该测试可以基于测试文件中的特殊注释(//! 开头的)对dump的CFG进行IR正确性测试，注释的详细用法见 `ecmascript/arksteed/test/README.md`

#### 约束
- 涉及字节码优化相关改动，在`ecmascript/arksteed/test`下新增的ts测试源文必须有`//! `开头的CFG校验注释

### Done定义
涉及代码修改的任务完成必须满足
1. 所有修改的文件LSP诊断无新增error
2. 代码格式化已执行
3. libark_jsruntime编译成功
4. run_arksteed_tests.py 运行全量通过
