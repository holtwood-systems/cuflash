# 2026-11-06 RTX 4090 forward/backward/decode benchmark 快照

按 `docs/performance/benchmarks.md` 复验协议产出的首份**带原始
Google Benchmark JSON** 的 sm_89 快照。

## Provenance

| 项 | 值 |
|---|---|
| GPU | NVIDIA GeForce RTX 4090（sm_89），驱动 550.107.02 |
| CUDA Toolkit | 12.8（nvcc 12.8.61） |
| 构建 | CMake Release，`-DCMAKE_CUDA_ARCHITECTURES=89`；`cuobjdump --list-elf` 确认仅 sm_89 cubin |
| commit | `6c4f310`（构建时 clean） |
| 命令 | `cmake -S . -B build-sm89 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=89 -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON && cmake --build build-sm89 -j16 && ./cuflash_bench --benchmark_format=json --benchmark_min_time=0.5s` |
| 原始数据 | `gbench-forward.json`（52 项）、`ctest-serial.log`、`ctest-parallel-j8.log` |

## 测试

- **串行 ctest：71/71 通过**（1 skip：`cuflash_pytorch_comparison`，
  需可选 PyTorch 集成）；
- 并行 `ctest -j8` 首跑有 1 项失败：`StressTest.RepeatedExecutionNoLeak`
  报 399,704,064 B 增长。**隔离复跑通过（115 ms）**，根因是并发测试
  污染 `cudaMemGetInfo` 差值测量——该用例只适合串行执行，两份日志
  均保留。

## Benchmark 摘要（`--benchmark_min_time=0.5s`，单位 ms）

| 类别 | 代表点 | 延迟 |
|---|---|---|
| NaiveForward FP32 | seq 2048 / d 64 | 9.208 |
| Forward FP16 | seq 1024 / 2048 | 0.338 / 0.857 |
| Forward BF16 | seq 2048 / d 64 | 0.877 |
| Backward FP32 | seq 1024 / 4096 | 4.921 / 69.470 |
| Backward FP16 | seq 1024 / 4096 | 6.711 / 97.967 |
| Backward BF16 | seq 4096 / 4096(d128) | 97.948 / 191.815 |
| Decode FP16 | kv 65536 / d 64 | 0.246 |
| GridY 溢出冒烟 | iter 10 | 0.005 |

- FP16/BF16 forward 比 Naive FP32 快约 10–20×（同 shape）；
- FP16 与 BF16 backward 基本同速（6.707 vs 6.707 ms @1024）；
- 与旧 RTX 3060 描述性数字不构成前后对比（旧数字无 raw）。

## 边界

- ncu/nsys 未采集：本环境 `ERR_NVGPUCTRPERM`（宿主 profiling 权限），
  无 profiler 产物；
- 单 GPU、单次运行；GBench 内部迭代已含稳态，但无跨运行方差统计；
- 参考级实现，未接 CUTLASS/cuDNN，不代表生产库上限；
- `StressTest.RepeatedExecutionNoLeak` 的串行/并行差异要求 CI 串行
  执行该用例（或改用 `cudaMalloc` 记账替代 `cudaMemGetInfo`）。
