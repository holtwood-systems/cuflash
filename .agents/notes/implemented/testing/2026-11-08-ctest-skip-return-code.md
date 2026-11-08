# Agent Note: 无 GPU 的 CTest 用 skip 返回码，而不是伪装成 Passed

Status: implemented

## Problem

`tests/test_main.cpp` 在全局 `GpuRequirementEnvironment::SetUp()` 里调用
`GTEST_SKIP()`，意图是在无 GPU 的 CI 上把 GPU 用例标成 SKIPPED。实测
（gtest 1.11 / 1.13 / 1.14 均类似）这个路径的结果是：

```text
[==========] 2 tests from 1 test suite ran. (0 ms total)
[  PASSED  ] 2 tests.
```

gtest 把“全局 environment 跳过、没有任何测试体执行”视为成功，退出码为 0；
`GTEST_SKIP()` 也没有把逐个 TestInfo 标成 skipped，所以 CTest 的
`ctest --output-on-failure` 会把 `cuflash_tests` 的每个发现式用例显示成
Passed。于是 GPU-less CI 的绿色并不代表任何 kernel 跑过，正好掩盖了
“全 skip 仍全绿”的问题。

## Decision

- `tests/test_main.cpp`：`RUN_ALL_TESTS()` 返回 0 且
  `cuda_device_available()` 为 false 时返回 77（CTest 约定的 skip 码）。
  `--gtest_list_tests` 分支保持返回 0，构建期 `gtest_discover_tests` 的
  发现不受影响。
- `CMakeLists.txt`：`gtest_discover_tests(cuflash_tests ... PROPERTIES
  SKIP_RETURN_CODE 77)`，让每个发现式测试在看到 77 时显示为 Skipped。
- 已有的 `cuflash_pytorch_comparison` 同样使用 77，语义一致。

## Alternatives considered

- **让全局 environment 的 SetUp 直接 `exit(77)`**：会跳过 gtest 的
  tear-down/environment 清理和报告，且 `--gtest_list_tests` 也可能被误伤；
  用 main 的返回值更可控。
- **在 CI 里解析 gtest 输出，发现全 skip 就失败**：会让无 GPU 的 job 变红，
  但该 job 本来就没有 GPU，红/绿都表达不了数值正确性；显示 Skipped 更准确，
  数值验证交给 GPU workflow。
- **只保留编译 smoke，明确不跑测试**：当前 CI 已经在跑 host smoke
  （api_smoke/package_smoke），无法把 GPU 用例的“未执行”与“通过”区分开；
  加 skip code 的成本更低。
- **依赖 `ctest -N` 的数量检查**：只能证明用例被注册，不能区分执行与跳过。

## Consequences

Benefits:

- 无 GPU 时 `ctest` 输出明确标记每个 GPU 用例为 Skipped；CI 的绿色不再被
  误读为“67 个用例全部通过”。
- GPU 机器上行为不变：`cudaGetDeviceCount > 0` 时返回 `RUN_ALL_TESTS()`
  的原始结果，真实失败仍然是非零。
- 构建期 `gtest_discover_tests` 的 `--gtest_list_tests` 仍退出 0。

Costs and limits:

- 直接在无 GPU 机器上手动运行 `cuflash_tests` 会得到 77 而不是 0；脚本若
  把 77 当失败需要调用方识别（这正是 CTest 的约定）。
- 只覆盖“完全没有 CUDA device”的情况；单个用例内部因架构/功能不支持而
  `GTEST_SKIP()` 时，gtest 的逐用例 skip 统计仍由 gtest 自己处理。
- 不改变 GPU workflow 的 `ctest` 调用，也不改变 host smoke 测试。

## Verification

- 用本机 gtest 1.11 + 桩 `cuda_runtime.h`（`cudaGetDeviceCount` 可配置为
  0/1）编译真实 `tests/test_main.cpp`：
  - 设备数为 0：gtest 仍打印 `PASSED`，但进程退出码为 77。
  - 设备数为 0 + `--gtest_list_tests`：退出码 0，列表正常输出。
  - 设备数为 1：退出码 0，测试正常执行。
- CMake 3.28 的 `GoogleTest.cmake` 已确认 `gtest_discover_tests` 支持
  `PROPERTIES`，且现有 `cuflash_pytorch_comparison` 使用同一
  `SKIP_RETURN_CODE 77` 约定。
- 未运行：真实 nvcc 构建、GPU workflow、无 GPU 的 GitHub Actions job；当前
  环境没有 CUDA Toolkit 与 GPU。上述验证只覆盖 main 的返回码逻辑与 CMake
  参数支持，不替代 CI 实跑。
