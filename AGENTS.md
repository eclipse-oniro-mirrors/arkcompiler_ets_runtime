# ArkTS AGENTS.md (ets_runtime)

> 使用 agent 修改代码时，新增目录、功能或特性开关，或改变接口、架构边界时，必须同步更新本指南及受影响的子目录指南和知识页。

## 1. 项目定位

本 AGENTS.md 适用于 `arkcompiler/ets_runtime`。
本仓库实现 ArkTS 运行时——OpenHarmony 上执行 ArkTS/TS/JS 的默认运行时。
提供字节码解释执行、AOT/JIT 编译、模块加载，并发共享，内存管理（GC）、NAPI C++ 互操作及 ES2021 标准库。

## 2. 代码地图

### 2.1 关键目录（按修改频率与风险分级）

| 目录 | 风险 | 原因 |
|-----------|------|-----|
| `ecmascript/arksteed/` | 🔴 高 | 轻量化JIT；平衡编译时间与优化效果 |
| `ecmascript/interpreter/` | 🔴 高 | 核心节码执行引擎；此处的缺陷会影响所有 ArkTS 应用 |
| `ecmascript/module/` | 🔴 高  | ES 模块加载器；与 CommonJS 互操作 |
| `ecmascript/mem/` | 🔴 高 | GC 与分配器；对性能和稳定性高度敏感 |
| `ecmascript/napi/` | 🟡 中 | 公共 C++ API 接口层；兼容性敏感 |
| `ecmascript/builtins/` | 🟡 中 | ES 标准库；符合ECMA2021规范 |
| `ecmascript/compiler/` | 🟡 中 | AOT 编译器；类型推断 + 代码生成，与前端紧密相关 |
| `ecmascript/jit/` | 🟡 中 | JIT 编译器；性能敏感 |
| `ecmascript/ic/` | 🟡 中 | 内联缓存；与解释器紧密耦合 |
| `ecmascript/ts_types/` | 🟡 中 | 类型元数据；影响 AOT 编译质量 |
| `ecmascript/dfx/` | 🟡 中 | 诊断/观测/性能分析（CPU 采样、堆快照、栈追踪、tracing） |
| `ecmascript/debugger/` | 🟡 中 | JS调试器（断点/单步/热重载/DropFrame） |
| `ecmascript/extractortool/` | 🟡 中 | SourceMap 解析与 HAP 提取集成 |
| `ecmascript/patch/` | 🟡 中 | Quick Fix/Hot Reload/Cold Patch方法替换 |
| `common_components/` | 🟢 低 | 共享基础设施；稳定，较少变更 |
| `test/` | 🟢 低 | 测试相关 |
| `docs/` | 🟢 低 | 仅文档 |

### 2.2 嵌套指南

按实际修改路径查阅 [嵌套指南和补充入口](docs/knowledge/README.md)。新增或移动指南时，在仓根运行 `git ls-files --cached --others --exclude-standard '*/AGENTS.md'` 核对清单。

## 3. 知识路由

在规划或编辑代码前，说明任务类别、已读文档、适用约束和验证计划。

### 3.1 基于任务的路由

