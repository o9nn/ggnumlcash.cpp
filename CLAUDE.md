# CLAUDE.md - Project Guide for Claude Code

## Project Overview

**GGNuCash** is a high-performance financial computation platform built on the GGML tensor library (derived from llama.cpp). It provides real-time financial modeling, risk analysis, and trading applications with enterprise-grade performance and regulatory compliance.

### Key Facts
- **Primary Language**: C/C++ with Python utility scripts
- **Build System**: CMake (Makefile is deprecated)
- **Core Dependency**: ggml tensor library (vendored in `ggml/` directory)
- **License**: MIT
- **Compliance**: SOX, Basel III, MiFID II, GDPR, POPIA, FIC Act

### Related Ecosystem
- **cogpy/cogflu** - Transaction flow analytics (Influent-based, Java/Apache Avro)
- **cogpy/influent** - Visual analytics for big data transaction flow (JavaScript)
- **cogpy/revstream1** - Revenue stream investigation & evidence management (Python)
- **cogpy/casport** - Case evidence portal with timeline visualization (TypeScript/React)
- **cogpy/CogFin-MA** - Multi-agent RAG framework for financial analysis (Jupyter/Python)
- **cogpy/chainlex** - Legal knowledge graph in Cypher/Neo4j (South African law)
- **cogpy/cogerpnext** - Enterprise resource planning (forked ERPNext)
- **cogpy/beancog** - Double-entry accounting from text files
- **unchartedsoftware/influent** - Transaction flow analytics platform (MIT, DARPA XDATA)
- **unchartedsoftware/ensemble-clustering** - Multi-threaded clustering for heterogeneous data
- **unchartedsoftware/grafer** - Large graph rendering via WebGL
- **unchartedsoftware/salt-core** - Big data tiling library (Scala/Spark)
- **Fincosys** - Business entity under investigation (documented in cogpy/revstream1)

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

# Build and run audit & anti-fraud tests (Phase A)
cmake --build build --target test-audit-trail
cmake --build build --target test-transaction-validator

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
│   ├── issues/             # Feature issue specifications (#001-#015)
│   ├── development-roadmap.md
│   ├── security-compliance.md
│   ├── ggnucash-architecture.md
│   └── project-tracking.md
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

### Implemented Components (Phase 1)
- **Task 1.1** - Extended Chart of Accounts (`enhanced-coa.h`, `account-templates.h`)
- **Task 1.2** - Advanced Transaction Engine (`transaction-engine.h/.cpp`)
- **Task 1.3** - Real-time Financial Reporting (`financial-reporting.h/.cpp`)
- **Task 1.4** - Database Persistence & Recovery (`database-persistence.h/.cpp`)

### Implemented Components (Phase A: Audit Foundation)
- **Task A.1** - Immutable Audit Trail Engine (`audit-trail.h/.cpp`) - 22 tests passing
- **Task A.3** - Transaction Integrity Validator (`transaction-validator.h/.cpp`) - 20 tests passing

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
- Development stages: Embryonic -> Juvenile -> Mature -> Senescent
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

---

## Financial Audit & Anti-Fraud Analysis - Development Roadmap

This roadmap outlines the integration of financial audit features and anti-fraud analysis capabilities into GGNuCash, drawing on patterns and architectures from the cogpy ecosystem (cogflu, influent, revstream1, CogFin-MA, chainlex) and the Uncharted Software visual analytics platform (influent, ensemble-clustering, salt-core, grafer).

### Vision

