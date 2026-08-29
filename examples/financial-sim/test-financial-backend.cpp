#include "financial-backend.h"
#include "financial-memory-pool.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace ggnucash::tensors;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                                 \
    do {                                                                           \
        std::cout << "  Testing: " << name << "... ";                              \
        try {

#define TEST_END(name)                                                             \
            std::cout << "PASSED" << std::endl;                                    \
            tests_passed++;                                                        \
        } catch (const std::exception & e) {                                       \
            std::cout << "FAILED: " << e.what() << std::endl;                      \
            tests_failed++;                                                        \
        } catch (...) {                                                            \
            std::cout << "FAILED: Unknown exception" << std::endl;                 \
            tests_failed++;                                                        \
        }                                                                          \
    } while (0)

#define ASSERT_TRUE(cond) do { if (!(cond)) { throw std::runtime_error("Assertion failed: " #cond " at line " + std::to_string(__LINE__)); } } while(0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { throw std::runtime_error("Assertion failed: " #a " != " #b " at line " + std::to_string(__LINE__)); } } while(0)

// ============================================================================
// Backend Detection Tests
// ============================================================================

void test_cpu_capabilities_detected() {
    TEST("CPU capabilities are detected");
    BackendSelector selector;
    const auto & caps = selector.cpu_capabilities();

    ASSERT_TRUE(caps.logical_cores >= 1);
    ASSERT_TRUE(caps.physical_cores >= 1);
    ASSERT_TRUE(caps.simd_width_bytes == 16 || caps.simd_width_bytes == 32 ||
                caps.simd_width_bytes == 64);
    ASSERT_TRUE(!caps.best_isa().empty());
    TEST_END("CPU capabilities are detected");
}

void test_backend_enumeration_has_cpu() {
    TEST("Backend enumeration always includes CPU");
    BackendSelector selector;
    auto backends = selector.enumerate_backends();
    ASSERT_TRUE(backends.size() >= 1);
    ASSERT_FALSE(backends[0].is_gpu);
    ASSERT_TRUE(backends[0].device_name.find("CPU") != std::string::npos);
    TEST_END("Backend enumeration always includes CPU");
}

void test_auto_select_returns_cpu_for_small_workload() {
    TEST("AUTO selects CPU for small workload");
    BackendSelector selector;
    auto backend = selector.select(BackendKind::AUTO, 100);
    ASSERT_FALSE(backend.is_gpu);
    TEST_END("AUTO selects CPU for small workload");
}

void test_gpu_request_small_falls_back_to_cpu() {
    TEST("GPU request for small workload falls back to CPU");
    BackendSelector selector;
    auto backend = selector.select(BackendKind::GPU_CUDA, 10);
    ASSERT_FALSE(backend.is_gpu);  // small batch -> CPU
    TEST_END("GPU request for small workload falls back to CPU");
}

void test_gpu_request_large_honored() {
    TEST("GPU request for large workload is honored");
    BackendSelector selector;
    auto backend = selector.select(BackendKind::GPU_CUDA,
                                   BackendSelector::gpu_preferred_threshold() + 1);
    ASSERT_TRUE(backend.is_gpu);
    ASSERT_EQ(backend.kind, BackendKind::GPU_CUDA);
    TEST_END("GPU request for large workload is honored");
}

void test_cpu_request_respects_host_isa() {
    TEST("CPU request never claims unsupported ISA tier");
    BackendSelector selector;
    const auto & caps = selector.cpu_capabilities();
    auto backend = selector.select(BackendKind::CPU_AVX512, 0);
    if (!caps.has_avx512f) {
        // Host lacks AVX-512: selector must downgrade
        ASSERT_TRUE(backend.kind != BackendKind::CPU_AVX512);
    }
    TEST_END("CPU request never claims unsupported ISA tier");
}

void test_backend_kind_strings() {
    TEST("backend_kind_to_string mappings");
    ASSERT_EQ(backend_kind_to_string(BackendKind::CPU), std::string("CPU"));
    ASSERT_EQ(backend_kind_to_string(BackendKind::GPU_CUDA), std::string("GPU_CUDA"));
    ASSERT_EQ(backend_kind_to_string(BackendKind::AUTO), std::string("AUTO"));
    TEST_END("backend_kind_to_string mappings");
}

// ============================================================================
// Memory Pool Tests
// ============================================================================

void test_pool_basic_acquire_release() {
    TEST("Pool acquire/release basic");
    FinancialMemoryPool pool(128, 8);
    ASSERT_TRUE(pool.block_size() >= 128);

    void * a = pool.acquire();
    void * b = pool.acquire();
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    ASSERT_TRUE(a != b);

    pool.release(a);
    pool.release(b);

    auto s = pool.stats();
    ASSERT_EQ(s.used_blocks, (uint64_t)0);
    ASSERT_EQ(s.acquires, (uint64_t)2);
    ASSERT_EQ(s.releases, (uint64_t)2);
    TEST_END("Pool acquire/release basic");
}

