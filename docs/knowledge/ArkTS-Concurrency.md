# ArkTS 并发知识库

本文记录 `arkcompiler/ets_runtime` 仓内的并发编程知识，涵盖锁、原子操作、内存序、写屏障、共享堆 vs 本地堆差异，以及常见陷阱。所有涉及多线程（Worker、Sendable、GC daemon、JIT thread）的开发需求必须先阅读本文。

## 1. 并发设施总览

### 1.1 通用并发原语（STL）

| 类别 | 设施 | 头文件 | 适用场景 |
|---|---|---|---|
| 互斥锁 | `Mutex` | `ecmascript/platform/mutex.h` | 独占访问，不可重入 |
| 可重入互斥锁 | `RecursiveMutex` | `ecmascript/platform/mutex.h` | 同线程嵌套加锁（IC、JIT） |
| 读写锁 | `RWLock` / `MutatorLock` | `ecmascript/platform/mutex.h` | 读多写少 |
| RAII 持有者 | `LockHolder` / `ReadLockHolder` / `WriteLockHolder` | `ecmascript/platform/mutex.h` | 自动加锁/解锁 |
| 条件变量 | `ConditionVariable` | `ecmascript/platform/mutex.h` | 等待/通知 |
| futex 屏障 | `SuspendBarrier` | `ecmascript/js_thread.h` | 线程挂起/恢复 |
| 原子变量 | `std::atomic<T>` | `<atomic>` | 无锁读写、计数、状态标志 |
| CAS | `std::atomic_compare_exchange_strong_explicit` | `<atomic>` | 无锁 compare-and-swap |
| 原子 store | `std::atomic<T>::store(val, order)` | `<atomic>` | 原子写（选 release 发布） |
| 原子 load | `std::atomic<T>::load(order)` | `<atomic>` | 原子读（选 acquire 消费） |
| RMW | `fetch_add` / `fetch_sub` / `exchange` | `<atomic>` | 原子读-改-写 |
| 内存 fence | `std::atomic_thread_fence(order)` | `<atomic>` | 发布可见性配对 |
| GC 断言作用域 | `DisallowGarbageCollection` 等 | `ecmascript/mem/assert_scope.h` | 禁止/允许 GC、堆分配、Handle 分配 |
| 线程状态切换 | `ThreadManagedScope` / `ThreadNativeScope` | `ecmascript/checkpoint/thread_state_transition.h` | 切换线程状态（managed/native/GC） |
| IC 锁 | `JSThread::GetIcAccessorLock()` | `ecmascript/js_thread.h` | IC 多线程访问器保护（`RecursiveMutex`） |

### 1.2 ArkTS 堆 Barrier API

以下 API 封装了 GC 写屏障 + 原子操作，**仅用于 ArkTS 堆对象（TaggedObject）字段写**。普通并发场景用 1.1 的 STL 原语。

| API | 头文件 | 用途 |
|---|---|---|
| `Barriers::AtomicSetPrimitive` | `ecmascript/mem/barriers.h` | CAS 写堆字段（release/relaxed），底层 `compare_exchange_strong` + 写屏障 |
| `Barriers::SynchronizedSetObject` | `ecmascript/mem/barriers-inl.h` | 原子 store 堆字段（release）+ GC 写屏障 |
| `Barriers::SetObject` | `ecmascript/mem/barriers-inl.h` | 非原子写堆字段 + GC 写屏障 |
| `Barriers::SetPrimitive` | `ecmascript/mem/barriers.h` | 非原子写堆字段（无屏障） |
| `Barriers::GetTaggedValue` | `ecmascript/mem/barriers.h` | 非原子读堆字段（含可选读屏障） |
| `Barriers::GetTaggedValueAtomic` | `ecmascript/mem/barriers.h` | 原子读堆字段（按 RBMode 选内存序） |
| `Barriers::Update` | `ecmascript/mem/barriers.cpp` | 本地堆写屏障核心：标记 + RememberedSet |
| `Barriers::UpdateShared` | `ecmascript/mem/barriers.cpp` | 共享堆写屏障核心：原子标记 + 跨堆 RSet |

### 1.3 Sendable 容器并发 API

| 设施 | 头文件 | 用途 |
|---|---|---|
| `ConcurrentApiScope` | `ecmascript/shared_objects/concurrent_api_scope.h` | **仅限 Sendable 容器**（SendableArray/Map/Set 等）读写分离，基于 CAS 操作容器的 `MOD_RECORD_OFFSET`，冲突时 fail-fast 抛 `CONCURRENT_MODIFICATION_ERROR` |

> `ConcurrentApiScope` 不是通用读写分离工具。非 Sendable 容器场景不要使用。

## 2. 选型决策树

