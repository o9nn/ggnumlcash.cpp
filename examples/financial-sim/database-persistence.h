#pragma once

#include "transaction-engine.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <mutex>
#include <map>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>

// Simple Account structure for persistence (simplified version)
struct Account {
    std::string code;
    std::string name;
    std::string type_str;
    double balance;
    std::map<std::string, std::string> metadata;
    
    Account() : code(""), name(""), type_str("ASSET"), balance(0.0) {}
};

// Forward declaration
class DatabaseConnection;
class ConnectionPool;

// ============================================================================
// Database Configuration
// ============================================================================

enum class DatabaseType {
    POSTGRESQL,
    TIMESCALEDB,
    SQLITE  // For testing
};

struct DatabaseConfig {
    DatabaseType type;
    std::string host;
    std::string port;
    std::string database;
    std::string username;
    std::string password;
    std::string connection_string;
    
    // Connection pool settings
    size_t pool_size;
    size_t max_connections;
    std::chrono::seconds connection_timeout;
    std::chrono::seconds idle_timeout;
    
    // Performance tuning
    bool enable_async_writes;
    size_t batch_size;
    std::chrono::milliseconds flush_interval;
    
    // Compression and archival
    bool enable_compression;
    std::chrono::hours archive_after;
    std::chrono::hours delete_after;
    
    // Backup and encryption
    bool enable_encryption;
    std::string encryption_key;
    std::string backup_directory;
    std::chrono::hours backup_interval;
    size_t backup_retention_count;
    
    DatabaseConfig() 
        : type(DatabaseType::POSTGRESQL),
          host("localhost"),
          port("5432"),
          database("ggnucash"),
          username("ggnucash"),
          password(""),
          connection_string(""),
          pool_size(10),
          max_connections(50),
          connection_timeout(std::chrono::seconds(30)),
          idle_timeout(std::chrono::seconds(300)),
          enable_async_writes(true),
          batch_size(1000),
          flush_interval(std::chrono::milliseconds(100)),
          enable_compression(true),
          archive_after(std::chrono::hours(24 * 30)), // 30 days
          delete_after(std::chrono::hours(24 * 365 * 7)), // 7 years
          enable_encryption(true),
          encryption_key(""),
          backup_directory("/var/backups/ggnucash"),
          backup_interval(std::chrono::hours(24)),
          backup_retention_count(30) {}
    
    // Build connection string from components
    void build_connection_string() {
        if (type == DatabaseType::POSTGRESQL || type == DatabaseType::TIMESCALEDB) {
            connection_string = "host=" + host + 
                              " port=" + port +
                              " dbname=" + database +
                              " user=" + username;
            if (!password.empty()) {
                connection_string += " password=" + password;
            }
            connection_string += " connect_timeout=" + std::to_string(connection_timeout.count());
        }
    }
};

// ============================================================================
// Database Result
// ============================================================================

class DatabaseResult {
private:
    bool success;
    std::string error_message;
    std::vector<std::map<std::string, std::string>> rows;
    uint64_t rows_affected;
    
public:
    DatabaseResult() : success(false), error_message(""), rows_affected(0) {}
    
    bool is_success() const { return success; }
    const std::string& get_error() const { return error_message; }
    const std::vector<std::map<std::string, std::string>>& get_rows() const { return rows; }
    uint64_t get_rows_affected() const { return rows_affected; }
    
    void set_success(bool s) { success = s; }
    void set_error(const std::string& err) { error_message = err; }
    void add_row(const std::map<std::string, std::string>& row) { rows.push_back(row); }
    void set_rows_affected(uint64_t count) { rows_affected = count; }
};

// ============================================================================
// Database Connection (Abstract Interface)
// ============================================================================

class DatabaseConnection {
protected:
    bool connected;
    std::string connection_id;
    std::chrono::steady_clock::time_point last_used;
    std::mutex connection_mutex;
    
public:
    DatabaseConnection() : connected(false), connection_id("") {
        last_used = std::chrono::steady_clock::now();
    }
    virtual ~DatabaseConnection() = default;
    
    virtual bool connect(const DatabaseConfig& config) = 0;
    virtual bool disconnect() = 0;
    virtual bool is_connected() const { return connected; }
    
    virtual DatabaseResult execute(const std::string& query) = 0;
    virtual DatabaseResult execute_prepared(const std::string& query, 
                                           const std::vector<std::string>& params) = 0;
    
    virtual bool begin_transaction() = 0;
    virtual bool commit_transaction() = 0;
    virtual bool rollback_transaction() = 0;
    
    virtual std::string escape_string(const std::string& str) = 0;
    
    void update_last_used() { last_used = std::chrono::steady_clock::now(); }
    std::chrono::steady_clock::time_point get_last_used() const { return last_used; }
    const std::string& get_connection_id() const { return connection_id; }
};

// ============================================================================
// PostgreSQL Connection Implementation
// ============================================================================

