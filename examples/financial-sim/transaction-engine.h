#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstring>

// Forward declarations
struct TransactionEntry;
struct Transaction;

// ============================================================================
// Cryptographic Hashing for Audit Trail
// ============================================================================

// SHA-256 Implementation (simplified for audit trail)
class SHA256 {
public:
    static std::string hash(const std::string& input);
    static std::string hash_transaction(const Transaction& tx);
    
private:
    static const uint32_t K[64];
    static void transform(uint32_t state[8], const uint8_t block[64]);
    static uint32_t rotr(uint32_t x, uint32_t n);
    static uint32_t choose(uint32_t e, uint32_t f, uint32_t g);
    static uint32_t majority(uint32_t a, uint32_t b, uint32_t c);
    static uint32_t sig0(uint32_t x);
    static uint32_t sig1(uint32_t x);
    static uint32_t sigma0(uint32_t x);
    static uint32_t sigma1(uint32_t x);
};

// Constants
namespace TransactionConstants {
    constexpr double BALANCE_TOLERANCE = 0.01; // Tolerance for floating point comparison
}

// ============================================================================
// Transaction Entry - represents signal flow between nodes
// ============================================================================

struct TransactionEntry {
    std::string account_code;   // Target node
    double debit_amount;        // Positive signal flow
    double credit_amount;       // Negative signal flow
    std::string description;    // Signal description
    
    TransactionEntry() : account_code(""), debit_amount(0.0), credit_amount(0.0), description("") {}
    
    TransactionEntry(const std::string& code, double debit, double credit, const std::string& desc = "")
        : account_code(code), debit_amount(debit), credit_amount(credit), description(desc) {}
};

// ============================================================================
// Transaction - represents a complete signal routing operation
// ============================================================================

struct Transaction {
    std::string id;
    std::string description;
    std::vector<TransactionEntry> entries;
    std::string timestamp;
    std::string hash;           // Cryptographic hash for audit trail
    std::string prev_hash;      // Previous transaction hash (blockchain-style)
    
    // Transaction metadata
    std::string template_id;    // If created from template
    bool is_recurring;          // Is this a recurring transaction
    std::string recurrence_id;  // ID of the recurrence schedule
    
    Transaction() : id(""), description(""), timestamp(""), hash(""), prev_hash(""),
                   template_id(""), is_recurring(false), recurrence_id("") {}
    
    // Validate double-entry (conservation law)
    bool is_balanced() const {
        double total_debits = 0.0, total_credits = 0.0;
        for (const auto& entry : entries) {
            total_debits += entry.debit_amount;
            total_credits += entry.credit_amount;
        }
        return std::abs(total_debits - total_credits) < TransactionConstants::BALANCE_TOLERANCE;
    }
    
    // Generate timestamp
    void generate_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
        timestamp = ss.str();
    }
    
    // Calculate hash of this transaction
    void calculate_hash() {
        hash = SHA256::hash_transaction(*this);
    }
};

// ============================================================================
// Transaction Template
// ============================================================================

struct TransactionTemplate {
    std::string id;
    std::string name;
    std::string description;
    std::vector<TransactionEntry> entry_template;
    std::map<std::string, std::string> placeholders; // For variable values
    
    TransactionTemplate() : id(""), name(""), description("") {}
    
    // Create a transaction from this template
    Transaction instantiate(const std::map<std::string, double>& values) const;
};

// ============================================================================
// Recurrence Schedule
// ============================================================================

enum class RecurrenceFrequency {
    DAILY,
    WEEKLY,
    BIWEEKLY,
    MONTHLY,
    QUARTERLY,
    ANNUALLY
};

struct RecurrenceSchedule {
    std::string id;
    std::string template_id;
    RecurrenceFrequency frequency;
    std::chrono::system_clock::time_point start_date;
    std::chrono::system_clock::time_point end_date;
    std::chrono::system_clock::time_point next_occurrence;
    std::map<std::string, double> template_values;
    bool is_active;
    int execution_count;
    
