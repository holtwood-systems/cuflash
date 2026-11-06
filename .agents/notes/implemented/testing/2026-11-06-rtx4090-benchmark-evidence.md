# Agent Note: RTX 4090 原始 benchmark 证据归档 + 泄漏测试并发污染

Status: implemented

## Problem

`docs/performance/benchmarks.md` 规定"下一份可引用结果必须归档
硬件/软件/commit、Google Benchmark JSON、命令、原始日志"，但仓库
此前没有任何一份满足该协议的包（V100/A100/H100 表格无原始数据，
RTX 3060 只有描述性数字）。另外并行 ctest 暴露
`StressTest.RepeatedExecutionNoLeak` 在 `-j8` 下误报泄漏
（399 MB 差值），隔离复跑通过——该用例用 `cudaMemGetInfo` 差值
测量，与并发测试共享设备时不可靠。

## Decision

- 在 `docs/performance/results/<date>-<gpu>/` 归档协议要求的全部
  产物：GBench JSON、串行/并行 ctest 日志、provenance 表与命令、
  `report.md`（摘要 + 边界 + 失败项）。首批
  `2026-11-06-rtx4090/`（commit `6c4f310`，sm_89-only cubin）。
- 泄漏测试并发污染的结论：**该用例只适用于串行执行**；不修改
  测试代码（改测量方式属于后续独立决策），但把并行失败日志与
  串行通过日志一并归档，避免"删除失败证据"。

## Alternatives considered

- 只归档通过日志：违反"失败显式保留"的仓库纪律。
- 立即改用 `cudaMalloc` 记账修测试：需要动测试基建，本批聚焦
  证据产出；已在 report.md 边界中登记为后续项。

## Consequences

- 本仓有了第一份可按协议复核的硬件快照；旧跨 GPU 表格继续不可引用。
- CI 若以并行方式跑 ctest，可能继续出现该假阳性；报告已写明
  串行要求，后续可在测试内加串行守卫或换测量方式。

## Verification

`ctest`（串行）71/71 通过、1 skip；`ctest -j8` 失败日志与隔离
复跑（115 ms 通过）均有记录；`cuobjdump --list-elf` 确认 sm_89。
