#include "transaction-engine.h"
#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

// ============================================================================
// SHA-256 Implementation
// ============================================================================

const uint32_t SHA256::K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

uint32_t SHA256::rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

uint32_t SHA256::choose(uint32_t e, uint32_t f, uint32_t g) {
    return (e & f) ^ (~e & g);
}

uint32_t SHA256::majority(uint32_t a, uint32_t b, uint32_t c) {
    return (a & b) ^ (a & c) ^ (b & c);
}

uint32_t SHA256::sig0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

uint32_t SHA256::sig1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

uint32_t SHA256::sigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

uint32_t SHA256::sigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

void SHA256::transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    
    // Prepare message schedule
    for (int i = 0; i < 16; i++) {
        W[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) | 
               (block[i * 4 + 2] << 8) | block[i * 4 + 3];
    }
    
    for (int i = 16; i < 64; i++) {
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
    }
    
    // Initialize working variables
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    
    // Main loop
    for (int i = 0; i < 64; i++) {
        t1 = h + sig1(e) + choose(e, f, g) + K[i] + W[i];
        t2 = sig0(a) + majority(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    
    // Update state
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

std::string SHA256::hash(const std::string& input) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    
    uint64_t bit_len = input.length() * 8;
    size_t new_len = input.length() + 1;
    while (new_len % 64 != 56) new_len++;
    
    std::vector<uint8_t> padded(new_len + 8);
    std::memcpy(padded.data(), input.data(), input.length());
    padded[input.length()] = 0x80;
    
    for (int i = 0; i < 8; i++) {
        padded[new_len + 7 - i] = (bit_len >> (i * 8)) & 0xff;
    }
    
    for (size_t i = 0; i < padded.size(); i += 64) {
        transform(state, &padded[i]);
    }
    
    std::stringstream ss;
    for (int i = 0; i < 8; i++) {
        ss << std::hex << std::setfill('0') << std::setw(8) << state[i];
    }
    
    return ss.str();
}

std::string SHA256::hash_transaction(const Transaction& tx) {
    std::stringstream ss;
    ss << tx.id << "|" << tx.description << "|" << tx.timestamp << "|" << tx.prev_hash << "|";
    
    for (const auto& entry : tx.entries) {
        ss << entry.account_code << ":" 
           << std::fixed << std::setprecision(2) << entry.debit_amount << ":"
           << std::fixed << std::setprecision(2) << entry.credit_amount << ":"
           << entry.description << "|";
    }
    
    return hash(ss.str());
}

// ============================================================================
// Transaction Template Implementation
// ============================================================================

Transaction TransactionTemplate::instantiate(const std::map<std::string, double>& values) const {
    Transaction tx;
    tx.template_id = id;
    tx.description = description;
    tx.generate_timestamp();
    tx.id = TransactionUtils::generate_transaction_id("TMPL");
    
    // Copy template entries and substitute values
    for (const auto& entry_tmpl : entry_template) {
        TransactionEntry entry = entry_tmpl;
        
        // For simplicity, use "amount" as the default placeholder
        // If template has debit=1.0 or credit=1.0, substitute with the value
        auto amount_it = values.find("amount");
        if (amount_it != values.end()) {
            double amount = amount_it->second;
            if (entry_tmpl.debit_amount == 1.0) {
                entry.debit_amount = amount;
            }
            if (entry_tmpl.credit_amount == 1.0) {
                entry.credit_amount = amount;
            }
        }
        
        tx.entries.push_back(entry);
    }
    
    return tx;
}

// ============================================================================
// Recurrence Schedule Implementation
// ============================================================================

void RecurrenceSchedule::calculate_next_occurrence() {
    auto now = std::chrono::system_clock::now();
    
    switch (frequency) {
        case RecurrenceFrequency::DAILY:
            next_occurrence = now + std::chrono::hours(24);
            break;
        case RecurrenceFrequency::WEEKLY:
            next_occurrence = now + std::chrono::hours(24 * 7);
            break;
        case RecurrenceFrequency::BIWEEKLY:
            next_occurrence = now + std::chrono::hours(24 * 14);
            break;
        case RecurrenceFrequency::MONTHLY:
            next_occurrence = now + std::chrono::hours(24 * 30); // Approximate
            break;
        case RecurrenceFrequency::QUARTERLY:
            next_occurrence = now + std::chrono::hours(24 * 90); // Approximate
            break;
        case RecurrenceFrequency::ANNUALLY:
            next_occurrence = now + std::chrono::hours(24 * 365);
            break;
    }
}

bool RecurrenceSchedule::should_execute_now() const {
    auto now = std::chrono::system_clock::now();
    return is_active && (now >= next_occurrence) && (now <= end_date);
}

// ============================================================================
// Audit Block Implementation
// ============================================================================

void AuditBlock::calculate_block_hash() {
    std::stringstream ss;
    ss << block_number << "|" << previous_block_hash << "|";
    
    for (const auto& tx : transactions) {
        ss << tx.hash << "|";
    }
    
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    ss << time_t;
    
    block_hash = SHA256::hash(ss.str());
}

bool AuditBlock::verify_integrity() const {
    std::stringstream ss;
    ss << block_number << "|" << previous_block_hash << "|";
    
    for (const auto& tx : transactions) {
        ss << tx.hash << "|";
    }
    
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    ss << time_t;
    
    std::string computed_hash = SHA256::hash(ss.str());
    return computed_hash == block_hash;
}

// ============================================================================
// Audit Trail Implementation
// ============================================================================

AuditTrail::AuditTrail() {
    genesis_hash = SHA256::hash("GENESIS_BLOCK_GGNUCASH_2024");
}

void AuditTrail::add_transactions(const std::vector<Transaction>& transactions) {
    std::lock_guard<std::mutex> lock(audit_mutex);
    
    AuditBlock block;
    block.block_number = blocks.size();
    block.previous_block_hash = blocks.empty() ? genesis_hash : blocks.back().block_hash;
    block.transactions = transactions;
    block.timestamp = std::chrono::system_clock::now();
    block.calculate_block_hash();
    
    blocks.push_back(block);
}

bool AuditTrail::verify_trail() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(audit_mutex));
    
    if (blocks.empty()) return true;
    
    // Verify first block
    if (blocks[0].previous_block_hash != genesis_hash) return false;
    if (!blocks[0].verify_integrity()) return false;
    
    // Verify chain
    for (size_t i = 1; i < blocks.size(); i++) {
        if (blocks[i].previous_block_hash != blocks[i - 1].block_hash) return false;
        if (!blocks[i].verify_integrity()) return false;
    }
    
    return true;
}