class PostgreSQLConnection : public DatabaseConnection {
private:
    void* pg_conn;  // PGconn* - using void* to avoid libpq dependency in stub implementation
                    // In production: replace with proper forward declaration and pimpl idiom
    
public:
    PostgreSQLConnection();
    ~PostgreSQLConnection() override;
    
    bool connect(const DatabaseConfig& config) override;
    bool disconnect() override;
    
    DatabaseResult execute(const std::string& query) override;
    DatabaseResult execute_prepared(const std::string& query,
                                   const std::vector<std::string>& params) override;
    
    bool begin_transaction() override;
    bool commit_transaction() override;
    bool rollback_transaction() override;
    
    std::string escape_string(const std::string& str) override;
};

// ============================================================================
// Connection Pool
// ============================================================================

class ConnectionPool {
private:
    DatabaseConfig config;
    std::vector<std::unique_ptr<DatabaseConnection>> connections;
    std::queue<DatabaseConnection*> available_connections;
    std::mutex pool_mutex;
    std::condition_variable pool_cv;
    std::atomic<size_t> active_connections;
    std::atomic<bool> is_running;
    
    std::thread maintenance_thread;
    void maintenance_loop();
    
public:
    ConnectionPool(const DatabaseConfig& cfg);
    ~ConnectionPool();
    
    bool initialize();
    void shutdown();
    
    DatabaseConnection* acquire_connection(std::chrono::seconds timeout = std::chrono::seconds(30));
    void release_connection(DatabaseConnection* conn);
    
    size_t get_pool_size() const { return connections.size(); }
    size_t get_active_count() const { return active_connections.load(); }
    size_t get_available_count() const;
};

// ============================================================================
// Point-in-Time Recovery Manager
// ============================================================================

struct RecoveryPoint {
    std::string id;
    std::string description;
    std::chrono::system_clock::time_point timestamp;
    uint64_t transaction_count;
    uint64_t checkpoint_id;
    std::string snapshot_path;
    bool is_verified;
    
    RecoveryPoint() : id(""), description(""), transaction_count(0), 
                     checkpoint_id(0), snapshot_path(""), is_verified(false) {
        timestamp = std::chrono::system_clock::now();
    }
};

class RecoveryManager {
private:
    std::string wal_directory;
    std::string snapshot_directory;
    std::vector<RecoveryPoint> recovery_points;
    std::mutex recovery_mutex;
    
    uint64_t current_checkpoint_id;
    std::atomic<bool> recovery_in_progress;
    
public:
    RecoveryManager(const std::string& wal_dir, const std::string& snapshot_dir);
    
    // Create recovery point
    std::string create_recovery_point(const std::string& description,
                                     DatabaseConnection* conn);
    
    // List recovery points
    std::vector<RecoveryPoint> list_recovery_points() const;
    
    // Restore to a specific point
    bool restore_to_point(const std::string& recovery_point_id,
                         DatabaseConnection* conn);
    
    // Restore to specific timestamp
    bool restore_to_timestamp(const std::chrono::system_clock::time_point& timestamp,
                             DatabaseConnection* conn);
    
    // Verify recovery point
    bool verify_recovery_point(const std::string& recovery_point_id) const;
    
    // Cleanup old recovery points
    void cleanup_old_points(std::chrono::hours retention_period);
    
    bool is_recovery_active() const { return recovery_in_progress.load(); }
};

// ============================================================================
// Data Archival Manager
// ============================================================================

struct ArchivalPolicy {
    std::string name;
    std::chrono::hours archive_after;
    std::chrono::hours delete_after;
    bool enable_compression;
    std::string compression_algorithm; // gzip, lz4, zstd
    std::string archive_table_name;
    std::vector<std::string> included_tables;
    
    ArchivalPolicy() 
        : name("default"),
          archive_after(std::chrono::hours(24 * 30)),
          delete_after(std::chrono::hours(24 * 365 * 7)),
          enable_compression(true),
          compression_algorithm("zstd"),
          archive_table_name("") {}
};

class ArchivalManager {
private:
    std::map<std::string, ArchivalPolicy> policies;
    std::string archive_directory;
    std::mutex archival_mutex;
    
    std::atomic<bool> is_archiving;
    std::atomic<uint64_t> records_archived;
    std::atomic<uint64_t> bytes_saved;
    
public:
    ArchivalManager(const std::string& archive_dir);
    
    // Add archival policy
    void add_policy(const ArchivalPolicy& policy);
    
    // Execute archival for a specific policy
    bool execute_archival(const std::string& policy_name, DatabaseConnection* conn);
    
    // Execute all archival policies
    bool execute_all_archival_policies(ConnectionPool* pool);
    
    // Restore archived data
    bool restore_archived_data(const std::string& archive_id,
                              const std::chrono::system_clock::time_point& start_time,
                              const std::chrono::system_clock::time_point& end_time,
                              DatabaseConnection* conn);
    
    // Get archival statistics
    uint64_t get_records_archived() const { return records_archived.load(); }
    uint64_t get_bytes_saved() const { return bytes_saved.load(); }
    