```
需要保护共享可变状态？
├─ 是 → 是否是 ArkTS 堆对象字段写？
│   ├─ 是 → 用 Barriers API（见 1.2），自动处理 GC 屏障
│   └─ 否 → 是否是跨 Worker 共享（shared heap）？
│       ├─ 是 → 是否可以用无锁 CAS？
│       │   ├─ 是 → std::atomic_compare_exchange_strong_explicit（release/relaxed）
│       │   └─ 否 → 跨 Worker 锁需进程级单例 → LockHolder（Mutex）
│       └─ 否（单 Worker / 同线程内并发）→ IcAccessorLock（RecursiveMutex）或 LockHolder
└─ 否 → 不需要同步
```

### 2.1 锁选择

| 场景 | 选择 | 理由 |
|---|---|---|
| IC 槽访问（解释器 + JIT 同线程） | `IcAccessorLock` (RecursiveMutex) | 可重入，同线程嵌套安全 |
| 字符串表操作 | `EcmaStringTableMutex` | 专用，避免与其他锁竞争 |
| GC 挂起/恢复 | `SuspendBarrier` (futex) | 用户态快速唤醒，避免自旋 |
| PGO profiler 读写 | `ReadLockHolder` / `WriteLockHolder` (RWLock) | 读多写少 |
| 模块加载 | `LockHolder` (Mutex) | 独占，简单 |
| Sendable 容器读写 | `ConcurrentApiScope` | 无锁 reader/writer，fail-fast |

**注意**：运行时不使用自旋锁（`SpinLock`）。所有等待走 futex 或条件变量。

### 2.2 原子操作选择

| 场景 | 选择 | 理由 |
|---|---|---|
| 单字段 CAS（状态转换） | `std::atomic_compare_exchange_strong_explicit` | release 成功 / relaxed 失败 |
| 原子写发布 | `std::atomic<T>::store(val, memory_order_release)` | 配对读者 acquire load |
| 原子读消费 | `std::atomic<T>::load(memory_order_acquire)` | 配对写者 release store |
| 纯计数/统计 | `std::atomic<T>::fetch_add(1, memory_order_relaxed)` | 无数据依赖 |
| RMW 触发清理 | `std::atomic<T>::fetch_sub(1, memory_order_acq_rel)` | 减到 0 时双向同步 |
| ArkTS 堆字段 CAS | `Barriers::AtomicSetPrimitive` | CAS + GC 写屏障（封装上述原语） |
| ArkTS 堆字段原子写 | `Barriers::SynchronizedSetObject` | release store + GC 写屏障 |
| GC mark bit 置位 | `Region::AtomicMark` | 原子位操作 |

## 3. 内存序规则

### 3.1 基本规则

| 内存序 | 适用场景 | 示例 |
|---|---|---|
| `relaxed` | 无依赖的计数、统计 | `count_.fetch_add(1, std::memory_order_relaxed)` |
| `acquire` (load) | 读被其他线程 release 发布的值 | `flag_.load(std::memory_order_acquire)` |
| `release` (store) | 写发布（CAS 成功、store 指针） | `ptr_.store(val, std::memory_order_release)` |
| `acq_rel` | RMW 且需双向同步 | `cnt_.fetch_sub(1, std::memory_order_acq_rel)` |
| `seq_cst` | 最强需求（屏障、线程状态） | `std::atomic_thread_fence(std::memory_order_seq_cst)` |

### 3.2 CAS 内存序

CAS 使用 release/relaxed 配对：

```cpp
// 正确：CAS 发布
std::atomic<T> slot;
T expected = UNINIT;
bool success = slot.compare_exchange_strong_explicit(
    expected, desired,
    std::memory_order_release,   // 成功：发布写
    std::memory_order_relaxed);  // 失败：无需同步
if (success) {
    // desired 已 release 发布，读者用 acquire load 可见
}
```

### 3.3 何时用 fence

**场景 1：非原子写 + 原子发布 → 读者用 acquire load**

```cpp
// 写者：先用非原子写填充数组内容，再用 CAS 原子发布指针
arr[0] = weakHc;       // 裸 store
arr[1] = handler;      // 裸 store
slot.compare_exchange_strong_explicit(
    expected, arrPtr,
    std::memory_order_release, std::memory_order_relaxed);

// 读者：acquire load 即可保证数组内容可见
auto arr = slot.load(std::memory_order_acquire);
// arr[0], arr[1] 保证可见
```

**场景 2：快路径直接读字段（无法改 atomic load）→ 用 acquire fence**

当读路径通过裸指针读字段（无法改为 `std::atomic::load`），用 fence 补齐可见性：