const AuditBlock* AuditTrail::get_block(uint64_t block_number) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(audit_mutex));
    
    if (block_number >= blocks.size()) return nullptr;
    return &blocks[block_number];
}

std::vector<Transaction> AuditTrail::get_all_transactions() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(audit_mutex));
    
    std::vector<Transaction> all_txs;
    for (const auto& block : blocks) {
        all_txs.insert(all_txs.end(), block.transactions.begin(), block.transactions.end());
    }
    return all_txs;
}

std::string AuditTrail::export_trail() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(audit_mutex));
    
    std::stringstream ss;
    ss << "=== AUDIT TRAIL EXPORT ===\n";
    ss << "Genesis Hash: " << genesis_hash << "\n";
    ss << "Total Blocks: " << blocks.size() << "\n";
    ss << "Total Transactions: " << get_transaction_count() << "\n\n";
    
    for (const auto& block : blocks) {
        ss << "Block #" << block.block_number << "\n";
        ss << "  Hash: " << block.block_hash << "\n";
        ss << "  Previous Hash: " << block.previous_block_hash << "\n";
        ss << "  Transactions: " << block.transactions.size() << "\n";
        ss << "  Verified: " << (block.verify_integrity() ? "YES" : "NO") << "\n\n";
    }
    
    return ss.str();
}

size_t AuditTrail::get_transaction_count() const {
    size_t count = 0;
    for (const auto& block : blocks) {
        count += block.transactions.size();
    }
    return count;
}

// ============================================================================
// Transaction Engine Implementation
// ============================================================================

