#include <cuda_runtime.h>
#include <gtest/gtest.h>

#include <cstring>

namespace {

// CTest 约定的 skip 返回码；与 CMakeLists.txt 中 cuflash_tests 的
// SKIP_RETURN_CODE 保持一致。
constexpr int kCtestSkipReturnCode = 77;

bool is_listing_tests(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gtest_list_tests") == 0) {
            return true;
        }
    }
    return false;
}

bool cuda_device_available() {
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    return err == cudaSuccess && device_count > 0;
}

// Skips every test when no CUDA device is present so that CI (which runs on
// GPU-less runners) reports these cases as SKIPPED rather than silently
// passing them. A green CI that never executed a single kernel is worse than
// a red one: it hides regressions behind a false sense of correctness.
class GpuRequirementEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        if (!cuda_device_available()) {
            GTEST_SKIP() << "CUDA device not available; skipping GPU tests";
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    const bool listing_tests = is_listing_tests(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);

    // Test discovery (--gtest_list_tests) must succeed without a GPU so that
    // gtest_discover_tests can enumerate cases at build time; the skip logic
    // only applies when the tests actually run.
    if (!listing_tests) {
        ::testing::AddGlobalTestEnvironment(new GpuRequirementEnvironment);
    }

    const int result = RUN_ALL_TESTS();

    // gtest 把全局 environment 的 GTEST_SKIP() 当作“没有测试运行”，
    // 退出码仍是 0，CTest 会把整轮报成 Passed。返回 skip 码让无 GPU
    // 的 CI 明确显示 Skipped，而不是伪装成全绿。
    if (!listing_tests && result == 0 && !cuda_device_available()) {
        return kCtestSkipReturnCode;
    }
    return result;
}