    RecurrenceSchedule() : id(""), template_id(""), frequency(RecurrenceFrequency::MONTHLY),
                          is_active(true), execution_count(0) {
        auto now = std::chrono::system_clock::now();
        start_date = now;
        end_date = now + std::chrono::hours(24 * 365); // Default 1 year
        next_occurrence = now;
    }
    
    // Calculate next occurrence date
    void calculate_next_occurrence();
    
    // Check if should execute now
    bool should_execute_now() const;
};

// ============================================================================
// Batch Transaction Processor
// ============================================================================

struct BatchTransaction {
    std::string batch_id;
    std::vector<Transaction> transactions;
    std::chrono::system_clock::time_point submitted_time;
    std::atomic<bool> completed;
    std::atomic<bool> has_errors;
    std::string error_message;
    std::vector<size_t> failed_indices; // Indices of failed transactions
    
    BatchTransaction() : batch_id(""), completed(false), has_errors(false), error_message("") {
        submitted_time = std::chrono::system_clock::now();
    }
};

// ============================================================================
// Transaction Audit Trail (Blockchain-style)
// ============================================================================

struct AuditBlock {
    std::string block_hash;
    std::string previous_block_hash;
    std::vector<Transaction> transactions;
    std::chrono::system_clock::time_point timestamp;
    uint64_t block_number;
    
    AuditBlock() : block_hash(""), previous_block_hash(""), block_number(0) {
        timestamp = std::chrono::system_clock::now();
    }
    
    // Calculate block hash
    void calculate_block_hash();
    
    // Verify block integrity
    bool verify_integrity() const;
};

class AuditTrail {
private:
    std::vector<AuditBlock> blocks;
    std::string genesis_hash;
    std::mutex audit_mutex;
    
public:
    AuditTrail();
    
    // Add transactions to audit trail
    void add_transactions(const std::vector<Transaction>& transactions);
    
    // Verify entire audit trail
    bool verify_trail() const;
    
    // Get block by number
    const AuditBlock* get_block(uint64_t block_number) const;
    
    // Get all transactions
    std::vector<Transaction> get_all_transactions() const;
    
    // Export audit trail to string
    std::string export_trail() const;
    
    size_t get_block_count() const { return blocks.size(); }
    size_t get_transaction_count() const;
};

// ============================================================================
// Advanced Transaction Engine
// ============================================================================

class TransactionEngine {
private:
    // Transaction processing
    std::queue<std::shared_ptr<BatchTransaction>> batch_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::vector<std::thread> worker_threads;
    std::atomic<bool> is_running;
    std::atomic<uint64_t> transactions_processed;
    
    // Templates and recurrence
    std::map<std::string, TransactionTemplate> templates;
    std::map<std::string, RecurrenceSchedule> recurrence_schedules;
    std::mutex template_mutex;
    std::mutex recurrence_mutex;
    
    // Audit trail
    std::unique_ptr<AuditTrail> audit_trail;
    
    // Performance metrics
    std::atomic<uint64_t> total_batches_processed;
    std::atomic<uint64_t> total_transactions_failed;
    std::chrono::steady_clock::time_point start_time;
    
    // Thread pool size
    size_t num_workers;
    
    // Transaction processor callback
    std::function<bool(const Transaction&)> transaction_processor;
    
    // Worker thread function
    void worker_thread_func();
    
public:
    TransactionEngine(size_t workers = 4);
    ~TransactionEngine();
    
    // Start/Stop engine
    void start();
    void stop();
    bool is_engine_running() const { return is_running.load(); }
    
    // Set transaction processor callback
    void set_transaction_processor(std::function<bool(const Transaction&)> processor);
    
    // Batch processing
    std::string submit_batch(const std::vector<Transaction>& transactions);
    std::shared_ptr<BatchTransaction> get_batch_status(const std::string& batch_id);
    void wait_for_batch(const std::string& batch_id);
    