TransactionEngine::TransactionEngine(size_t workers) 
    : is_running(false), transactions_processed(0), total_batches_processed(0),
      total_transactions_failed(0), num_workers(workers) {
    audit_trail = std::make_unique<AuditTrail>();
    start_time = std::chrono::steady_clock::now();
}

TransactionEngine::~TransactionEngine() {
    stop();
}

void TransactionEngine::start() {
    if (is_running.load()) return;
    
    is_running.store(true);
    
    for (size_t i = 0; i < num_workers; i++) {
        worker_threads.emplace_back(&TransactionEngine::worker_thread_func, this);
    }
}

void TransactionEngine::stop() {
    if (!is_running.load()) return;
    
    is_running.store(false);
    queue_cv.notify_all();
    
    for (auto& thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    worker_threads.clear();
}

void TransactionEngine::set_transaction_processor(std::function<bool(const Transaction&)> processor) {
    transaction_processor = processor;
}

void TransactionEngine::worker_thread_func() {
    while (is_running.load()) {
        std::shared_ptr<BatchTransaction> batch;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this] { 
                return !batch_queue.empty() || !is_running.load(); 
            });
            
            if (!is_running.load() && batch_queue.empty()) {
                break;
            }
            
            if (!batch_queue.empty()) {
                batch = batch_queue.front();
                batch_queue.pop();
            }
        }
        
        if (batch) {
            // Process batch
            std::string prev_hash = "";
            if (audit_trail->get_block_count() > 0) {
                auto last_block = audit_trail->get_block(audit_trail->get_block_count() - 1);
                if (last_block && !last_block->transactions.empty()) {
                    prev_hash = last_block->transactions.back().hash;
                }
            }
            
            std::vector<Transaction> processed_txs;
            
            for (size_t i = 0; i < batch->transactions.size(); i++) {
                auto& tx = batch->transactions[i];
                
                // Set previous hash for blockchain chain
                tx.prev_hash = prev_hash;
                
                // Calculate transaction hash
                tx.calculate_hash();
                
                // Process transaction
                bool success = false;
                if (transaction_processor) {
                    success = transaction_processor(tx);
                } else {
                    success = tx.is_balanced(); // Default validation
                }
                
                if (success) {
                    processed_txs.push_back(tx);
                    prev_hash = tx.hash;
                    transactions_processed.fetch_add(1);
                } else {
                    batch->failed_indices.push_back(i);
                    batch->has_errors.store(true);
                    total_transactions_failed.fetch_add(1);
                }
            }
            
            // Add to audit trail
            if (!processed_txs.empty()) {
                audit_trail->add_transactions(processed_txs);
            }
            
            batch->completed.store(true);
            total_batches_processed.fetch_add(1);
        }
    }
}

std::string TransactionEngine::submit_batch(const std::vector<Transaction>& transactions) {
    auto batch = std::make_shared<BatchTransaction>();
    batch->batch_id = TransactionUtils::generate_batch_id();
    batch->transactions = transactions;
    
    // Generate timestamps for transactions without them
    for (auto& tx : batch->transactions) {
        if (tx.timestamp.empty()) {
            tx.generate_timestamp();
        }
        if (tx.id.empty()) {
            tx.id = TransactionUtils::generate_transaction_id();
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(batch_status_mutex);
        batch_status_map[batch->batch_id] = batch;
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        batch_queue.push(batch);
    }
    
    queue_cv.notify_one();
    
    return batch->batch_id;
}

std::shared_ptr<BatchTransaction> TransactionEngine::get_batch_status(const std::string& batch_id) {
    std::lock_guard<std::mutex> lock(batch_status_mutex);
    auto it = batch_status_map.find(batch_id);
    return (it != batch_status_map.end()) ? it->second : nullptr;
}

void TransactionEngine::wait_for_batch(const std::string& batch_id) {
    auto batch = get_batch_status(batch_id);
    if (!batch) return;
    
    while (!batch->completed.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool TransactionEngine::add_template(const TransactionTemplate& tmpl) {
    std::lock_guard<std::mutex> lock(template_mutex);
    
    if (templates.find(tmpl.id) != templates.end()) {
        return false; // Template already exists
    }
    
    templates[tmpl.id] = tmpl;
    return true;
}

bool TransactionEngine::remove_template(const std::string& template_id) {
    std::lock_guard<std::mutex> lock(template_mutex);
    return templates.erase(template_id) > 0;
}

const TransactionTemplate* TransactionEngine::get_template(const std::string& template_id) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(template_mutex));
    auto it = templates.find(template_id);
    return (it != templates.end()) ? &it->second : nullptr;
}

std::vector<std::string> TransactionEngine::list_templates() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(template_mutex));
    
    std::vector<std::string> result;
    for (const auto& [id, tmpl] : templates) {
        result.push_back(id);
    }
    return result;
}