```cpp
// 读路径：通过 TaggedArray* 直接读字段（非 atomic load）
auto arr = TaggedArray::Cast(cachedValue.GetTaggedObject());
if (arr->IsInSharedHeap()) {
    std::atomic_thread_fence(std::memory_order_acquire);  // 配对 writer 的 release CAS
}
// 非共享堆跳过 fence（由锁的 acquire-release 保证）
auto val = arr->Get(thread, 0);  // 保证可见
```

**选择原则**：能改读路径为 `acquire load` 就用 `acquire load`（更高效，编译器可优化）；无法改读路径时才用 `acquire fence`。

## 4. 共享堆 vs 本地堆 Barrier 差异

ArkTS 堆对象字段写必须走 `Barriers` API（见 1.2），自动处理 GC 屏障。以下为共享堆与本地堆 barrier 差异：

| 维度 | 本地堆 (LocalHeap) | 共享堆 (SharedHeap) |
|---|---|---|
| 写屏障触发 | `thread->IsConcurrentMarkingOrFinished()` | `thread->IsSharedConcurrentMarkingOrFinished()` |
| 写屏障 API | `Barriers::Update` | `Barriers::UpdateShared` |
| RememberedSet | `InsertOldToNewRSet` | `InsertLocalToShareRSet`（local→shared 跨堆引用） |
| 对象标记 | Update 内部处理 | `AtomicMark`（daemon 线程并发标记） |
| 跨堆引用断言 | `ASSERT(!objRegion->InSharedHeap() \|\| valRegion->InSharedHeap())` — shared→local 非法 | 同左 |
| 弱引用检查 | `CHECK_NO_LOCAL_TO_SHARE_WEAK_REF_HANDLE` — local→shared 弱引用需 `ALLOW_LOCAL_TO_SHARE_WEAK_REF_HANDLE` | — |
| 对象拷贝 fence | `cc_evacuator.cpp` | `shared_cc_evacuator-inl.h` — memcpy 后 `seq_cst` fence 防半拷贝 |

### 读屏障（Concurrent Copy 场景）

GC 并发拷贝（Concurrent Copy）期间，对象可能正在被 evacuator 从 FromRegion 搬到 ToRegion。此时其他线程读该对象字段必须经过读屏障，否则可能读到已失效的旧地址。

**触发条件**：`thread->NeedReadBarrier()` 为 true（GC 并发拷贝阶段）。

**核心逻辑**（`ReadBarrierImpl`，`ecmascript/mem/barriers.cpp`）：
1. 读字段值 `value`（可能指向 FromRegion 中的旧对象）
2. 判断 `objectRegion->IsFromRegion()` — 是否在待搬移区域
3. 若是，读 `MarkWord` 判断 `IsForwardingAddress()` — 是否已搬移
4. 若已搬移，用 CAS 更新 slot 指向新地址（forwarding），返回新地址
5. 若未搬移，触发 `Copy()` 执行搬移并返回新地址

```cpp
// ReadBarrierImpl 简化逻辑
TaggedObject *object = value.GetHeapObject();
Region *objectRegion = Region::ObjectAddressToRange(object);
if (objectRegion->IsFromRegion()) {
    MarkWord markWord(object, RELAXED_LOAD);
    TaggedObject *toObject = markWord.IsForwardingAddress()
        ? markWord.ToForwardingAddress()       // 已搬移 → 用新地址
        : thread->GetLocalCCEvacuator()->Copy(object, markWord);  // 未搬移 → 触发搬移
    // CAS 更新 slot，后续读到旧地址的线程也能看到新地址
    if (value.IsWeak()) {
        slot.CASUpdateWeak(rawObject, toObject);
    } else {
        slot.CASUpdate(rawObject, toObject);
    }
    return slot.GetTaggedType();  // 返回新地址
}
return slot.GetTaggedType();  // 非 FromRegion → 无需屏障
```

**关键点**：
- 读屏障在本地堆和共享堆上行为一致，仅触发条件不同（本地堆由 local GC 触发，共享堆由 shared GC 触发）
- 读屏障仅在 GC 并发拷贝阶段触发，非 GC 期间 `NeedReadBarrier()` 返回 false，读操作零开销
- 弱引用（`IsWeak()`）走 `CASUpdateWeak`，普通引用走 `CASUpdate`
- `MarkWord` 用 `RELAXED_LOAD` 读取（无数据依赖，仅需原子性）
- 字符串表有专用 `ReadBarrierForStringTableSlotImpl`（`ecmascript/mem/barriers.cpp`），逻辑类似但独立维护

### 关键不变式