| 任务 | 代码起点 | 先读 |
|---|---|---|
| 公共 API 或 NAPI 行为 | `ecmascript/napi/` | [NAPI 指南](ecmascript/napi/AGENTS.md) |
| 架构或模块边界 | `BUILD.gn`、`bundle.json` | [架构概览](docs/overview.md) |
| 字节码执行和属性访问 | `ecmascript/interpreter/`、`ecmascript/ic/` | [解释器](ecmascript/interpreter/AGENTS.md)、[IC](ecmascript/ic/AGENTS.md) |
| GC、内存或分配 | `ecmascript/mem/`、`common_components/heap/` | [GC](docs/knowledge/ArkTS-GC.md) |
| 并发、共享堆与线程切换 | `ecmascript/checkpoint/`、`ecmascript/cross_vm/` | [并发](docs/knowledge/ArkTS-Concurrency.md) |
| DFX、采样、堆快照或追踪 | `ecmascript/dfx/` | [DFX](docs/knowledge/DFX-Subsystem.md) |
| 断点、单步、Drop Frame | `ecmascript/debugger/` | [Debugger](docs/knowledge/Debugger-Subsystem.md)、[子目录指南](ecmascript/debugger/AGENTS.md) |
| 压缩包集成或 SourceMap | `ecmascript/extractortool/` | [SourceMap 与 HAP 集成](docs/knowledge/ExtractorTool-Subsystem.md) |
| Quick Fix、热重载和冷补丁 | `ecmascript/patch/`、`ecmascript/quick_fix/` | [Patch](docs/knowledge/Patch-Subsystem.md) |
| AOT 编译或服务 | `ecmascript/compiler/`、`compiler_service/` | [AOT](docs/aot-guide_zh.md)、[编译服务](compiler_service/AGENTS.md) |
| ArkSteed 或 JIT | `ecmascript/arksteed/`、`ecmascript/jit/` | [ArkSteed](ecmascript/arksteed/AGENTS.md)、[JIT](ecmascript/jit/AGENTS.md) |
| TypeScript 类型标识 | `ecmascript/ts_types/` | [类型标识指南](ecmascript/ts_types/AGENTS.md) |
| 模块加载或 CommonJS | `ecmascript/module/`、`ecmascript/require/` | [模块](docs/knowledge/ArkTS-Module.md) |
| ES 标准库 | `ecmascript/builtins/` | [Builtins](ecmascript/builtins/AGENTS.md)及适用的 ECMAScript 规范 |
| 非 Sendable 的 `util.Collections` 容器（`JSAPIHashMap`、`JSAPIVector`、`JSAPITreeMap` 等） | `ecmascript/containers/`（容器注册与 JS 方法入口）、`ecmascript/js_api/`（容器对象与迭代器实现） | [容器接口测试](ecmascript/containers/tests/BUILD.gn)、[容器对象与迭代器测试](ecmascript/tests/BUILD.gn) |
| 构建选项或测试接线 | `BUILD.gn`、`js_runtime_config.gni`、`test/test_helper.gni` | [构建环境](docs/environment-setup-and-compilation.md) |

### 3.2 基于路径的路由

| 修改路径 | 先读 | 注意事项 |
|---|---|---|
| `ecmascript/napi/` | [NAPI 指南](ecmascript/napi/AGENTS.md) | 签名与生命周期兼容性 |
| `ecmascript/mem/`、`common_components/heap/` | [GC](docs/knowledge/ArkTS-GC.md)、[并发](docs/knowledge/ArkTS-Concurrency.md) | 屏障、共享堆和 runtime_core ABI |
| `ecmascript/compiler/` | [编译器](ecmascript/compiler/AGENTS.md)、[编译服务](compiler_service/AGENTS.md) | AOT 变更须与编译服务保持同步 |
| `ecmascript/interpreter/`、`ecmascript/ic/` | [解释器](ecmascript/interpreter/AGENTS.md)、[IC](ecmascript/ic/AGENTS.md) | 字节码语义与性能回归 |
| `ecmascript/module/` | [模块](docs/knowledge/ArkTS-Module.md) | 模块身份与加载时序 |
| `ecmascript/dfx/`、`ecmascript/debugger/` | [DFX](docs/knowledge/DFX-Subsystem.md)、[Debugger](docs/knowledge/Debugger-Subsystem.md) | 热路径、采样和回调 |
| `ecmascript/extractortool/` | [SourceMap 与 HAP 集成](docs/knowledge/ExtractorTool-Subsystem.md) | 缓冲区和构建条件 |
| `ecmascript/patch/`、`ecmascript/quick_fix/` | [Patch](docs/knowledge/Patch-Subsystem.md) | 方法替换和补丁生命周期 |
| `ecmascript/builtins/` | [Builtins](ecmascript/builtins/AGENTS.md) | 标准库可观察行为 |

### 3.3 基于术语的路由

当任务、issue、日志、API 或变更文件涉及以下术语时，在规划前阅读对应文档：