bool TransactionEngine::add_recurrence(const RecurrenceSchedule& schedule) {
    std::lock_guard<std::mutex> lock(recurrence_mutex);
    
    if (recurrence_schedules.find(schedule.id) != recurrence_schedules.end()) {
        return false;
    }
    
    recurrence_schedules[schedule.id] = schedule;
    return true;
}

bool TransactionEngine::remove_recurrence(const std::string& recurrence_id) {
    std::lock_guard<std::mutex> lock(recurrence_mutex);
    return recurrence_schedules.erase(recurrence_id) > 0;
}

bool TransactionEngine::update_recurrence(const std::string& recurrence_id, const RecurrenceSchedule& schedule) {
    std::lock_guard<std::mutex> lock(recurrence_mutex);
    
    auto it = recurrence_schedules.find(recurrence_id);
    if (it == recurrence_schedules.end()) {
        return false;
    }
    
    recurrence_schedules[recurrence_id] = schedule;
    return true;
}

std::vector<std::string> TransactionEngine::list_recurrences() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(recurrence_mutex));
    
    std::vector<std::string> result;
    for (const auto& [id, schedule] : recurrence_schedules) {
        result.push_back(id);
    }
    return result;
}

void TransactionEngine::process_due_recurrences() {
    std::vector<Transaction> due_transactions;
    
    {
        std::lock_guard<std::mutex> rec_lock(recurrence_mutex);
        std::lock_guard<std::mutex> tmpl_lock(template_mutex);
        
        for (auto& [id, schedule] : recurrence_schedules) {
            if (schedule.should_execute_now()) {
                // Find template
                auto tmpl_it = templates.find(schedule.template_id);
                if (tmpl_it != templates.end()) {
                    // Create transaction from template
                    Transaction tx = tmpl_it->second.instantiate(schedule.template_values);
                    tx.is_recurring = true;
                    tx.recurrence_id = schedule.id;
                    
                    due_transactions.push_back(tx);
                    
                    // Update schedule
                    schedule.execution_count++;
                    schedule.calculate_next_occurrence();
                }
            }
        }
    }
    
    // Submit due transactions as batch
    if (!due_transactions.empty()) {
        submit_batch(due_transactions);
    }
}

bool TransactionEngine::verify_audit_trail() const {
    return audit_trail->verify_trail();
}

std::string TransactionEngine::export_audit_trail() const {
    return audit_trail->export_trail();
}

double TransactionEngine::get_transactions_per_second() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
    
    if (duration == 0) return 0.0;
    
    return static_cast<double>(transactions_processed.load()) / duration;
}

std::string TransactionEngine::get_performance_report() const {
    std::stringstream ss;
    
    ss << "=== TRANSACTION ENGINE PERFORMANCE REPORT ===\n";
    ss << "Status: " << (is_running.load() ? "RUNNING" : "STOPPED") << "\n";
    ss << "Worker Threads: " << num_workers << "\n";
    ss << "Total Transactions Processed: " << transactions_processed.load() << "\n";
    ss << "Total Batches Processed: " << total_batches_processed.load() << "\n";
    ss << "Total Transactions Failed: " << total_transactions_failed.load() << "\n";
    ss << "Transactions Per Second: " << std::fixed << std::setprecision(2) 
       << get_transactions_per_second() << "\n";
    ss << "Audit Trail Blocks: " << audit_trail->get_block_count() << "\n";
    ss << "Audit Trail Verified: " << (audit_trail->verify_trail() ? "YES" : "NO") << "\n";
    ss << "Templates Registered: " << templates.size() << "\n";
    ss << "Recurring Schedules: " << recurrence_schedules.size() << "\n";
    
    return ss.str();
}