Build a hardware-accelerated financial audit and anti-fraud platform that combines GGNuCash's high-performance tensor computation with transaction flow visualization, entity resolution, anomaly detection, and regulatory evidence management -- capable of analyzing entities such as Fincosys and similar organizations at scale.

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                    GGNuCash Audit & Anti-Fraud Platform             │
├─────────────────────────────────────────────────────────────────────┤
│  Presentation Layer                                                 │
│  ├── Transaction Flow Visualizer (Influent-style)                  │
│  ├── Entity Relationship Graph (Grafer/WebGL)                      │
│  ├── Evidence Portal & Case Management (casport-style)             │
│  └── Compliance Dashboard & Reporting                              │
├─────────────────────────────────────────────────────────────────────┤
│  Analysis Engine                                                    │
│  ├── Transaction Pattern Detector (GGML tensor ops)                │
│  ├── Entity Resolution & Clustering (ensemble-clustering)          │
│  ├── Anomaly Detection (Isolation Forest, LSTM, Autoencoder)       │
│  ├── Multi-Agent Financial Analyzer (CogFin-MA patterns)           │
│  └── Legal Knowledge Graph (chainlex/Neo4j integration)            │
├─────────────────────────────────────────────────────────────────────┤
│  Data Layer                                                         │
│  ├── Transaction Ingestion (Avro/SPI, FIX protocol)                │
│  ├── Immutable Audit Trail (SHA-256 chain, SOX compliant)          │
│  ├── Evidence Store (indexed, searchable, exportable)              │
│  └── Multi-Source Connector (Xero, ERPNext, GnuCash, Beancount)   │
├─────────────────────────────────────────────────────────────────────┤
│  Infrastructure                                                     │
│  ├── GGML Hardware Acceleration (CPU/CUDA/Metal/Vulkan)            │
│  ├── Big Data Tiling (salt-core patterns for scale)                │
│  └── Distributed Processing (parallel entity analysis)             │
└─────────────────────────────────────────────────────────────────────┘
```

---

### Phase A: Audit Foundation (Weeks 1-6)

#### A.1 Immutable Audit Trail Engine
**Priority**: Critical | **Depends on**: Task 1.4 (Database Persistence) | **Status**: IMPLEMENTED

Implement a cryptographically secured, append-only audit trail for all financial transactions, drawing on SOX compliance patterns from the existing security-compliance framework.

- [x] SHA-256 hash chain for transaction integrity verification
- [x] Cryptographic signing of audit entries with timestamp attestation
- [x] 7-year retention with instant retrieval capability
- [x] Tamper detection with automatic alerting
- [x] Export formats: JSON, CSV for external auditors
- [ ] PDF export for external auditors
- [ ] Integration with existing `database-persistence.h` storage layer

**Implementation**: `examples/financial-sim/audit-trail.h/.cpp` (22 tests passing)
**Reference**: `docs/security-compliance.md` SOX Controls, `cogpy/revstream1` evidence chain patterns

#### A.2 Multi-Source Financial Data Connector
**Priority**: High | **Depends on**: Task 1.1 (Chart of Accounts)

Build connectors to ingest financial data from multiple accounting systems for cross-system audit analysis.

- [ ] Xero API connector (REST, OAuth2) -- for analyzing Xero-based entities
- [ ] GnuCash file reader (XML/SQLite) -- leveraging `cogpy/gnucash-browser` patterns
- [ ] Beancount/hledger text file parser -- leveraging `cogpy/beancog` patterns
- [ ] ERPNext API connector -- leveraging `cogpy/cogerpnext` data models
- [ ] CSV/Excel universal importer with field mapping
- [ ] Apache Avro serialization for cross-language data exchange (cogflu SPI pattern)
- [ ] Data normalization layer mapping external accounts to GGNuCash CoA

**Reference**: `cogpy/cogflu` Service Provider Interface, `cogpy/gnucash-graphql`

#### A.3 Transaction Integrity Validator
**Priority**: High | **Depends on**: A.1, A.2 | **Status**: IMPLEMENTED

Core validation engine for detecting accounting inconsistencies.

- [x] Double-entry balance verification across all accounts
- [ ] Inter-company transaction reconciliation
- [x] Missing transaction gap detection with date-range analysis
- [x] Duplicate transaction identification using fuzzy matching (Levenshtein similarity)
- [ ] Currency conversion audit with rate source verification
- [x] Trial balance validation and automated discrepancy reporting
- [x] Hash chain integrity verification
- [x] Account existence validation
- [x] Amount reasonableness checks (large transaction detection, negative amount detection)
- [x] Comprehensive validation report (text and JSON export)

**Implementation**: `examples/financial-sim/transaction-validator.h/.cpp` (20 tests passing)
**Reference**: `cogpy/revstream1/MR/Xero_Accounts.md`, `examples/financial-sim/enhanced-coa.h`

---

### Phase B: Transaction Flow Analytics (Weeks 7-14)

#### B.1 Transaction Flow Visualization Engine
**Priority**: High | **Depends on**: Phase A

Implement Influent-style transaction flow visualization for following money trails, adapted from `unchartedsoftware/influent` and `cogpy/cogflu`.

- [ ] Left-to-right semantic flow layout for directional money movement
- [ ] Interactive link expansion for progressive entity exploration
- [ ] Hierarchical entity clustering to manage visual complexity at scale
- [ ] Summary visualizations for transactional pattern identification
- [ ] Temporal controls for analyzing transaction flows over time windows
- [ ] Support for millions of entities and hundreds of millions of transactions
- [ ] WebGL rendering using grafer patterns for large graph performance

**Data Model** (aligned with Influent SPI):
```cpp
struct audit_entity {
    std::string entity_id;      // Unique identifier
    std::string entity_name;    // Display name
    std::string entity_type;    // person, company, account, etc.
    std::unordered_map<std::string, std::string> properties;
};