1. **shared→local 引用非法**：共享堆对象不能引用本地堆对象。写屏障会断言拦截。
2. **local→shared 引用需记录**：本地堆对象引用共享堆对象时，需写入 `LocalToShareRSet`，供 SharedGC 扫描。
3. **弱引用跨堆**：从本地堆写弱引用到共享堆对象，必须用 `ALLOW_LOCAL_TO_SHARE_WEAK_REF_HANDLE` 作用域。

## 5. 常见陷阱

### 5.1 Torn Read（半写可见）

**问题**：多字段写不原子，读者看到半写状态。

```cpp
// 错误：两次独立原子写不构成原子对
slot1.store(val1, std::memory_order_release);  // 写1
slot2.store(val2, std::memory_order_release);  // 写2
// 读者可能在写1和写2之间看到 val1(新) + val2(旧)
```

**修复**：将多字段打包到单个对象，CAS 原子发布对象指针。

```cpp
// 正确：先填充对象，再 CAS 发布指针
arr[0] = val1;  // 裸 store 填充
arr[1] = val2;  // 裸 store 填充
slot.compare_exchange_strong_explicit(
    expected, arrPtr,
    std::memory_order_release, std::memory_order_relaxed);  // CAS 原子发布
```

### 5.2 CAS 失败后破坏赢家数据

**问题**：CAS 失败后非 CAS 写覆盖赢家的有效数据。

```cpp
// 错误：CAS 失败 → 非原子写覆盖赢家
bool success = slot.compare_exchange_strong_explicit(
    expected, desired, std::memory_order_release, std::memory_order_relaxed);
if (!success) {
    slot.store(HOLE, std::memory_order_relaxed);  // ❌ 砸掉赢家数据
}
```

**修复**：CAS 失败时 skip（return），不写 slot。赢家状态保留，loser 下次重试。

```cpp
bool success = slot.compare_exchange_strong_explicit(
    expected, desired, std::memory_order_release, std::memory_order_relaxed);
if (!success) {
    return;  // ✅ skip，不破坏赢家数据
}
```

### 5.3 发布可见性缺失

**问题**：用裸 store 写对象内容，用 relaxed load 读指针。ARM64 上读者看到新指针但内容 stale。

```cpp
// 错误：无 fence
arr[0] = weakHc;  // 裸 store
slot.compare_exchange_strong_explicit(
    expected, arrPtr, std::memory_order_release, std::memory_order_relaxed);
auto ptr = slot.load(std::memory_order_relaxed);  // relaxed → 数组内容可能 stale
auto val = arr[0];
```

**修复**：读者侧用 `acquire load`（可改读路径时）或 `acquire fence`（无法改读路径时）。

```cpp
// 修复 1：acquire load（优先）
auto ptr = slot.load(std::memory_order_acquire);
auto val = arr[0];  // 保证可见

// 修复 2：acquire fence（读路径无法改为 atomic load 时）
auto ptr = slot.load(std::memory_order_relaxed);
std::atomic_thread_fence(std::memory_order_acquire);
auto val = arr[0];  // 保证可见
```

### 5.4 TOCTOU 竞争（read-check-write）

**问题**：先读再检查再写，读和写之间无原子保证。

```cpp
// 错误：TOCTOU
auto data = slot.load(std::memory_order_relaxed);  // 读
if (data == expected) {                           // 检查
    slot.store(newVal, std::memory_order_relaxed); // 写 — 另一线程可能在检查后抢占
}
```

**修复**：用 CAS 替代 read-check-write。CAS 在硬件级保证原子性。

## 6. 并发代码检查清单

在提交涉及并发的代码前，逐项检查：

- [ ] **多字段写是否原子**？多字段是否打包到单对象 + CAS 发布？不能用两次独立 `store(release)` 替代。
- [ ] **CAS 失败后是否破坏赢家数据**？CAS 失败应 skip（return），不写 slot。不能在 CAS 失败后调任何非 CAS 写操作。
- [ ] **发布可见性是否有 fence/load 配对**？裸 store + CAS 发布 → 读者侧 acquire load 或 acquire fence。
- [ ] **是否有 TOCTOU（read-check-write）**？用 CAS 替代。不能先 load 再判断再 store。
- [ ] **共享堆 vs 本地堆 barrier 是否正确**？`IsInSharedHeap()` 分支是否覆盖？写共享堆对象字段是否走 `Barriers::UpdateShared`？
- [ ] **跨堆弱引用是否有 `ALLOW_LOCAL_TO_SHARE_WEAK_REF_HANDLE`**？
- [ ] **shared→local 引用是否被断言拦截**？不能从共享堆引用本地堆。
- [ ] **锁序是否一致**？多锁场景是否避免死锁（统一锁序）？
- [ ] **并发 dedup 是否需要**？两个 Worker 并发 miss 相同输入时，是否会在 CAS 前各自构建重复项？若可能，CAS 前需扫描去重。
