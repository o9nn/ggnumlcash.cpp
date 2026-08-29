#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// Financial Backend Detection & Selection - Phase 1, Issue #002 (Task 2.2)
//
// Runtime detection and selection of the compute backend used to execute the
// financial tensor kernels. The GGNuCash financial modules are intentionally
// standalone (no hard link to libllama/libggml) so they can be built and
// tested in isolation. This helper therefore detects capabilities in two
// tiers:
//
//   1. CPU feature detection (always available): SSE/AVX/AVX2/AVX-512/FMA,
//      core counts, and an estimated SIMD width. This drives kernel dispatch
//      and lets the platform report what the host can do.
//
//   2. Optional GGML backend probing: when libggml is linked by the host
//      application, a GPU/ACCEL backend (CUDA/Metal/Vulkan/SYCL) can be
//      discovered and preferred for large batch workloads. When GGML is not
//      linked the selector cleanly degrades to the CPU backend.
//
// The selected `FinancialBackend` describes *where* kernels should run and
// carries the metadata higher layers need (device name, memory, SIMD width).
// ============================================================================

namespace ggnucash {
namespace tensors {

// ============================================================================
// Backend Kinds
// ============================================================================

enum class BackendKind {
    CPU,        // Always available; vectorized CPU reference path
    CPU_AVX2,   // CPU with AVX2/FMA vectorization
    CPU_AVX512, // CPU with AVX-512 vectorization
    GPU_CUDA,   // NVIDIA CUDA (via GGML)
    GPU_METAL,  // Apple Metal (via GGML)
    GPU_VULKAN, // Cross-vendor Vulkan (via GGML)
    GPU_SYCL,   // Intel SYCL (via GGML)
    AUTO        // Let the selector choose the best available
};

inline std::string backend_kind_to_string(BackendKind kind) {
    switch (kind) {
        case BackendKind::CPU:        return "CPU";
        case BackendKind::CPU_AVX2:   return "CPU_AVX2";
        case BackendKind::CPU_AVX512: return "CPU_AVX512";
        case BackendKind::GPU_CUDA:   return "GPU_CUDA";
        case BackendKind::GPU_METAL:  return "GPU_METAL";
        case BackendKind::GPU_VULKAN: return "GPU_VULKAN";
        case BackendKind::GPU_SYCL:   return "GPU_SYCL";
        case BackendKind::AUTO:       return "AUTO";
        default:                      return "UNKNOWN";
    }
}

// ============================================================================
// CPU Capability Report
// ============================================================================

struct CpuCapabilities {
    bool has_sse;
    bool has_sse2;
    bool has_sse3;
    bool has_avx;
    bool has_avx2;
    bool has_avx512f;
    bool has_fma;
    bool has_neon;          // ARM NEON (always false on x86)
    uint32_t physical_cores;
    uint32_t logical_cores;
    size_t simd_width_bytes;  // Widest usable vector register (16/32/64)

    CpuCapabilities()
        : has_sse(false), has_sse2(false), has_sse3(false), has_avx(false),
          has_avx2(false), has_avx512f(false), has_fma(false), has_neon(false),
          physical_cores(1), logical_cores(1), simd_width_bytes(16) {}

    // Human-readable summary of the most capable instruction set available.
    std::string best_isa() const {
        if (has_avx512f) return "AVX-512";
        if (has_avx2 && has_fma) return "AVX2+FMA";
        if (has_avx2) return "AVX2";
        if (has_avx) return "AVX";
        if (has_neon) return "NEON";
        if (has_sse3) return "SSE3";
        if (has_sse2) return "SSE2";
        if (has_sse) return "SSE";
        return "scalar";
    }
};

// ============================================================================
// Selected Backend Descriptor
// ============================================================================

struct FinancialBackend {
    BackendKind kind;
    std::string device_name;
    size_t memory_bytes;        // 0 when unknown / not applicable
    size_t simd_width_bytes;
    uint32_t compute_units;     // cores or SMs
    bool is_gpu;

    FinancialBackend()
        : kind(BackendKind::CPU),
          device_name("CPU"),
          memory_bytes(0),
          simd_width_bytes(16),
          compute_units(1),
          is_gpu(false) {}

    std::string to_string() const {
        std::string s = backend_kind_to_string(kind) + " (" + device_name + ")";
        return s;
    }
};

// ============================================================================
// Backend Selector
// ============================================================================

class BackendSelector {
public:
    BackendSelector();

    // Detect host CPU capabilities (cached after first call).
    const CpuCapabilities & cpu_capabilities() const { return cpu_caps_; }

    // Enumerate the backends available on this host. Always contains at least
    // one CPU entry; GPU entries appear when discoverable.
    std::vector<FinancialBackend> enumerate_backends() const;

    // Select the best backend for a workload. `request` may be a specific
    // kind or AUTO. `workload_size` (number of elements) lets the selector
    // prefer GPU only when the batch is large enough to amortize transfer.
    FinancialBackend select(BackendKind request = BackendKind::AUTO,
                            size_t workload_size = 0) const;

    // Minimum batch size at which a GPU backend is preferred over CPU.
    static constexpr size_t gpu_preferred_threshold() { return 100000; }

private:
    CpuCapabilities cpu_caps_;

    static CpuCapabilities detect_cpu();
    static uint32_t count_physical_cores();
};

} // namespace tensors
} // namespace ggnucash