// ============================================================================
// Multi-leg Transaction Implementation
// ============================================================================

void MultiLegTransaction::add_leg(const Leg& leg) {
    legs.push_back(leg);
}

void MultiLegTransaction::remove_leg(const std::string& leg_id) {
    legs.erase(
        std::remove_if(legs.begin(), legs.end(),
            [&leg_id](const Leg& leg) { return leg.leg_id == leg_id; }),
        legs.end()
    );
}

Transaction MultiLegTransaction::to_transaction() const {
    Transaction tx;
    tx.id = transaction_id;
    tx.description = description;
    tx.generate_timestamp();
    
    // Combine all leg entries
    for (const auto& leg : legs) {
        for (const auto& entry : leg.entries) {
            tx.entries.push_back(entry);
        }
    }
    
    return tx;
}

bool MultiLegTransaction::is_valid() const {
    // Check that all legs have valid entries
    for (const auto& leg : legs) {
        if (leg.entries.empty()) return false;
    }
    
    // Validate as transaction
    Transaction tx = to_transaction();
    return tx.is_balanced();
}

MultiLegTransaction MultiLegTransaction::create_fx_swap(
    const std::string& from_currency,
    const std::string& to_currency,
    double spot_amount,
    double forward_amount,
    const std::chrono::system_clock::time_point& spot_date,
    const std::chrono::system_clock::time_point& forward_date
) {
    MultiLegTransaction multi_leg;
    multi_leg.set_transaction_id(TransactionUtils::generate_transaction_id("FX-SWAP"));
    multi_leg.set_description("FX Swap: " + from_currency + " to " + to_currency);
    
    // Spot leg
    Leg spot_leg;
    spot_leg.leg_id = "SPOT";
    spot_leg.type = LegType::FX_SWAP;
    spot_leg.currency = from_currency;
    spot_leg.notional_amount = spot_amount;
    spot_leg.settlement_date = spot_date;
    
    TransactionEntry spot_from("1101", spot_amount, 0.0, "FX Swap Spot - Sell " + from_currency);
    TransactionEntry spot_to("1101", 0.0, spot_amount, "FX Swap Spot - Buy " + to_currency);
    spot_leg.entries.push_back(spot_from);
    spot_leg.entries.push_back(spot_to);
    
    // Forward leg
    Leg forward_leg;
    forward_leg.leg_id = "FORWARD";
    forward_leg.type = LegType::FX_SWAP;
    forward_leg.currency = to_currency;
    forward_leg.notional_amount = forward_amount;
    forward_leg.settlement_date = forward_date;
    
    TransactionEntry fwd_from("1101", forward_amount, 0.0, "FX Swap Forward - Buy " + from_currency);
    TransactionEntry fwd_to("1101", 0.0, forward_amount, "FX Swap Forward - Sell " + to_currency);
    forward_leg.entries.push_back(fwd_from);
    forward_leg.entries.push_back(fwd_to);
    
    multi_leg.add_leg(spot_leg);
    multi_leg.add_leg(forward_leg);
    
    return multi_leg;
}