    bool is_archival_active() const { return is_archiving.load(); }
};

// ============================================================================
// Backup Manager with Encryption
// ============================================================================

struct BackupInfo {
    std::string backup_id;
    std::string backup_path;
    std::chrono::system_clock::time_point timestamp;
    uint64_t size_bytes;
    bool is_encrypted;
    bool is_compressed;
    std::string checksum;
    bool is_verified;
    
    BackupInfo() : backup_id(""), backup_path(""), size_bytes(0),
                  is_encrypted(false), is_compressed(false),
                  checksum(""), is_verified(false) {
        timestamp = std::chrono::system_clock::now();
    }
};

class BackupManager {
private:
    std::string backup_directory;
    std::string encryption_key;
    std::vector<BackupInfo> backup_history;
    std::mutex backup_mutex;
    
    std::atomic<bool> backup_in_progress;
    size_t retention_count;
    
    // Encryption helpers
    bool encrypt_file(const std::string& input_path, const std::string& output_path);
    bool decrypt_file(const std::string& input_path, const std::string& output_path);
    std::string calculate_checksum(const std::string& file_path);
    
public:
    BackupManager(const std::string& backup_dir, const std::string& key);
    
    // Create backup
    std::string create_backup(DatabaseConnection* conn, bool encrypt = true, bool compress = true);
    
    // Restore from backup
    bool restore_from_backup(const std::string& backup_id, DatabaseConnection* conn);
    
    // List backups
    std::vector<BackupInfo> list_backups() const;
    
    // Verify backup
    bool verify_backup(const std::string& backup_id) const;
    
    // Cleanup old backups
    void cleanup_old_backups();
    
    // Set retention policy
    void set_retention_count(size_t count) { retention_count = count; }
    
    bool is_backup_active() const { return backup_in_progress.load(); }
};

// ============================================================================
// Main Database Persistence Manager
// ============================================================================

class DatabasePersistenceManager {
private:
    DatabaseConfig config;
    std::unique_ptr<ConnectionPool> connection_pool;
    std::unique_ptr<RecoveryManager> recovery_manager;
    std::unique_ptr<ArchivalManager> archival_manager;
    std::unique_ptr<BackupManager> backup_manager;
    
    // Async write queue
    struct WriteOperation {
        enum class Type { INSERT_TRANSACTION, UPDATE_ACCOUNT, INSERT_AUDIT_BLOCK };
        Type type;
        std::string data;
        std::function<void(bool)> callback;
    };
    
    std::queue<WriteOperation> write_queue;
    std::mutex write_mutex;
    std::condition_variable write_cv;
    std::vector<std::thread> write_threads;
    std::atomic<bool> is_running;
    
    void write_worker_thread();
    
    // Schema management
    bool create_schema(DatabaseConnection* conn);
    bool verify_schema(DatabaseConnection* conn);
    
public:
    DatabasePersistenceManager(const DatabaseConfig& cfg);
    ~DatabasePersistenceManager();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Account persistence
    bool save_account(const Account& account);
    bool load_account(const std::string& account_code, Account& account);
    bool delete_account(const std::string& account_code);
    std::vector<Account> load_all_accounts();
    
    // Transaction persistence
    bool save_transaction(const Transaction& transaction);
    bool load_transaction(const std::string& transaction_id, Transaction& transaction);
    std::vector<Transaction> load_transactions_by_date_range(
        const std::chrono::system_clock::time_point& start,
        const std::chrono::system_clock::time_point& end);
    
    // Audit trail persistence
    bool save_audit_block(const AuditBlock& block);
    bool load_audit_trail(AuditTrail& trail);
    
    // Batch operations
    bool save_transactions_batch(const std::vector<Transaction>& transactions);
    
    // Recovery operations
    std::string create_recovery_point(const std::string& description);
    bool restore_to_recovery_point(const std::string& recovery_point_id);
    std::vector<RecoveryPoint> list_recovery_points() const;
    
    // Archival operations
    bool execute_archival();
    
    // Backup operations
    std::string create_backup(bool encrypt = true);
    bool restore_from_backup(const std::string& backup_id);
    std::vector<BackupInfo> list_backups() const;
    
    // Statistics
    struct PersistenceStats {
        uint64_t total_accounts;
        uint64_t total_transactions;
        uint64_t total_audit_blocks;
        uint64_t database_size_bytes;
        uint64_t backup_count;
        uint64_t recovery_point_count;
        double write_throughput_tps;
        std::chrono::milliseconds avg_write_latency;
        double uptime_percentage;
    };
    
    PersistenceStats get_statistics() const;
    
    // Health check
    bool health_check();
    
    // Get managers for advanced operations
    ConnectionPool* get_connection_pool() { return connection_pool.get(); }
    RecoveryManager* get_recovery_manager() { return recovery_manager.get(); }
    ArchivalManager* get_archival_manager() { return archival_manager.get(); }
    BackupManager* get_backup_manager() { return backup_manager.get(); }
};
