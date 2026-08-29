# GGNuCash Financial-Sim: Extended Modules

This document describes the modules added on top of the original
`financial-sim` example as part of the GGNuCash platform completion work.
Every module is **standalone and dependency-free** (links only against the C++
standard library and threads) so it can be built, tested, and embedded without
pulling in libllama/libggml. Each ships with a `test-*.cpp` suite following
the repo's established test conventions.

## Build & Test

The modules compile directly with g++ (no CMake configure step needed):

```bash
cd examples/financial-sim
g++ -std=c++17 -I. -o /tmp/test-pdf test-pdf-report.cpp pdf-report.cpp -pthread
/tmp/test-pdf
```

Or via CMake (each has a `test-<name>` target):

```bash
cmake --build build --target test-pdf-report test-financial-tensors \
    test-financial-backend test-flow-analytics test-fraud-detection
ctest --test-dir build --output-on-failure -R "pdf|tensor|backend|flow|fraud"
```

## Module Index

### `pdf-report.h/.cpp` — Minimal PDF Report Writer (Phase A.1)
Dependency-free PDF 1.4 writer producing valid, self-contained documents with
automatic pagination, configurable page size (Letter/A4) and monospace layout.
Used by `AuditPersistenceAdapter::export_to_pdf` to hand audit reports to
external auditors. **Tests:** `test-pdf-report.cpp` (13).

### `financial-tensors.h/.cpp` — Financial Tensor Kernels (Issue #002 / Task 2.1)
CPU reference implementations of the core quantitative kernels, written with
tensor-shaped APIs (flat buffers + element counts) so they map onto GGML
backends when available: Black-Scholes + full Greeks, binomial/trinomial
trees, Monte Carlo VaR/Expected Shortfall, portfolio linear algebra
(covariance, matvec, dot), and statistics (`normal_cdf/pdf/inv_cdf`).
**Tests:** `test-financial-tensors.cpp` (18).

### `financial-backend.h/.cpp` — Backend Detection & Selection (Task 2.2)
Runtime CPU ISA detection (SSE/AVX/AVX2/AVX-512/FMA/NEON via CPUID) and a
backend selector that prefers a GPU backend only for large batches, always
falling back to the CPU reference path. **Tests:** `test-financial-backend.cpp`.

### `financial-memory-pool.h/.cpp` — Financial Memory Pool (Task 2.3)
Cache-line-aligned (64B), thread-safe pool allocator with O(1) acquire/release
from a free-list and geometric slab growth. Designed for bounded-latency
market-data and risk buffers. **Tests:** `test-financial-backend.cpp`.

### `flow-analytics.h/.cpp` — Flow Analytics & Entity Resolution (Phase B.1/B.2)
Influent-aligned `AuditEntity`/`AuditTransaction` model, `FlowGraph`
aggregation with time-window filtering, left-to-right flow ranking, fan-out /
fan-in (branch-and-converge) detection, Levenshtein/Jaro/Jaro-Winkler string
similarity, address normalization, Union-Find entity resolution, and
Louvain-style community detection. **Tests:** `test-flow-analytics.cpp` (14).

### `fraud-detection.h/.cpp` — Fraud & Anomaly Detection (Issue #005 Task 5.3 / Phase C.1)
Isolation Forest, Benford's Law first-digit analysis (Nigrini MAD conformity),
autoencoder-style reconstruction-error scorer, Welford online SPC control-chart
monitor, and a composite `RiskScorer` that fuses detectors into a single
alert-thresholded risk score. **Tests:** `test-fraud-detection.cpp` (13).

## Design Notes

- **Standalone by construction.** None of these modules link libllama/libggml.
  Where hardware acceleration applies (tensor kernels), the API is shaped so a
  host application that *does* link GGML can route the same calls to a GPU
  backend via `financial-backend.h`.
- **Reuse over duplication.** Higher layers build on the existing data plane:
  `compliance` consumes `audit-trail` + `transaction-validator`; `flow-analytics`
  and `fraud-detection` consume the transaction model; quant models reuse
  `financial-tensors`.
- **No new external dependencies** anywhere in this directory.