struct audit_transaction {
    std::string transaction_id;
    std::string source_entity;
    std::string dest_entity;
    double      amount;
    std::string currency;
    int64_t     timestamp;
    std::unordered_map<std::string, std::string> metadata;
};
```

**Reference**: `unchartedsoftware/influent` (MIT, DARPA XDATA), `cogpy/cogflu` (Apache-2.0)

#### B.2 Entity Resolution & Clustering
**Priority**: High | **Depends on**: B.1

Resolve entities across disparate data sources and cluster related entities for investigation.

- [ ] Fuzzy name matching across data sources (Levenshtein, Jaro-Winkler)
- [ ] Address normalization and matching
- [ ] Company registration number cross-referencing (CIPC, Companies House)
- [ ] Directorship and beneficial ownership graph construction
- [ ] Louvain community detection for entity clustering (per Influent Bitcoin approach)
- [ ] Hardware-accelerated similarity computation via GGML tensor ops

**Reference**: `unchartedsoftware/ensemble-clustering`, Influent entity resolution patterns

#### B.3 Revenue Stream Analysis
**Priority**: Critical | **Depends on**: B.1, B.2

Specialized analysis module for detecting revenue stream anomalies, diversion, and hijacking patterns.

- [ ] Revenue flow mapping from source to final destination
- [ ] Branch-and-converge detection (classic money laundering indicator)
- [ ] Revenue diversion pattern recognition using tensor-based ML models
- [ ] Comparative period analysis (expected vs. actual revenue flows)
- [ ] Third-party intermediary identification and risk scoring
- [ ] Revenue completeness testing against external data sources

**Reference**: `cogpy/revstream1` Revenue Stream Hijacking analysis patterns

---

### Phase C: Anti-Fraud Detection Engine (Weeks 15-22)

#### C.1 ML-Powered Anomaly Detection
**Priority**: Critical | **Depends on**: Phase B, Issue #005 (ML Integration)

Hardware-accelerated fraud detection using GGML tensor operations for real-time scoring.

- [ ] Isolation Forest for transaction amount/frequency anomalies (GGML kernels)
- [ ] LSTM sequence model for temporal transaction pattern anomalies
- [ ] Autoencoder for behavioral baseline modeling and deviation detection
- [ ] Benford's Law analysis for digit distribution anomalies in financial data
- [ ] Statistical process control (SPC) for continuous transaction monitoring
- [ ] Real-time risk scoring: process 1M+ transactions/hour with <1% false positives
- [ ] Configurable alert thresholds per entity type and jurisdiction

**Performance Targets**:
- Risk score computation: <100us per transaction on GPU
- Batch analysis: 10M+ transactions in <60 seconds
- Model retraining: incremental updates without service interruption

**Reference**: Issue #005 Task 5.3 (Fraud Detection), `cogpy/CogFin-MA` multi-agent patterns

#### C.2 Suspicious Transaction Reporting (STR)
**Priority**: Critical | **Depends on**: C.1

Automated generation and submission of suspicious transaction reports per FIC Act, FinCEN, and international AML requirements.

- [ ] FIC Suspicious Transaction Report generation (South African FIC Act)
- [ ] FinCEN SAR/CTR generation (US Bank Secrecy Act)
- [ ] EU 4th/5th Anti-Money Laundering Directive reporting
- [ ] Automated threshold monitoring (structuring detection)
- [ ] PEP (Politically Exposed Person) screening integration
- [ ] Sanctions list screening (OFAC, EU, UN consolidated lists)
- [ ] Report queue management with escalation workflows

**Reference**: `cogpy/revstream1/FOREKNOWLEDGE_REFINED_FILINGS_2026_02_08/FIC_Suspicious_Transaction_Report`

#### C.3 Forensic Pattern Library
**Priority**: High | **Depends on**: C.1, B.2

Curated library of known fraud patterns for automated detection.

- [ ] Round-tripping / circular transaction detection
- [ ] Layering detection (rapid movement through multiple accounts)
- [ ] Ghost employee/vendor identification
- [ ] Invoice fraud patterns (duplicate, inflated, fictitious)
- [ ] Payroll fraud detection (overtime anomalies, phantom employees)
- [ ] Procurement fraud (bid rigging indicators, vendor collusion)
- [ ] Journal entry fraud (unusual timing, amounts, accounts)
- [ ] Related-party transaction monitoring with undisclosed relationship detection

---

### Phase D: Legal & Evidence Management (Weeks 23-28)

#### D.1 Case Management System
**Priority**: Medium | **Depends on**: Phase C

Investigation case management for audit findings, modeled on `cogpy/casport` evidence portal.

- [ ] Case creation with structured evidence linking
- [ ] Timeline visualization for event sequencing
- [ ] Entity explorer with relationship mapping
- [ ] Evidence indexing with full-text search (Annexure-style: JF01-JF13)
- [ ] Chain of custody tracking for digital evidence
- [ ] Collaboration features for multi-investigator cases
- [ ] Export for legal proceedings (PDF reports with evidence references)

**Reference**: `cogpy/casport` (TypeScript/React), `cogpy/ad-res-j7` legal framework

#### D.2 Legal Knowledge Graph Integration
**Priority**: Medium | **Depends on**: D.1

Integrate legal reasoning capabilities using graph-based legal knowledge, adapted from `cogpy/chainlex`.

- [ ] Neo4j/graph database integration for legal entity relationships
- [ ] South African law framework (Companies Act, FIC Act, POPIA, Tax Administration Act)
- [ ] International regulatory mapping (SOX, MiFID II, Basel III, GDPR)
- [ ] Automated legal citation and precedent lookup
- [ ] Burden of proof assessment (criminal 95% / civil 50%+ thresholds)
- [ ] Multi-level inference: deductive, inductive, abductive, analogical reasoning

**Reference**: `cogpy/chainlex` (Cypher/Neo4j, 8 legal branches, 60+ legal maxims)

#### D.3 Regulatory Filing Automation
**Priority**: High | **Depends on**: D.1, D.2

Automated generation of regulatory filings and complaints.

- [ ] CIPC Complaint generation (South African Companies & IP Commission)
- [ ] POPIA Complaint generation (Protection of Personal Information Act)
- [ ] NPA Tax Fraud Report generation (National Prosecuting Authority)
- [ ] Commercial Crime submission templates
- [ ] Professional Misconduct Complaint generation
- [ ] Filing status tracking and deadline management
- [ ] Regulatory body submission API integration where available

**Reference**: `cogpy/revstream1/FOREKNOWLEDGE_REFINED_FILINGS_2026_02_08/` filing templates

---

### Phase E: Multi-Agent Financial Intelligence (Weeks 29-34)

#### E.1 RAG-Based Financial Analyst Agents
**Priority**: Medium | **Depends on**: Phase C, Phase D

Multi-agent system for autonomous financial analysis, adapted from `cogpy/CogFin-MA` (FIND-MA framework).

- [ ] Sentiment analysis agent for news and filings
- [ ] Financial health assessment agent (ratio analysis, trend detection)
- [ ] Correlation analysis agent (inter-entity transaction pattern correlation)
- [ ] Risk profiling agent (SWOT analysis, strategic risk assessment)
- [ ] FAISS-based dense vector retriever for financial document search
- [ ] Shared memory store for inter-agent communication and evidence fusion
- [ ] LLM integration for natural language audit report generation

**Reference**: `cogpy/CogFin-MA` (FIND-MA architecture, DeepSeek-R1, Qwen3, FAISS)

#### E.2 Fincosys-Class Entity Analyzer
**Priority**: High | **Depends on**: E.1, B.2

Purpose-built analysis pipeline for investigating complex corporate entities and their financial relationships.

- [ ] Corporate structure decomposition (subsidiaries, SPVs, shell companies)
- [ ] Directorship network analysis with conflict-of-interest detection
- [ ] Bank account relationship mapping across institutions
- [ ] Transaction volume and pattern profiling per entity
- [ ] Tax compliance analysis (VAT, income tax, payroll tax)
- [ ] Regulatory filing history analysis and gap detection
- [ ] Automated entity risk scoring based on composite indicators
- [ ] Cross-referencing against `revstream1`-style evidence databases

**Target Entities**: Organizations similar to Fincosys -- companies with complex multi-entity structures, multiple revenue streams, and cross-border transaction patterns that require deep forensic analysis.

#### E.3 Continuous Monitoring & Alerting
**Priority**: High | **Depends on**: E.1, C.1

Real-time monitoring system for ongoing surveillance of entities under investigation.

- [ ] Configurable watchlists with entity-specific monitoring rules
- [ ] Real-time transaction feed monitoring with ML scoring
- [ ] Threshold-based alerts (amount, frequency, counterparty changes)
- [ ] Behavioral drift detection (deviation from established patterns)
- [ ] Escalation workflows with SLA tracking
- [ ] Integration with case management for automatic evidence capture
- [ ] Dashboard: monitored entities, alert volume, resolution metrics

---

### Phase F: Visualization & Reporting Platform (Weeks 35-40)

#### F.1 Big Data Transaction Tile Server
**Priority**: Medium | **Depends on**: Phase B

Scalable visualization backend for billions of transaction data points, adapted from `unchartedsoftware/salt-core`.

- [ ] Tile pyramid generation for transaction heat maps
- [ ] Cartesian and temporal projections for financial data
- [ ] Aggregators: sum, count, mean, min/max for transaction metrics
- [ ] Multi-level zoom for drill-down from portfolio to individual transactions
- [ ] Apache Spark integration for distributed tile computation
- [ ] WebGL rendering via lumo/grafer for client-side performance

**Reference**: `unchartedsoftware/salt-core` (Scala/Spark), `unchartedsoftware/lumo`

#### F.2 Audit Report Generator
**Priority**: High | **Depends on**: Phase D

Comprehensive audit report generation for internal and external stakeholders.

- [ ] Executive summary with key findings and risk indicators
- [ ] Detailed transaction flow diagrams with annotations
- [ ] Entity relationship maps with risk scoring overlays
- [ ] Timeline narratives with evidence cross-references
- [ ] Regulatory-specific report formats (SOX, Basel III, MiFID II)
- [ ] South African regulatory formats (FIC, CIPC, SARS, POPIA)
- [ ] PDF export with digital signatures and tamper-evident seals
- [ ] Interactive HTML report with embedded visualizations

#### F.3 Compliance Scorecard & Dashboard
**Priority**: Medium | **Depends on**: F.2

Real-time compliance monitoring dashboard.

- [ ] Entity-level compliance scorecards
- [ ] Jurisdiction-specific compliance status views
- [ ] Trend analysis for compliance posture over time
- [ ] Drill-down from scorecard to specific violations and evidence
- [ ] Benchmark comparisons against industry standards
- [ ] Automated compliance status notifications to stakeholders

---

### Integration Matrix: External Repos -> GGNuCash Components

| External Source | GGNuCash Target | Integration Type |
|---|---|---|
| `cogpy/cogflu` (Influent SPI/Avro) | Transaction Flow Visualizer (B.1) | Data model, SPI architecture |
| `cogpy/influent` (JS client) | Frontend visualization layer (F.1) | UI patterns, interaction design |
| `cogpy/revstream1` (evidence data) | Case Management (D.1), Filing (D.3) | Evidence structures, filing templates |
| `cogpy/casport` (React portal) | Case Management UI (D.1) | Component architecture, data models |
| `cogpy/CogFin-MA` (multi-agent) | Agent Analyzer (E.1) | Agent patterns, RAG architecture |
| `cogpy/chainlex` (legal graph) | Legal Knowledge Graph (D.2) | Graph schema, inference patterns |
| `cogpy/beancog` (accounting) | Data Connector (A.2) | File format parsers |
| `cogpy/cogerpnext` (ERP) | Data Connector (A.2) | API integration patterns |
| `unchartedsoftware/influent` | Transaction Flow Engine (B.1) | Core analytics engine, data model |
| `unchartedsoftware/ensemble-clustering` | Entity Clustering (B.2) | Clustering algorithms |
| `unchartedsoftware/salt-core` | Tile Server (F.1) | Tiling architecture, Spark jobs |
| `unchartedsoftware/grafer` | Graph Rendering (B.1, F.1) | WebGL rendering pipeline |

### Performance Targets for Audit & Anti-Fraud

| Metric | Target | Hardware |
|---|---|---|
| Transaction anomaly scoring | <100us per transaction | GPU (CUDA/Metal) |
| Batch fraud analysis (10M txns) | <60 seconds | Multi-GPU |
| Entity resolution (1M entities) | <30 seconds | CPU AVX-512 |
| Flow visualization (100K nodes) | 60fps interactive | WebGL |
| Audit trail write | <10us per entry | NVMe SSD |
| STR report generation | <5 seconds | CPU |
| Full entity risk profile | <10 seconds | GPU + CPU |

### South African Regulatory Integration

Given the cogpy ecosystem's focus on South African jurisdiction:

- **FIC Act (Financial Intelligence Centre)**: Suspicious transaction reporting, cash threshold reporting
- **POPIA (Protection of Personal Information Act)**: Data subject rights, breach notification
- **Companies Act (CIPC)**: Company registration verification, directorship monitoring
- **Tax Administration Act (SARS)**: Tax fraud indicators, VAT anomaly detection
- **Prevention of Organised Crime Act (POCA)**: Asset forfeiture, money laundering indicators
- **National Prosecuting Authority Act**: Criminal referral documentation

---

## Existing Development Roadmap (Feature Issues #001-#015)

The full platform roadmap is documented in `docs/development-roadmap.md` with detailed feature issues in `docs/issues/`. Current status:

| Phase | Issues | Status |
|---|---|---|
| Phase 1: Foundation | #001 Financial Core, #002 Hardware Accel, #003 Market Data | In Progress (25%) - Tasks 1.1-1.4 Complete |
| Phase A: Audit Foundation | A.1 Audit Trail, A.2 Data Connectors, A.3 Integrity Validator | In Progress (50%) - A.1, A.3 Complete |
| Phase 2: Financial Models | #004 Quant Models, #005 ML Integration, #006 Compliance | Not Started |
| Phase B: Transaction Flow | B.1 Flow Viz, B.2 Entity Resolution, B.3 Revenue Analysis | Not Started |
| Phase 3: Trading Platform | #007 HFT Engine, #008 Cross-Asset, #009 Algo Strategies | Not Started |
| Phase C: Anti-Fraud | C.1 ML Anomaly, C.2 STR Reporting, C.3 Forensic Patterns | Not Started |
| Phase 4: Enterprise | #010 Cloud-Native, #011 API Platform, #012 Security Hardening | Not Started |
| Phase D: Legal/Evidence | D.1 Case Management, D.2 Legal Graph, D.3 Filing Automation | Not Started |
| Phase 5: Innovation | #013 Quantum, #014 Blockchain/DeFi, #015 ESG | Not Started |
| Phase E: Multi-Agent Intel | E.1 RAG Agents, E.2 Entity Analyzer, E.3 Monitoring | Not Started |
| Phase F: Visualization | F.1 Tile Server, F.2 Report Generator, F.3 Dashboard | Not Started |

The audit & anti-fraud roadmap (Phases A-F above) integrates with:
- **Issue #005 Task 5.3** (Fraud Detection & Anomaly Detection) -> Phase C
- **Issue #006** (Regulatory Compliance Engine) -> Phases A, D
- **Issue #012** (Security & Compliance Hardening) -> Phases A, D

## Documentation Links

- [Build Guide](docs/build.md)
- [Architecture](docs/ggnucash-architecture.md)
- [Financial Hardware](docs/financial-hardware-implementation.md)
- [Development Roadmap](docs/development-roadmap.md)
- [Security & Compliance](docs/security-compliance.md)
- [Project Tracking](docs/project-tracking.md)
- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)