void test_pool_block_alignment() {
    TEST("Pool blocks are cache-line aligned");
    FinancialMemoryPool pool(64, 4);
    void * p = pool.acquire();
    ASSERT_EQ(reinterpret_cast<uintptr_t>(p) % 64, (uintptr_t)0);
    pool.release(p);
    TEST_END("Pool blocks are cache-line aligned");
}

void test_pool_reuses_freed_blocks() {
    TEST("Pool reuses freed blocks (LIFO free list)");
    FinancialMemoryPool pool(64, 4);
    void * a = pool.acquire();
    pool.release(a);
    void * b = pool.acquire();
    ASSERT_EQ(a, b);  // freed block handed back out
    pool.release(b);
    TEST_END("Pool reuses freed blocks (LIFO free list)");
}

void test_pool_grows_when_exhausted() {
    TEST("Pool grows when exhausted");
    FinancialMemoryPool pool(64, 2);
    std::vector<void *> ptrs;
    for (int i = 0; i < 10; i++) {
        ptrs.push_back(pool.acquire());
    }
    auto s = pool.stats();
    ASSERT_EQ(s.used_blocks, (uint64_t)10);
    ASSERT_TRUE(s.total_blocks >= 10);
    ASSERT_TRUE(s.growth_events >= 1);
    for (void * p : ptrs) pool.release(p);
    ASSERT_EQ(pool.stats().used_blocks, (uint64_t)0);
    TEST_END("Pool grows when exhausted");
}

void test_pool_high_water_mark() {
    TEST("Pool tracks high-water mark");
    FinancialMemoryPool pool(64, 4);
    std::vector<void *> ptrs;
    for (int i = 0; i < 3; i++) ptrs.push_back(pool.acquire());
    for (void * p : ptrs) pool.release(p);
    ptrs.clear();

    // Drain and reuse; high-water mark should stay at 3.
    void * one = pool.acquire();
    pool.release(one);

    auto s = pool.stats();
    ASSERT_EQ(s.high_water_mark, (uint64_t)3);
    TEST_END("Pool tracks high-water mark");
}

void test_pool_reserve() {
    TEST("Pool reserve grows capacity");
    FinancialMemoryPool pool(64, 2);
    pool.reserve(50);
    ASSERT_TRUE(pool.stats().total_blocks >= 50);
    TEST_END("Pool reserve grows capacity");
}

void test_pool_blocks_writable() {
    TEST("Pool blocks are fully writable");
    FinancialMemoryPool pool(256, 1);
    void * p = pool.acquire();
    std::memset(p, 0xAB, 256);  // write across the whole block
    auto * bytes = static_cast<unsigned char *>(p);
    ASSERT_EQ(bytes[0], (unsigned char)0xAB);
    ASSERT_EQ(bytes[255], (unsigned char)0xAB);
    pool.release(p);
    TEST_END("Pool blocks are fully writable");
}

void test_pool_thread_safe() {
    TEST("Pool is thread-safe under contention");
    FinancialMemoryPool pool(64, 8);
    const int threads = 4;
    const int iters = 2000;
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&pool, iters]() {
            for (int i = 0; i < iters; i++) {
                void * p = pool.acquire();
                pool.release(p);
            }
        });
    }
    for (auto & w : workers) w.join();

    auto s = pool.stats();
    ASSERT_EQ(s.used_blocks, (uint64_t)0);
    ASSERT_EQ(s.acquires, (uint64_t)(threads * iters));
    ASSERT_EQ(s.releases, (uint64_t)(threads * iters));
    TEST_END("Pool is thread-safe under contention");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Financial Backend & Memory Pool Tests" << std::endl;
    std::cout << "  (Phase 1, Issue #002 - Tasks 2.2/2.3)" << std::endl;
    std::cout << "============================================\n" << std::endl;

    std::cout << "--- Backend Detection ---" << std::endl;
    test_cpu_capabilities_detected();
    test_backend_enumeration_has_cpu();
    test_auto_select_returns_cpu_for_small_workload();
    test_gpu_request_small_falls_back_to_cpu();
    test_gpu_request_large_honored();
    test_cpu_request_respects_host_isa();
    test_backend_kind_strings();

    std::cout << "\n--- Memory Pool ---" << std::endl;
    test_pool_basic_acquire_release();
    test_pool_block_alignment();
    test_pool_reuses_freed_blocks();
    test_pool_grows_when_exhausted();
    test_pool_high_water_mark();
    test_pool_reserve();
    test_pool_blocks_writable();
    test_pool_thread_safe();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
