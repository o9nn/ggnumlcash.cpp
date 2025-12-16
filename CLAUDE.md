# CLAUDE.md - Project Guide for Claude Code

## Project Overview

**GGNuCash** is a high-performance financial computation platform built on the GGML tensor library (derived from llama.cpp). It provides real-time financial modeling, risk analysis, and trading applications with enterprise-grade performance and regulatory compliance.

### Key Facts
- **Primary Language**: C/C++ with Python utility scripts
- **Build System**: CMake (Makefile is deprecated)
- **Core Dependency**: ggml tensor library (vendored in `ggml/` directory)
- **License**: MIT
- **Compliance**: SOX, Basel III, MiFID II, GDPR

## Build Instructions

### Prerequisites
- CMake 3.14+
- C++17 compatible compiler (GCC 13.3+, Clang, MSVC)
- Optional: ccache (automatically detected and used)

### Standard Build (CPU-only)
```bash
cmake -B build
cmake --build build --config Release -j $(nproc)
```

Built binaries are placed in `build/bin/`.

### Backend-Specific Builds
```bash
# CUDA (NVIDIA GPUs)
cmake -B build -DGGML_CUDA=ON
cmake --build build --config Release -j $(nproc)

# Metal (macOS)
cmake -B build -DGGML_METAL=ON
cmake --build build --config Release -j $(nproc)

# Vulkan
cmake -B build -DGGML_VULKAN=ON
cmake --build build --config Release -j $(nproc)
```

### Debug Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Testing

### Run Full Test Suite
```bash
ctest --test-dir build --output-on-failure -j $(nproc)
```

### Server Unit Tests
```bash
cmake --build build --target llama-server
cd tools/server/tests
source ../../../.venv/bin/activate
./tests.sh
```

### Financial Module Tests
```bash
# Build and run financial simulation tests
cmake --build build --target test-enhanced-coa
cmake --build build --target test-transaction-engine
cmake --build build --target test-financial-reporting
cmake --build build --target test-database-persistence

# Run demos
./build/bin/demo-financial-sim
./build/bin/llama-ontogenesis
```

## Code Formatting and Linting

### C++ Code
**Always format before committing:**
```bash
git clang-format
```

Key style rules (from `.clang-format`):
- 4-space indentation
- 120 column limit
- Braces on same line for functions
- Pointer alignment: `void * ptr` (middle)
- Reference alignment: `int & a` (middle)

### Python Code
```bash
source .venv/bin/activate
# Then use tools from the virtual environment
```

### Pre-commit Hooks
```bash
pre-commit run --all-files
```

## Project Structure

```
├── src/                    # Main llama library implementation
│   ├── llama.cpp           # Core library (~14k lines)
│   ├── llama-*.cpp/.h      # Modular components
│   └── llama-ontogenesis.cpp  # Self-generating kernels
├── include/                # Public API headers
│   └── llama.h             # Main C API header
├── ggml/                   # Core tensor library (vendored)
├── examples/               # Example applications
│   ├── financial-sim/      # Financial simulation module
│   ├── ontogenesis/        # Self-generating kernel demos
│   └── ...
├── tools/                  # CLI tools and server
│   ├── main/               # llama-cli
│   ├── server/             # llama-server (OpenAI-compatible)
│   └── ...
├── tests/                  # Test suite (CTest)
├── common/                 # Shared utility code
├── docs/                   # Documentation
├── scripts/                # Utility scripts
└── .github/                # CI workflows and agents
```

## Key Executables (in `build/bin/`)

- `llama-cli` - Main inference tool
- `llama-server` - OpenAI-compatible HTTP server
- `llama-bench` - Performance benchmarking
- `llama-ontogenesis` - Self-generating kernel demos
- `llama-financial-sim` - Financial simulation demo
- `llama-quantize` - Model quantization utility
- `llama-perplexity` - Model evaluation tool

## Coding Guidelines

### Style
- Use `snake_case` for functions, variables, and types
- Prefer basic `for` loops over fancy STL constructs
- Use sized integer types (`int32_t`) in public API
- Declare structs as `struct foo {}` not `typedef struct foo {} foo`
- Vertical alignment for readability

### Naming Pattern
```cpp
// Pattern: <class>_<method> with <method> being <action>_<noun>
llama_model_init();           // class: "llama_model", method: "init"
llama_sampler_get_seed();     // class: "llama_sampler", method: "get_seed"
```

### Enum Values
```cpp
enum llama_vocab_type {
    LLAMA_VOCAB_TYPE_NONE = 0,
    LLAMA_VOCAB_TYPE_SPM  = 1,
    // ...
};
```

## Financial Module Architecture

### Chart of Accounts
- Header-only design in `examples/financial-sim/`
- `chart-of-accounts.h` - Core structures
- `enhanced-coa.h` - Main implementation
- `account-templates.h` - Business templates

### Performance Targets
- Sub-microsecond account lookups
- 10,000+ accounts with <20ms total processing
- 161K+ transactions per second
- Real-time reporting with <1ms latency

### Multi-Currency Support
- 11 currencies: USD, EUR, GBP, JPY, CAD, AUD, CHF, CNY, INR, BRL, MXN
- ExchangeRateManager with division-by-zero protection

## Ontogenesis (Self-Generating Kernels)

Revolutionary system for self-generating, evolving computational kernels:
- B-series expansion as genetic code
- Differential operators for reproduction (chain, product, quotient rules)
- Development stages: Embryonic → Juvenile → Mature → Senescent
- Tournament selection and genetic algorithms

```bash
./build/bin/llama-ontogenesis      # Run all demos
./build/bin/llama-ontogenesis 1    # Self-generation
./build/bin/llama-ontogenesis 4    # Multi-generation evolution
./build/bin/test-ontogenesis       # Run tests
```

## CI/CD

### GitHub Actions Workflows
- `.github/workflows/build.yml` - Multi-platform builds
- `.github/workflows/server.yml` - Server tests
- `.github/workflows/python-lint.yml` - Python linting
- `.github/workflows/python-type-check.yml` - Type checking

### Local CI Validation
```bash
mkdir tmp
bash ./ci/run.sh ./tmp/results ./tmp/mnt
```

Add `ggml-ci` to commit message to trigger heavy CI workloads.

## Important Notes

### Dependencies
- Avoid adding new external dependencies
- System: OpenMP, libcurl (for model downloading)
- Optional: CUDA SDK, Metal framework, Vulkan SDK
- Bundled: httplib, json (header-only)

### Thread Safety
- Use RAII-style scoped locks instead of manual lock/unlock
- Critical for financial transaction processing

### Git Workflow
- Never commit build artifacts (`build/`, `*.o`, `*.gguf`)
- Create feature branches from `master`
- Use descriptive commit messages

### API Stability
- Changes to `include/llama.h` require careful consideration
- This is a performance-critical inference library

## Quick Reference

```bash
# Build
cmake -B build && cmake --build build -j $(nproc)

# Test
ctest --test-dir build --output-on-failure

# Format
git clang-format

# Run financial demo
./build/bin/llama-financial-sim --demo

# Run ontogenesis
./build/bin/llama-ontogenesis

# Benchmark
./build/bin/llama-bench -m model.gguf
```

## Documentation Links

- [Build Guide](docs/build.md)
- [Architecture](docs/ggnucash-architecture.md)
- [Financial Hardware](docs/financial-hardware-implementation.md)
- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)