| 术语 | 功能描述 | 阅读 |
|------|-----------|------|
| NAPI / JSNAPI | 提供napi C++ API inner接口层；签名变更会破坏下游 | `ecmascript/napi/AGENTS.md` |
| AOT / ark_aot_compiler | 机器码编译；与 compiler_service SA 协同工作 | `compiler_service/AGENTS.md` + `docs/aot-guide_zh.md` |
| GC / CMS-GC / Partial-Compressing-GC | 内存管理关键路径；修改需关注性能与稳定性风险 | `docs/knowledge/ArkTS-GC.md` |
| DFX / HiLog / heapsnapshot / CPU profiling / SIGPROF / tracing | 诊断/观测/性能分析；旁路路径不得影响主链路性能 | `docs/knowledge/DFX-Subsystem.md` |
| debugger / 调试器 / 断点 / dropframe / single step | 调试器子系统；BytecodePcChanged是热路径，非调试模式须零开销 | `docs/knowledge/Debugger-Subsystem.md` |
| interpreter / IC / 内联缓存 | 与解释器紧密耦合；性能敏感 | `ecmascript/ic/AGENTS.md` + `ecmascript/interpreter/AGENTS.md` |
| PGO | 配置文件引导优化；与 AOT 关联 | [PGO 子模块指南](ecmascript/pgo_profiler/AGENTS.md) + `docs/aot-guide_zh.md` |
| TaskPool / Actor / Sendable | 并发模型；影响启动性能 | `docs/knowledge/ArkTS-Sendable.md` |
| 并发 / CAS / 原子操作 / 锁 / 内存序 / fence / write barrier / shared heap / 多线程 / Worker | 并发编程；涉及锁、原子操作、内存序、写屏障、共享堆 | `docs/knowledge/ArkTS-Concurrency.md` |
| .abc / 字节码 / jspandafile | 字节码格式；须保持向后兼容 | `ecmascript/jspandafile/AGENTS.md` |
| Stub / StubCompiler | 运行时桩代码；必须匹配解释器调用约定 | `ecmascript/stubs/AGENTS.md` |
| Module | 模块加载器 | `docs/knowledge/ArkTS-Module.md` |
| HAP / SourceMap / VLQ / Extractor / extractortool | SourceMap 翻译和 ability_base 提取集成，留意缓冲区生命周期 | `docs/knowledge/ExtractorTool-Subsystem.md` |
| Quick Fix / Hot Reload / Cold Patch / patch / 热重载 / 补丁 | 运行时代码替换，方法不在调用栈上才能替换 | `docs/knowledge/Patch-Subsystem.md` |

## 4. 约束与边界

### 4.1 架构不变量

1. 仅可执行由 ts2abc/es2abc 生成的 ArkCompiler 字节码（.abc 文件）。
2. 仅支持 ES2021 strict 模式；不支持 sloppy 模式。
3. 不支持动态代码执行（`eval`、`new Function()`）。
4. NAPI 是唯一支持的 C++ 互操作路径。
5. .abc 中的类型元数据须保持编译器工具链与运行时版本之间的向后兼容。
6. GC 堆布局变更须保持与 runtime_core 的 ABI 兼容性。
7. DFX 代码必须是旁路观测路径，采样/追踪开关关闭时须零开销或仅一次条件分支。
8. Patch 方法替换必须在方法不在当前调用栈上时进行。`StageOfHotReload` 枚举的当前值存于 `JSThread::GlueData`，变更枚举或状态转换时检查所有读取点。

### 4.2 开发注意事项

- 修改 `TaggedObject` 派生类布局时，需注意不同数据类型在 32/64 位平台上的宽度差异；`JSTaggedValue` 或 GC 访问的 tagged 字段需按 `JSTaggedValue::TaggedTypeSize()` 对齐，`SIZE` 也应为该粒度的倍数，避免 `HClass` object size 截断或字段越界。
- 修改 `StringTable`/`Method`/`Module` 等虚拟机常驻基础数据结构时，须检查结构体 `SIZE` 是否增长，并提醒用户：此类变更影响进程级常驻内存（随 VM/Isolate 实例数量线性放大），需评估常驻内存与启动开销。
- 修改 `TaggedObject` 派生类的 bit 字段布局时，须用 `static_assert` 校验其与对应 `Literal` 结构字段的 offset 一致，避免类型元数据读取错位。
- 接口内引入缓存优化时，须检查缓存是否使用强引用（裸指针/`Global<>`/全局表登记）且是否设有大小上限与淘汰策略，防止常驻泄漏或无界增长。
- 使用位域或 tag pointer 时，须检查标记位是否占用指针高位（57-64 位），与 HWAsan 的 shadow 地址空间冲突。
- 修改 IR 或 C++ 解释器任一路径的字节码语义时，须检查另一路径是否存在相同逻辑并保持行为一致（见 `ecmascript/interpreter/AGENTS.md` 与 `ecmascript/compiler/AGENTS.md`）。
- 修改对象布局、删除字段或变更字段语义时，须判定是否构成不兼容变更（影响 `.abc` 类型元数据/`.so` AOT 产物/堆快照/补丁兼容），并显式提醒用户其影响范围。
- 涉及多线程并发（CAS、原子操作、锁、共享堆写屏障、内存序 fence）开发，须先阅读 `docs/knowledge/ArkTS-Concurrency.md` 并对照检查清单。对无锁复合状态发布，使用单一原子槽发布完整且初始化后的状态；读者从同一原子槽取得该发布值，再用 acquire 语义访问其内容。锁保护的多字段更新沿用既有锁协议，托管堆字段遵守 GC barrier 与对象生命周期要求。