MultiLegTransaction MultiLegTransaction::create_interest_rate_swap(
    double notional,
    double fixed_rate,
    const std::string& floating_rate_index,
    const std::chrono::system_clock::time_point& start_date,
    const std::chrono::system_clock::time_point& end_date
) {
    MultiLegTransaction multi_leg;
    multi_leg.set_transaction_id(TransactionUtils::generate_transaction_id("IRS"));
    multi_leg.set_description("Interest Rate Swap - Fixed vs " + floating_rate_index);
    
    // Fixed leg
    Leg fixed_leg;
    fixed_leg.leg_id = "FIXED";
    fixed_leg.type = LegType::INTEREST_RATE_SWAP;
    fixed_leg.currency = "USD";
    fixed_leg.notional_amount = notional;
    fixed_leg.settlement_date = end_date;
    fixed_leg.metadata["rate"] = std::to_string(fixed_rate);
    
    double fixed_payment = notional * fixed_rate;
    TransactionEntry fixed_entry("5103", fixed_payment, 0.0, "IRS Fixed Rate Payment");
    fixed_leg.entries.push_back(fixed_entry);
    
    // Floating leg
    Leg floating_leg;
    floating_leg.leg_id = "FLOATING";
    floating_leg.type = LegType::INTEREST_RATE_SWAP;
    floating_leg.currency = "USD";
    floating_leg.notional_amount = notional;
    floating_leg.settlement_date = end_date;
    floating_leg.metadata["index"] = floating_rate_index;
    
    // Placeholder for floating payment (would be determined by index)
    TransactionEntry floating_entry("4200", 0.0, fixed_payment, "IRS Floating Rate Receipt");
    floating_leg.entries.push_back(floating_entry);
    
    multi_leg.add_leg(fixed_leg);
    multi_leg.add_leg(floating_leg);
    
    return multi_leg;
}

// ============================================================================
// Utility Functions
// ============================================================================

namespace TransactionUtils {

std::string generate_transaction_id(const std::string& prefix) {
    static std::atomic<uint64_t> counter(0);
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    std::stringstream ss;
    ss << prefix << "-" << timestamp << "-" << counter.fetch_add(1);
    return ss.str();
}

std::string generate_batch_id() {
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    std::stringstream ss;
    ss << "BATCH-" << timestamp;
    return ss.str();
}

bool validate_transaction(const Transaction& tx) {
    // Check ID
    if (tx.id.empty()) return false;
    
    // Check entries
    if (tx.entries.empty()) return false;
    
    // Check balance
    if (!tx.is_balanced()) return false;
    
    // Check for valid account codes
    for (const auto& entry : tx.entries) {
        if (entry.account_code.empty()) return false;
    }
    
    return true;
}

std::vector<TransactionTemplate> create_sample_templates() {
    std::vector<TransactionTemplate> templates;
    
    // Template 1: Salary Payment
    TransactionTemplate salary;
    salary.id = "TMPL-SALARY";
    salary.name = "Monthly Salary Payment";
    salary.description = "Standard salary payment template";
    salary.entry_template = {
        TransactionEntry("5101", 0.0, 0.0, "Salary Expense {amount}"),
        TransactionEntry("1101", 0.0, 0.0, "Cash Payment {amount}")
    };
    templates.push_back(salary);
    
    // Template 2: Rent Payment
    TransactionTemplate rent;
    rent.id = "TMPL-RENT";
    rent.name = "Monthly Rent Payment";
    rent.description = "Standard rent payment template";
    rent.entry_template = {
        TransactionEntry("5102", 0.0, 0.0, "Rent Expense {amount}"),
        TransactionEntry("1101", 0.0, 0.0, "Cash Payment {amount}")
    };
    templates.push_back(rent);
    
    // Template 3: Sales Revenue
    TransactionTemplate sales;
    sales.id = "TMPL-SALES";
    sales.name = "Product Sales";
    sales.description = "Standard sales revenue template";
    sales.entry_template = {
        TransactionEntry("1101", 0.0, 0.0, "Cash Received {amount}"),
        TransactionEntry("4100", 0.0, 0.0, "Sales Revenue {amount}")
    };
    templates.push_back(sales);
    
    return templates;
}

std::vector<Transaction> generate_test_transactions(size_t count) {
    std::vector<Transaction> transactions;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> amount_dist(100.0, 10000.0);
    
    for (size_t i = 0; i < count; i++) {
        Transaction tx;
        tx.id = generate_transaction_id("TEST");
        tx.description = "Test transaction " + std::to_string(i);
        tx.generate_timestamp();
        
        double amount = amount_dist(gen);
        
        tx.entries = {
            TransactionEntry("1101", amount, 0.0, "Test debit"),
            TransactionEntry("4100", 0.0, amount, "Test credit")
        };
        
        transactions.push_back(tx);
    }
    
    return transactions;
}

} // namespace TransactionUtils