    // Template management
    bool add_template(const TransactionTemplate& tmpl);
    bool remove_template(const std::string& template_id);
    const TransactionTemplate* get_template(const std::string& template_id) const;
    std::vector<std::string> list_templates() const;
    
    // Recurring transactions
    bool add_recurrence(const RecurrenceSchedule& schedule);
    bool remove_recurrence(const std::string& recurrence_id);
    bool update_recurrence(const std::string& recurrence_id, const RecurrenceSchedule& schedule);
    std::vector<std::string> list_recurrences() const;
    void process_due_recurrences(); // Check and process due recurring transactions
    
    // Audit trail
    const AuditTrail& get_audit_trail() const { return *audit_trail; }
    bool verify_audit_trail() const;
    std::string export_audit_trail() const;
    
    // Performance metrics
    uint64_t get_transactions_processed() const { return transactions_processed.load(); }
    uint64_t get_batches_processed() const { return total_batches_processed.load(); }
    uint64_t get_transactions_failed() const { return total_transactions_failed.load(); }
    double get_transactions_per_second() const;
    std::string get_performance_report() const;
    
private:
    std::map<std::string, std::shared_ptr<BatchTransaction>> batch_status_map;
    std::mutex batch_status_mutex;
};

// ============================================================================
// Multi-leg Transaction Support
// ============================================================================

class MultiLegTransaction {
public:
    enum class LegType {
        SIMPLE,         // Single entry
        DERIVATIVE,     // Options, futures, swaps
        FX_SWAP,        // Foreign exchange swap
        INTEREST_RATE_SWAP,
        EQUITY_SWAP
    };
    
    struct Leg {
        std::string leg_id;
        LegType type;
        std::vector<TransactionEntry> entries;
        std::string currency;
        double notional_amount;
        std::chrono::system_clock::time_point settlement_date;
        std::map<std::string, std::string> metadata;
        
        Leg() : leg_id(""), type(LegType::SIMPLE), currency("USD"), notional_amount(0.0) {
            settlement_date = std::chrono::system_clock::now();
        }
    };
    
private:
    std::string transaction_id;
    std::string description;
    std::vector<Leg> legs;
    std::string primary_currency;
    
public:
    MultiLegTransaction() : transaction_id(""), description(""), primary_currency("USD") {}
    
    void add_leg(const Leg& leg);
    void remove_leg(const std::string& leg_id);
    
    // Convert to standard transaction
    Transaction to_transaction() const;
    
    // Validate multi-leg transaction
    bool is_valid() const;
    
    // Create FX Swap transaction
    static MultiLegTransaction create_fx_swap(
        const std::string& from_currency,
        const std::string& to_currency,
        double spot_amount,
        double forward_amount,
        const std::chrono::system_clock::time_point& spot_date,
        const std::chrono::system_clock::time_point& forward_date
    );
    
    // Create Interest Rate Swap
    static MultiLegTransaction create_interest_rate_swap(
        double notional,
        double fixed_rate,
        const std::string& floating_rate_index,
        const std::chrono::system_clock::time_point& start_date,
        const std::chrono::system_clock::time_point& end_date
    );
    
    const std::vector<Leg>& get_legs() const { return legs; }
    void set_transaction_id(const std::string& id) { transaction_id = id; }
    void set_description(const std::string& desc) { description = desc; }
};

// ============================================================================
// Utility Functions
// ============================================================================

namespace TransactionUtils {
    // Generate unique transaction ID
    std::string generate_transaction_id(const std::string& prefix = "TX");
    
    // Generate batch ID
    std::string generate_batch_id();
    
    // Validate transaction entries
    bool validate_transaction(const Transaction& tx);
    
    // Create sample templates
    std::vector<TransactionTemplate> create_sample_templates();
    
    // Performance testing
    std::vector<Transaction> generate_test_transactions(size_t count);
}