### 4.3 不要做

- 除非明确要求，不要修改公共 NAPI 签名、错误码或生命周期语义。
- 未经批准，不要引入新的第三方生产依赖。
- 不要修改自动生成的文件（如 builtins stubs）；应修改源码生成器后重新生成。
- 不要移除或变更错误码而不更新 `docs/`。
- 不要只更新 C++ 或 IR 解释器一侧的共有字节码语义。

### 4.4 需先确认的事项

以下事项涉及现有的兼容性或授权边界。用户尚未明确授权时，先确认影响范围。

- 变更公共 NAPI 签名、错误码或生命周期。
- 引入新的第三方生产依赖。
- 改变 `.abc` 或 AOT 产物的兼容性约定。
- 改变与 `runtime_core` 共享的堆布局或 ABI。
- 引入解释器、模块加载或补丁协议的行为不兼容。

### 4.5 常见反模式

- ❌ 直接编辑生成的桩代码。修改生成输入后重新生成。
- ❌ 只检查改动的解释器路径。追踪 C++ 和 IR 路径中对应的字节码逻辑。
- ❌ 把测试可执行文件的构建当作测试运行。核对目标定义，并记录实际执行结果。

## 5. 构建与验证

构建命令从 OpenHarmony 源码根目录执行。按 [构建环境](docs/environment-setup-and-compilation.md) 选择产品和工具链。

### 5.1 standalone 构建

以下 standalone 示例仅适用于已配置的 standalone 构建树。标准 OpenHarmony 产品构建见 §5.4。

```bash
# Linux
python ark.py x64.debug

# 用QEMU模拟测试ARM64用
python ark.py arm64.debug --gn-args="run_with_qemu=true"
```

### 5.2 standalone ARM64 测试补充说明

如果已有的 ARM64 standalone 构建目录缺少 host 侧 `es2abc`，可单独构建。

```bash
ninja -C out/arm64.release arm64.release/arkcompiler/ets_frontend/es2abc
```

### 5.3 快速构建方法

以下命令使用已有的 `out/x64.debug` standalone 构建目录。

#### 5.3.1 增量构建运行时库

```bash
# Linux
ninja -C out/x64.debug libark_jsruntime
```

#### 5.3.2 跑abc/test262 需要VM可执行文件（会自动带上so依赖）

```bash
# Linux
ninja -C out/x64.debug ark_js_vm
```

#### 5.3.3 构建单测依赖库

```bash
# Linux
ninja -C out/x64.debug libark_jsruntime_test
```

### 5.4 完整编译命令

```bash
# 构建 Linux host 工具
./build.sh --product-name rk3568 --build-target ark_js_host_linux_tools_packages

# 带调试符号构建
./build.sh --product-name rk3568 --build-target ark_js_host_linux_tools_packages --gn-args is_debug=true
```

### 5.5 按变更类型验证

