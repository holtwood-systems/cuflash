# Agent Note: FlashDecoding 的 partials scratch 不能跨 stream/设备共享

Status: implemented

## Problem

`flash_decoding.cu` 的 `launch_flash_decoding_typed()` 用函数级
`static float* scratch` / `static size_t scratch_bytes` 缓存 partials：

```cpp
static float* scratch = nullptr;
static size_t scratch_bytes = 0;
if (scratch == nullptr || scratch_bytes < need_bytes) { ... }
```

这假设同一时刻只有一条流、一个设备在调用 decode。实际情况：

1. **同一设备、不同 stream 并发 decode**：两次调用共用同一块
   `[num_chunks, bh_total, D]` partials。第二次调用会在第一次的 partial /
   combine kernel 尚未结束时覆盖 `partial_o/m/l`，输出静默错误。
2. **不同设备**：scratch 在第一次调用的设备上 `cudaMalloc`，后续在另一台
   设备上调用时直接复用该指针，属于跨设备非法地址。
3. **扩容路径**：`scratch_bytes` 不够时直接 `cudaFree(scratch)`。如果另一条
   流上的 kernel 仍在读写该块，`cudaFree` 的隐式同步语义并不覆盖所有
   非阻塞流场景，存在 use-after-free 风险。
4. 函数级 static 没有析构释放，进程退出时依赖驱动回收。

## Decision

scratch 改为每次调用独立分配，并绑定到调用者的 stream：

```cpp
float* scratch = nullptr;
bool scratch_from_pool = true;
if (cudaMallocAsync(&scratch, need_bytes, stream) != cudaSuccess) {
    scratch_from_pool = false;
    (void)cudaGetLastError();
    if (cudaMalloc(&scratch, need_bytes) != cudaSuccess) {
        return FlashAttentionError::CUDA_ERROR;
    }
}
// ... partial / combine kernel 均在同一 stream 上 launch ...

if (scratch_from_pool) {
    if (cudaFreeAsync(scratch, stream) != cudaSuccess) {
        return FlashAttentionError::CUDA_ERROR;
    }
} else {
    cudaError_t sync_err = cudaStreamSynchronize(stream);
    cudaFree(scratch);
    if (sync_err != cudaSuccess) {
        return FlashAttentionError::CUDA_ERROR;
    }
}
```

`cudaMallocAsync` / `cudaFreeAsync` 从设备默认内存池取块，按 stream 顺序
生效：combine kernel 之后才归还，不阻塞主机。设备或驱动不支持
stream-ordered allocator 时（`cudaMallocAsync` 返回
`cudaErrorNotSupported` 等），退回同步 `cudaMalloc`，显式
`cudaStreamSynchronize` 后再 `cudaFree`。

## Alternatives considered

- **给函数加全局 mutex**：实现最简单，但把并发 decode 完全串行化，违背
  多流推理的初衷；而且跨设备场景仍然错误（同一把锁下设备 A 的指针仍可能
  被设备 B 复用）。
- **按 `(device, stream)` 缓存 scratch**：能保住“只分配一次”的性能，但需要
  处理 stream 销毁后的缓存失效、无上限的 map 增长和清理时机；stream 句柄
  被调用者销毁后无法安全判断，容易变成泄漏或悬空缓存。复杂度明显高于
  stream-ordered 分配。
- **每次 `cudaMalloc` + `cudaFree`**：完全正确，但 `cudaMalloc`/`cudaFree`
  的同步开销正是旧 static 缓存想避免的；decode 每步都会付出该代价。
- **`thread_local` 缓存**：同一线程可以用多条流，线程也可能切换设备；
  作用域比 static 小，但仍然不能表达“按 stream/设备隔离”。

## Consequences

Benefits:

- 不同 stream、不同设备的并发 decode 各自持有独立的 partials，不再有覆盖
  或跨设备指针复用。
- 支持内存池的设备上，每步分配是异步的、不阻塞主机；性能仍由设备内存池
  吸收，不需要显式缓存管理。
- 扩容/释放不再有跨流 use-after-free 窗口。

Costs and limits:

- 每次 decode 增加一次 `cudaMallocAsync` + `cudaFreeAsync` 的池操作；相对
  长 KV 扫描的 kernel 时间很小，但短序列、极小 batch 下不再是零开销。
- 老设备/驱动退回同步路径时会阻塞主机，且该路径下 decode 与同流内核仍是
  串行语义；这是可用性兜底，不是目标性能路径。
- 内存池的保留策略由 CUDA 默认内存池决定；本库不修改全局 pool 属性。

## Verification

- clang 18 host-only + 最小 CUDA runtime/bfloat16/fp16 shim 对
  `src/forward/flash_decoding.cu` 做 `-fsyntax-only`：通过（`-fgpu-defer-diag`；
  仅用于宿主侧语义检查，不替代 nvcc）。
- 主机侧 harness（把 `cudaMallocAsync`/`cudaFreeAsync`/`cudaConfigureCall`/
  `cudaSetupArgument`/`cudaLaunch` 换成记录桩）：
  - 修复后代码：每次调用一次 async 分配 + 一次同流异步释放，两次 kernel
    launch，无 `cudaMalloc`/`cudaStreamSynchronize`；连续两次不同 stream
    的调用拿到不同指针。
  - 同一 harness 对 `HEAD` 的旧实现：10 项断言失败（复用 static、不分配、
    不释放、fallback 行为不符）。
  - fallback 桩（`cudaMallocAsync` 返回不支持）：走
    `cudaMalloc` + `cudaStreamSynchronize` + `cudaFree`，并调用
    `cudaGetLastError` 清理错误。
- CUDA 12.2.2 devel 容器 + `-DCMAKE_CUDA_ARCHITECTURES=89` 完整编译
  `cuflash_tests` 通过（nvcc 12.2.140、GCC 11.4）；新增
  `FlashDecodingTest.ConcurrentStreamsKeepIndependentScratch`：两条 stream
  交替 20 次 decode、num_chunks 4/8 强制不同 scratch 尺寸，不逐步同步，
  再分别与 CPU reference 对拍。
- 无 GPU 环境下 `cuflash_tests` 返回 CTest skip 码 77；全量发现 68 项用例。
- 未运行：真实 GPU 上的数值对拍与并发压力测试。容器只验证编译与 launch
  控制流，不验证 kernel 结果。
