#include "financial-backend.h"

#include <thread>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#define GGNUMLCASH_X86 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace ggnucash {
namespace tensors {

BackendSelector::BackendSelector() : cpu_caps_(detect_cpu()) {}

uint32_t BackendSelector::count_physical_cores() {
    uint32_t logical = std::thread::hardware_concurrency();
    if (logical == 0) logical = 1;
    // Without topology queries, approximate physical cores. This is only used
    // for reporting/heuristics, never for correctness.
    return logical;
}

CpuCapabilities BackendSelector::detect_cpu() {
    CpuCapabilities caps;

    uint32_t logical = std::thread::hardware_concurrency();
    caps.logical_cores = logical == 0 ? 1 : logical;
    caps.physical_cores = count_physical_cores();

#if defined(GGNUMLCASH_X86)
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

    auto cpuid = [&](unsigned int leaf, unsigned int subleaf,
                     unsigned int & a, unsigned int & b, unsigned int & c,
                     unsigned int & d) {
#if defined(_MSC_VER)
        int regs[4];
        __cpuidex(regs, (int)leaf, (int)subleaf);
        a = (unsigned int)regs[0];
        b = (unsigned int)regs[1];
        c = (unsigned int)regs[2];
        d = (unsigned int)regs[3];
#else
        __cpuid_count(leaf, subleaf, a, b, c, d);
#endif
    };

    // Leaf 1: feature bits in ECX/EDX
    cpuid(1, 0, eax, ebx, ecx, edx);
    caps.has_sse  = (edx & (1u << 25)) != 0;
    caps.has_sse2 = (edx & (1u << 26)) != 0;
    caps.has_sse3 = (ecx & (1u << 0)) != 0;
    caps.has_fma  = (ecx & (1u << 12)) != 0;
    caps.has_avx  = (ecx & (1u << 28)) != 0;

    // Leaf 7 subleaf 0: AVX2 / AVX-512F in EBX
    unsigned int max_leaf = 0;
    {
        unsigned int a2, b2, c2, d2;
        cpuid(0, 0, a2, b2, c2, d2);
        max_leaf = a2;
    }
    if (max_leaf >= 7) {
        cpuid(7, 0, eax, ebx, ecx, edx);
        caps.has_avx2    = (ebx & (1u << 5)) != 0;
        caps.has_avx512f = (ebx & (1u << 16)) != 0;
    }

    if (caps.has_avx512f)      caps.simd_width_bytes = 64;
    else if (caps.has_avx2)    caps.simd_width_bytes = 32;
    else if (caps.has_sse)     caps.simd_width_bytes = 16;

#elif defined(__ARM_NEON) || defined(__aarch64__)
    caps.has_neon = true;
    caps.simd_width_bytes = 16;
#endif

    return caps;
}

std::vector<FinancialBackend> BackendSelector::enumerate_backends() const {
    std::vector<FinancialBackend> backends;

    // CPU backend is always present, tagged with the best detected ISA.
    FinancialBackend cpu;
    if (cpu_caps_.has_avx512f) {
        cpu.kind = BackendKind::CPU_AVX512;
    } else if (cpu_caps_.has_avx2) {
        cpu.kind = BackendKind::CPU_AVX2;
    } else {
        cpu.kind = BackendKind::CPU;
    }
    cpu.device_name = "CPU (" + cpu_caps_.best_isa() + ")";
    cpu.simd_width_bytes = cpu_caps_.simd_width_bytes;
    cpu.compute_units = cpu_caps_.logical_cores;
    cpu.is_gpu = false;
    backends.push_back(cpu);

    // GPU backends are only discoverable when the host process links GGML and
    // a vendor runtime is present. The financial module is standalone, so it
    // cannot probe them directly here; the application layer can register a
    // detected GPU via select() by requesting a specific GPU kind. We expose
    // the CPU entry unconditionally and leave GPU enumeration to the host.
    return backends;
}

FinancialBackend BackendSelector::select(BackendKind request,
                                         size_t workload_size) const {
    auto backends = enumerate_backends();
    FinancialBackend cpu = backends.front();

    // Explicit CPU-class request: return the detected CPU with its ISA tier.
    if (request == BackendKind::CPU || request == BackendKind::CPU_AVX2 ||
        request == BackendKind::CPU_AVX512) {
        // Honor the request but never claim a higher ISA tier than the host
        // supports.
        if (request == BackendKind::CPU_AVX512 && !cpu_caps_.has_avx512f) {
            return cpu;
        }
        if (request == BackendKind::CPU_AVX2 && !cpu_caps_.has_avx2) {
            return cpu;
        }
        FinancialBackend chosen = cpu;
        chosen.kind = request;
        return chosen;
    }

    // GPU requests: without a linked GGML runtime the standalone module can
    // only honor them when the workload is large enough to be worth a future
    // offload; otherwise fall back to CPU. We return the requested kind so
    // callers that *do* link GGML can dispatch accordingly.
    if (request == BackendKind::GPU_CUDA || request == BackendKind::GPU_METAL ||
        request == BackendKind::GPU_VULKAN || request == BackendKind::GPU_SYCL) {
        if (workload_size >= gpu_preferred_threshold()) {
            FinancialBackend gpu;
            gpu.kind = request;
            gpu.device_name = backend_kind_to_string(request) + " (via GGML)";
            gpu.is_gpu = true;
            return gpu;
        }
        return cpu;
    }

    // AUTO: pick the best available. Prefer a GPU only for large workloads;
    // for small/medium batches the CPU avoids transfer overhead and wins on
    // latency (the dominant concern for financial kernels).
    return cpu;
}

} // namespace tensors
} // namespace ggnucash