| 变更 | 最小验证 |
|---|---|
| Builtins 或运行时内部行为 | 构建受影响目标，运行对应单测或模块回归 |
| 解释器、IC、Stub | 检查两条解释路径的语义，运行受影响的用例及性能回归 |
| GC、对象布局或并发 | 运行对应的 [GC 测试](docs/knowledge/ArkTS-GC.md#测试与验证)，检查受影响的布局和工具链 |
| 公共 NAPI | 检查下游签名及生命周期兼容性，运行对应 JSNAPI 用例 |
| AOT、JIT 或 PGO | 编译受影响组件，运行相关编译器及运行时回归 |
| 编译服务 IPC 或权限 | 核对接口与代理/Stub、服务配置，执行相关服务测试 |
| 模块加载和补丁 | 运行对应的模块或 [Quick Fix 回归](docs/knowledge/Patch-Subsystem.md#代码和测试) |
| DFX、Debugger、SourceMap | 运行对应知识页登记的测试，检查受影响的诊断路径 |
| 构建配置或特性开关 | 核对 GN 目标、产品条件及 `bundle.json`，构建受影响产物 |
| 仅测试变更 | 运行改动与受影响的相邻测试 |
| 仅文档变更 | 检查 diff、引用路径、源码符号和列出的 GN 目标 |

### 5.6 完成判据

- 差异符合任务范围，且 `git diff --check` 通过。
- 变更行为及相关失败路径有验证证据。
- 已记录适用验证的命令、配置和真实结果。
- 已核对被影响的 API、ABI 或产物格式兼容性。
- 受影响的文档路由、链接、符号与目标和当前源码一致。

**不得将未执行、执行失败或被跳过的验证描述为已通过。** 记录未验证项、原因、应执行的命令及预期结果。有替代证据时注明来源和覆盖范围。修复验证失败后，复跑原检查并记录结果。

交付说明应包含改动摘要、实际验证结果、兼容性影响和剩余事项。

## 6. 提交与 PR 规范

向 openharmony 主仓提交代码时，commit message 与 PR 必须符合以下规范。可参考近期格式规范的 AI 辅助提交，如 `5d6dc99d2`（`fix(ic): handle JSProxy holder in LoadMiss`）。

### 6.1 Commit message 格式

```
<type>: <简短描述>                      # 第一行标题，必须 ≤48 字符（含 type 与空格）

<body>                                  # 空一行后写正文：做了什么、为什么

Issue: https://gitcode.com/openharmony/arkcompiler_ets_runtime/issues/<N>
Signed-off-by: <name> <email>

AI[100%] Human Fixed[0%] Human[0%] AI Adopted[100%]
Co-Authored-By: <tool> (<model>) <ai@local>

Change-Id: Ixxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

要点：

- **标题**：`<type>: <desc>`，**≤48 字符**。`type` 取 `fix` / `feat` / `test` / `refactor` / `docs` / `perf` 等，可带作用域如 `fix(ic):`。标题过长会被门禁拒绝。
- **正文**：空一行后说明做了什么、为什么，每行建议 ≤72 字符。
- **Issue**：**必须**先在 `https://gitcode.com/openharmony/arkcompiler_ets_runtime` 新建一个 issue（描述动机/范围/验收点），再把其完整 URL 填入 `Issue:` 行。每条 commit 都要有对应 issue。
- **Signed-off-by**：用 `git config user.name` 与 `user.email`（DCO 要求）。
- **AI 辅助标记**：由 AI 参与时加两行：
  - `AI[100%] Human Fixed[0%] Human[0%] AI Adopted[100%]`（按实际 AI 贡献比例调整百分比）
  - `Co-Authored-By: <tool> (<model>) <ai@local>`（本环境为 `opencode (glm-5.2)`；其他工具如实填写，如 `codex (gpt-5.6-sol)`）
- **Change-Id**：由 `.git/hooks/commit-msg`（repo 钩子）在首次提交时自动注入。**`git commit --amend` 时务必保留原 Change-Id**——删掉会被钩子生成新的，导致同一改动产生新评审、PR/CR 关联断裂。
- **字段顺序**：标题 →（空行）→ 正文 →（空行）→ `Issue:` → `Signed-off-by:` →（空行）→ `AI[..]` → `Co-Authored-By:` →（空行）→ `Change-Id:`。

### 6.2 推送到 fork 与创建 PR

1. **推到自己的 fork**：`git push <fork-remote> HEAD:master`，`<fork-remote>` 指向你名下的 fork（`gitcode.com/<you>/arkcompiler_ets_runtime`）。若已 amend 改写历史，用 `--force-with-lease`。
2. **创建 PR**：源 `fork:master` → 目标 `openharmony:master`。可用 gitcode API（无需网页）：
   - 取 token：`printf 'protocol=https\nhost=gitcode.com\n\n' | git credential fill` 中的 `password=` 即 personal access token（若已配置 credential helper 可直接取到缓存 token）。
   - `POST https://gitcode.com/api/v5/repos/openharmony/arkcompiler_ets_runtime/pulls`，头 `Authorization: Bearer <token>`，JSON body：`{"title","body","head":"<you>:master","base":"master"}`。返回 `number` 即 PR 号。
3. **PR 描述**：**必须用本仓模板**填写关联 Issue、修改原因、修改描述及自测结果。按实际适用范围勾选性能和 IR 自检项。L0 通过后勾选「已验证」并附结果。确实不涉及 L0 时勾选「不涉及」并说明理由。适用的 L0 如未执行或失败，记录其状态和原因。可用 `PATCH .../pulls/<N>` 更新 title/body。更新后若 GET 仍见旧值，可带 `?_t=<ms>` 与 `Cache-Control: no-cache` 复查。 [PR 模板](.gitee/PULL_REQUEST_TEMPLATE.zh-CN.md)

### 6.3 常用 API 速查

| 操作 | 方法 | URL |
|------|------|-----|
| 新建 issue | POST | `/api/v5/repos/openharmony/arkcompiler_ets_runtime/issues` |
| 创建 PR | POST | `/api/v5/repos/openharmony/arkcompiler_ets_runtime/pulls` |
| 查看/更新 PR | GET/PATCH | `/api/v5/repos/openharmony/arkcompiler_ets_runtime/pulls/<N>` |

鉴权统一用 `Authorization: Bearer <token>`（Gitee 风格 v5 API）。

### 6.4 端到端提交流程（AI 辅助）

以下描述 AI 辅助提交的完整端到端流程，从改动到门禁触发：

```
①  本地改动 → 编译验证（GN gen 0 ERROR + ninja target 编译通过）
②  创建 Issue（POST /issues，描述动机/范围/验收点）→ 拿到 issue number
③  git add + git commit -F commit_msg.txt（commit message 含 Issue URL + Signed-off-by + AI 标记）
④  git push <fork-remote> <branch>:refs/heads/<branch> [--force-with-lease]
⑤  创建 PR（POST /pulls，body 用 .gitee 模板，head=<fork>:<branch>, base=master）
⑥  触发门禁：POST /pulls/<N>/comments，body="start build"
    - 若 PR 含 AI 生成代码，用 "start build draft"（draft 模式，需人工确认后正式编译）
    - 若 PR 为纯人工或已人工 review，用 "start build"
⑦  轮询门禁结果：GET /pulls/<N> 看 mergeable_state / labels
    - "代码门禁通过" = 通过；"代码门禁未通过" = 需查 CI 报告修复
⑧  若门禁失败：修复 → amend（保留 Change-Id）→ force-push → 重新评论 start build
```

**AI 生成代码的 draft 流程**：

当 commit 由 AI 辅助生成时，触发编译用 `start build draft` 而非 `start build`。draft 模式下门禁执行编译但标记为草稿，需人工 review 确认后转为正式构建。流程：

```
1. AI 生成代码改动 + commit message 标记 AI[100%] / Co-Authored-By
2. push 到 fork 分支
3. 创建 PR，PR 描述自测项如实勾选
4. 评论 "start build draft" 触发 draft 编译
5. 人工 review 代码 → 确认无问题 → 评论 "start build" 转正式编译
6. 正式门禁通过后可合入
```

**门禁状态解读**：

| 标签 / 状态 | 含义 |
|---|---|
| `dco检查成功` | DCO 签名通过 |
| `静态检查成功` | 静态代码检查通过 |
| `代码门禁通过` | 编译 + 测试全通过，可合入 |
| `代码门禁未通过` | 编译或测试失败，查 CI 报告修复 |
| `waiting_on_author` | 等待作者处理 review 意见 |
| `检测到同仓同分支存在2个或以上PR` | 需先关闭旧 PR |
| `你因提交次数过多被限制提交` | 触发频率超限，需等冷却或申请恢复 |

### 6.5 Push 前强制检查清单

**每次 push 前必须逐项检查，缺一不可。不检查不 push。**

§5.5 用于选择本地验证范围。准备向上游推送时，同时执行本节的提交检查。

```
□ 1. Commit message 标题 ≤48 字符（含 type: 和空格）
□ 2. Commit message 含 Issue: URL（且与 PR 关联的 Issue 一致）
□ 3. Commit message 含 Signed-off-by（git config user.name / user.email）
□ 4. AI 辅助提交含 AI[...] + Co-Authored-By 两行
□ 5. Change-Id 保留（amend 时未删）
□ 6. 本地编译通过（gn gen 0 ERROR + ninja target 编译 0 FAILED）
□ 7. PR 描述使用 .gitee/PULL_REQUEST_TEMPLATE.zh-CN.md 模板
□ 8. PR 描述的自测项按 §6.2 记录实际结果和适用范围
□ 9. PR 描述记录实际测试结果；未运行的测试注明原因
```

创建 PR 后，按 §6.4 触发远端门禁并检查结果。

### 6.6 示例

格式范本见本仓近期 commit `5d6dc99d2`（含 Issue / Signed-off-by / AI 行 / Co-Authored-By / Change-Id 完整字段）。
