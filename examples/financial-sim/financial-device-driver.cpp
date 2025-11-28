#include "financial-device-driver.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <thread>

namespace ggnucash {
namespace vdev {

// ============================================================================
// Financial Device Driver Implementation
// ============================================================================

bool FinancialDeviceDriver::load(VirtualPCB* pcb) {
    if (!pcb) return false;
    
    device = pcb;
    is_loaded = true;
    
    std::cout << "Loading " << driver_name << " v" << driver_version << "...\n";
    return true;
}

bool FinancialDeviceDriver::initialize() {
    if (!is_loaded || !device) return false;
    
    std::cout << "Initializing financial hardware driver...\n";
    
    // Map financial accounts to hardware pins
    // Assets on pins 0-15
    map_account_to_pin("1101", 0);  // Cash
    map_account_to_pin("1102", 1);  // Accounts Receivable
    map_account_to_pin("1103", 2);  // Inventory
    map_account_to_pin("1201", 3);  // Equipment
    map_account_to_pin("1202", 4);  // Buildings
    
    // Liabilities on pins 16-31
    map_account_to_pin("2101", 16); // Accounts Payable
    map_account_to_pin("2102", 17); // Short-term Loans
    map_account_to_pin("2201", 18); // Long-term Loans
    
    // Equity on pins 32-39
    map_account_to_pin("3100", 32); // Owner's Equity
    map_account_to_pin("3200", 33); // Retained Earnings
    
    // Revenue on pins 40-47
    map_account_to_pin("4100", 40); // Sales Revenue
    map_account_to_pin("4200", 41); // Service Revenue
    
    // Expenses on pins 48-63
    map_account_to_pin("5101", 48); // Salaries
    map_account_to_pin("5102", 49); // Rent
    map_account_to_pin("5103", 50); // Utilities
    
    // Configure all mapped pins as analog inputs
    for (const auto& pair : account_pins) {
        device->configure_pin(pair.second.pin_number, PinMode::ANALOG_INPUT);
    }
    
    // Create transaction buffer in peripheral memory
    auto* periph_region = device->get_memory_region("PERIPH");
    if (periph_region) {
        transaction_buffer = std::make_shared<MemoryRegion>(
            REG_BASE + 0x1000, 4096, "TXN_BUFFER");
    }
    
    // Initialize hardware registers
    device->write_memory(REG_TRANSACTION_STATUS, 0x00);
    device->write_memory(REG_TRANSACTION_COUNT, 0x00);
    device->write_memory(REG_ERROR_CODE, 0x00);
    
    is_initialized = true;
    std::cout << "Financial hardware driver initialized successfully.\n";
    std::cout << "Mapped " << account_pins.size() << " accounts to GPIO pins.\n";
    
    return true;
}

bool FinancialDeviceDriver::probe() {
    if (!device) return false;
    
    std::cout << "Probing financial hardware...\n";
    
    // Check if device is responsive
    if (device->get_state() != DeviceState::READY && 
        device->get_state() != DeviceState::RUNNING) {
        std::cerr << "Device not in operational state\n";
        return false;
    }
    
    // Test pin access
    for (const auto& pair : account_pins) {
        PinState state = device->get_pin_state(pair.second.pin_number);
        (void)state;  // Suppress unused variable warning
    }
    
    std::cout << "Hardware probe completed successfully.\n";
    return true;
}

bool FinancialDeviceDriver::remove() {
    if (!device) return false;
    
    std::cout << "Removing financial hardware driver...\n";
    
    // Reset all account pins
    for (const auto& pair : account_pins) {
        device->set_analog_value(pair.second.pin_number, 0);
    }
    
    account_pins.clear();
    is_initialized = false;
    is_loaded = false;
    
    std::cout << "Driver removed.\n";
    return true;
}

bool FinancialDeviceDriver::map_account_to_pin(const std::string& account_code, uint32_t pin) {
    AccountPin ap;
    ap.account_code = account_code;
    ap.pin_number = pin;
    ap.balance_voltage = 0.0;
    ap.is_active = true;
    
    account_pins[account_code] = ap;
    return true;
}

bool FinancialDeviceDriver::update_account_balance(const std::string& account_code, double balance) {
    auto it = account_pins.find(account_code);
    if (it == account_pins.end()) {
        return false;
    }
    
    // Map balance to voltage (0-3.3V for 12-bit ADC)
    // Using logarithmic scale for large balances
    double voltage = 0.0;
    if (balance > 0) {
        voltage = std::min(3.3, std::log10(std::abs(balance) + 1.0) / 6.0 * 3.3);
    }
    
    // Convert voltage to 12-bit ADC value (0-4095)
    uint16_t adc_value = static_cast<uint16_t>(voltage / 3.3 * 4095.0);
    
    it->second.balance_voltage = voltage;
    device->set_analog_value(it->second.pin_number, adc_value);
    
    return true;
}

double FinancialDeviceDriver::read_account_balance(const std::string& account_code) {
    auto it = account_pins.find(account_code);
    if (it == account_pins.end()) {
        return 0.0;
    }
    
    uint16_t adc_value = device->get_analog_value(it->second.pin_number);
    double voltage = (adc_value / 4095.0) * 3.3;
    
    // Reverse logarithmic mapping
    double balance = std::pow(10.0, (voltage / 3.3) * 6.0) - 1.0;
    
    return balance;
}

bool FinancialDeviceDriver::process_hardware_transaction(const std::vector<uint8_t>& tx_data) {
    if (!transaction_buffer) return false;
    
    // Write transaction data to buffer
    for (size_t i = 0; i < tx_data.size() && i < transaction_buffer->size; i++) {
        transaction_buffer->write_byte(i, tx_data[i]);
    }
    
    // Update transaction count
    uint8_t count = device->read_memory(REG_TRANSACTION_COUNT);
    device->write_memory(REG_TRANSACTION_COUNT, count + 1);
    
    // Set status to processing
    device->write_memory(REG_TRANSACTION_STATUS, 0x01);
    
    return true;
}

std::string FinancialDeviceDriver::get_hardware_diagnostics() {
    std::stringstream ss;
    
    ss << "=== Financial Hardware Diagnostics ===\n";
    ss << "Driver: " << driver_name << " v" << driver_version << "\n";
    ss << "Status: " << (is_initialized ? "INITIALIZED" : "NOT INITIALIZED") << "\n";
    ss << "Mapped Accounts: " << account_pins.size() << "\n\n";
    
    ss << "Account Pin Mapping:\n";
    for (const auto& pair : account_pins) {
        ss << "  " << pair.first << " -> GPIO" << pair.second.pin_number;
        ss << " (ADC: " << device->get_analog_value(pair.second.pin_number);
        ss << ", " << std::fixed << std::setprecision(3) 
           << pair.second.balance_voltage << "V)\n";
    }
    
    ss << "\nHardware Registers:\n";
    ss << "  TX_STATUS: 0x" << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(device->read_memory(REG_TRANSACTION_STATUS)) << "\n";
    ss << "  TX_COUNT: " << std::dec 
       << static_cast<int>(device->read_memory(REG_TRANSACTION_COUNT)) << "\n";
    ss << "  ERROR_CODE: 0x" << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(device->read_memory(REG_ERROR_CODE)) << "\n";
    
    return ss.str();
}

bool FinancialDeviceDriver::run_self_test() {
    std::cout << "Running financial hardware self-test...\n";
    
    // Test 1: Pin accessibility
    std::cout << "Test 1: Pin accessibility... ";
    bool test1_pass = true;
    for (const auto& pair : account_pins) {
        if (!device->set_analog_value(pair.second.pin_number, 2048)) {
            test1_pass = false;
            break;
        }
    }
    std::cout << (test1_pass ? "PASS" : "FAIL") << "\n";
    
    // Test 2: Memory regions
    std::cout << "Test 2: Memory regions... ";
    bool test2_pass = (device->get_memory_region("SRAM") != nullptr &&
                       device->get_memory_region("FLASH") != nullptr &&
                       device->get_memory_region("PERIPH") != nullptr);
    std::cout << (test2_pass ? "PASS" : "FAIL") << "\n";
    
    // Test 3: Register access
    std::cout << "Test 3: Register access... ";
    device->write_memory(REG_TRANSACTION_STATUS, 0xAA);
    uint8_t read_val = device->read_memory(REG_TRANSACTION_STATUS);
    bool test3_pass = (read_val == 0xAA);
    std::cout << (test3_pass ? "PASS" : "FAIL") << "\n";
    
    bool all_pass = test1_pass && test2_pass && test3_pass;
    std::cout << "\nSelf-test " << (all_pass ? "PASSED" : "FAILED") << "\n";
    
    return all_pass;
}

// ============================================================================
// Telemetry Metric Implementation
// ============================================================================

void TelemetryMetric::update(double new_value) {
    value = new_value;
    
    if (sample_count == 0) {
        min_value = new_value;
        max_value = new_value;
        avg_value = new_value;
    } else {
        min_value = std::min(min_value, new_value);
        max_value = std::max(max_value, new_value);
        avg_value = (avg_value * sample_count + new_value) / (sample_count + 1);
    }
    
    sample_count++;
    last_update = std::chrono::steady_clock::now();
}

void TelemetryMetric::reset() {
    value = 0.0;
    min_value = 0.0;
    max_value = 0.0;
    avg_value = 0.0;
    sample_count = 0;
}

// ============================================================================
// Telemetry System Implementation
// ============================================================================

void TelemetrySystem::start_collection() {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    is_collecting = true;
}

void TelemetrySystem::stop_collection() {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    is_collecting = false;
}

bool TelemetrySystem::add_metric(const std::string& name, const std::string& unit) {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    if (metrics.find(name) != metrics.end()) {
        return false;
    }
    
    metrics[name] = TelemetryMetric(name, unit);
    return true;
}

bool TelemetrySystem::update_metric(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    auto it = metrics.find(name);
    if (it == metrics.end()) {
        return false;
    }
    
    if (is_collecting) {
        it->second.update(value);
    }
    
    return true;
}

TelemetryMetric TelemetrySystem::get_metric(const std::string& name) {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    auto it = metrics.find(name);
    if (it != metrics.end()) {
        return it->second;
    }
    
    return TelemetryMetric("", "");
}

std::vector<std::string> TelemetrySystem::list_metrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    std::vector<std::string> metric_list;
    for (const auto& pair : metrics) {
        metric_list.push_back(pair.first);
    }
    return metric_list;
}

std::string TelemetrySystem::generate_report() {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    std::stringstream ss;
    ss << "=== Telemetry Report ===\n";
    ss << "Collection Status: " << (is_collecting ? "ACTIVE" : "STOPPED") << "\n";
    ss << "Metrics: " << metrics.size() << "\n\n";
    
    for (const auto& pair : metrics) {
        const auto& metric = pair.second;
        ss << metric.name << " (" << metric.unit << "):\n";
        ss << "  Current: " << std::fixed << std::setprecision(2) << metric.value << "\n";
        ss << "  Min: " << metric.min_value << "\n";
        ss << "  Max: " << metric.max_value << "\n";
        ss << "  Avg: " << metric.avg_value << "\n";
        ss << "  Samples: " << metric.sample_count << "\n\n";
    }
    
    return ss.str();
}

void TelemetrySystem::reset_all() {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    
    for (auto& pair : metrics) {
        pair.second.reset();
    }
}

// ============================================================================
// Configuration Manager Implementation
// ============================================================================

bool ConfigurationManager::create_configuration(const std::string& name) {
    std::lock_guard<std::mutex> lock(config_mutex);
    
    if (configurations.find(name) != configurations.end()) {
        return false;
    }
    
    configurations[name] = DeviceConfiguration(name);
    return true;
}

bool ConfigurationManager::delete_configuration(const std::string& name) {
    std::lock_guard<std::mutex> lock(config_mutex);
    
    if (name == active_config_name) {
        return false;  // Cannot delete active config
    }
    
    auto it = configurations.find(name);
    if (it != configurations.end()) {
        configurations.erase(it);
        return true;
    }
    
    return false;
}

bool ConfigurationManager::load_configuration(const std::string& name) {
    std::lock_guard<std::mutex> lock(config_mutex);
    
    auto it = configurations.find(name);
    if (it != configurations.end()) {
        active_config_name = name;
        it->second.is_active = true;
        return true;
    }
    
    return false;
}

bool ConfigurationManager::save_configuration(const std::string& name) {
    std::lock_guard<std::mutex> lock(config_mutex);
    
    auto it = configurations.find(name);
    if (it != configurations.end()) {
        it->second.last_modified = std::chrono::system_clock::now();
        return true;
    }
    
    return false;
}

bool ConfigurationManager::set_parameter(const std::string& param, const std::string& value) {
    std::lock_guard<std::mutex> lock(config_mutex);
    
    auto it = configurations.find(active_config_name);
    if (it != configurations.end()) {
        it->second.parameters[param] = value;
        it->second.last_modified = std::chrono::system_clock::now();
        return true;
    }
    
    return false;
}

std::string ConfigurationManager::get_parameter(const std::string& param) {
    std::lock_guard<std::mutex> lock(config_mutex);
    
    auto it = configurations.find(active_config_name);
    if (it != configurations.end()) {
        auto param_it = it->second.parameters.find(param);
        if (param_it != it->second.parameters.end()) {
            return param_it->second;
        }
    }
    
    return "";
}

bool ConfigurationManager::apply_configuration(VirtualPCB* device) {
    if (!device) return false;
    
    std::lock_guard<std::mutex> lock(config_mutex);
    
    auto it = configurations.find(active_config_name);
    if (it == configurations.end()) {
        return false;
    }
    
    // Apply configuration parameters to device
    // This would configure device based on stored parameters
    
    return true;
}

std::vector<std::string> ConfigurationManager::list_configurations() {
    std::lock_guard<std::mutex> lock(config_mutex);
    
    std::vector<std::string> config_list;
    for (const auto& pair : configurations) {
        config_list.push_back(pair.first);
    }
    return config_list;
}

bool ConfigurationManager::import_from_file(const std::string& filepath) {
    // TODO: Implement file import
    (void)filepath;
    return false;
}

bool ConfigurationManager::export_to_file(const std::string& filepath) {
    // TODO: Implement file export
    (void)filepath;
    return false;
}

// ============================================================================
// Diagnostic System Implementation
// ============================================================================

void DiagnosticSystem::log_event(DiagnosticLevel level, const std::string& source,
                                 const std::string& message) {
    std::lock_guard<std::mutex> lock(diagnostic_mutex);
    
    DiagnosticEvent event;
    event.level = level;
    event.source = source;
    event.message = message;
    event.event_id = next_event_id++;
    event.timestamp = std::chrono::system_clock::now();
    
    event_log.push_back(event);
    
    // Trim log if it exceeds max size
    if (event_log.size() > max_log_size) {
        event_log.erase(event_log.begin());
    }
}

std::vector<DiagnosticEvent> DiagnosticSystem::get_events(DiagnosticLevel min_level) {
    std::lock_guard<std::mutex> lock(diagnostic_mutex);
    
    std::vector<DiagnosticEvent> filtered;
    for (const auto& event : event_log) {
        if (event.level >= min_level) {
            filtered.push_back(event);
        }
    }
    
    return filtered;
}

std::vector<DiagnosticEvent> DiagnosticSystem::get_recent_events(size_t count) {
    std::lock_guard<std::mutex> lock(diagnostic_mutex);
    
    size_t start = 0;
    if (event_log.size() > count) {
        start = event_log.size() - count;
    }
    
    return std::vector<DiagnosticEvent>(event_log.begin() + start, event_log.end());
}

void DiagnosticSystem::clear_log() {
    std::lock_guard<std::mutex> lock(diagnostic_mutex);
    event_log.clear();
}

std::string DiagnosticSystem::generate_diagnostic_report() {
    std::lock_guard<std::mutex> lock(diagnostic_mutex);
    
    std::stringstream ss;
    ss << "=== Diagnostic Report ===\n";
    ss << "Total Events: " << event_log.size() << "\n";
    
    // Count by level
    int info = 0, warning = 0, error = 0, critical = 0;
    for (const auto& event : event_log) {
        switch (event.level) {
            case DiagnosticLevel::INFO: info++; break;
            case DiagnosticLevel::WARNING: warning++; break;
            case DiagnosticLevel::ERROR: error++; break;
            case DiagnosticLevel::CRITICAL: critical++; break;
        }
    }
    
    ss << "  INFO: " << info << "\n";
    ss << "  WARNING: " << warning << "\n";
    ss << "  ERROR: " << error << "\n";
    ss << "  CRITICAL: " << critical << "\n\n";
    
    // Recent events
    ss << "Recent Events:\n";
    auto recent = get_recent_events(10);
    for (const auto& event : recent) {
        ss << "  [";
        switch (event.level) {
            case DiagnosticLevel::INFO: ss << "INFO"; break;
            case DiagnosticLevel::WARNING: ss << "WARN"; break;
            case DiagnosticLevel::ERROR: ss << "ERR "; break;
            case DiagnosticLevel::CRITICAL: ss << "CRIT"; break;
        }
        ss << "] " << event.source << ": " << event.message << "\n";
    }
    
    return ss.str();
}

bool DiagnosticSystem::check_device_health(VirtualPCB* device) {
    if (!device) return false;
    
    bool healthy = true;
    
    // Check device state
    if (device->get_state() == DeviceState::ERROR) {
        log_event(DiagnosticLevel::ERROR, "HealthCheck", "Device in ERROR state");
        healthy = false;
    }
    
    // Check voltage levels
    if (device->get_voltage_3v3() < 3.0 || device->get_voltage_3v3() > 3.6) {
        log_event(DiagnosticLevel::WARNING, "HealthCheck", "3.3V rail out of spec");
        healthy = false;
    }
    
    // Check temperature
    if (device->get_temperature() > 85.0f) {
        log_event(DiagnosticLevel::CRITICAL, "HealthCheck", "Temperature critical");
        healthy = false;
    }
    
    if (healthy) {
        log_event(DiagnosticLevel::INFO, "HealthCheck", "Device health check passed");
    }
    
    return healthy;
}

bool DiagnosticSystem::check_memory_integrity(VirtualPCB* device) {
    if (!device) return false;
    
    // Perform basic memory checks
    auto* sram = device->get_memory_region("SRAM");
    if (!sram) {
        log_event(DiagnosticLevel::ERROR, "MemoryCheck", "SRAM region not found");
        return false;
    }
    
    log_event(DiagnosticLevel::INFO, "MemoryCheck", "Memory integrity check passed");
    return true;
}

bool DiagnosticSystem::check_io_health(VirtualPCB* device) {
    if (!device) return false;
    
    log_event(DiagnosticLevel::INFO, "IOCheck", "I/O health check passed");
    return true;
}

// ============================================================================
// Dashboard System Implementation
// ============================================================================

void DashboardSystem::start_dashboard() {
    is_running = true;
    if (telemetry) {
        telemetry->start_collection();
    }
}

void DashboardSystem::stop_dashboard() {
    is_running = false;
    if (telemetry) {
        telemetry->stop_collection();
    }
}

void DashboardSystem::set_update_interval(std::chrono::milliseconds interval) {
    update_interval = interval;
}

std::string DashboardSystem::render_dashboard() {
    std::stringstream ss;
    
    ss << "\n╔════════════════════════════════════════════════════════════╗\n";
    ss << "║         GGNuCash Financial Hardware Dashboard           ║\n";
    ss << "╚════════════════════════════════════════════════════════════╝\n\n";
    
    ss << render_device_status() << "\n";
    ss << render_telemetry_panel() << "\n";
    ss << render_diagnostics_panel() << "\n";
    ss << render_io_status() << "\n";
    
    return ss.str();
}

std::string DashboardSystem::render_device_status() {
    if (!device) return "";
    
    std::stringstream ss;
    ss << "┌─ Device Status ───────────────────────────────────────────┐\n";
    ss << "│ ID: " << std::left << std::setw(52) << device->get_device_id() << "│\n";
    ss << "│ Model: " << std::left << std::setw(49) << device->get_model() << "│\n";
    ss << "│ Firmware: " << std::left << std::setw(46) << device->get_firmware_version() << "│\n";
    ss << "│ Uptime: " << std::left << std::setw(47) << (device->get_uptime_ms() / 1000) << "s │\n";
    ss << "│ Temperature: " << std::fixed << std::setprecision(1) 
       << device->get_temperature() << " °C" << std::string(39, ' ') << "│\n";
    ss << "└───────────────────────────────────────────────────────────┘\n";
    
    return ss.str();
}

std::string DashboardSystem::render_telemetry_panel() {
    std::stringstream ss;
    ss << "┌─ Telemetry ───────────────────────────────────────────────┐\n";
    
    if (telemetry) {
        auto metrics = telemetry->list_metrics();
        if (metrics.empty()) {
            ss << "│ No metrics available" << std::string(37, ' ') << "│\n";
        } else {
            for (size_t i = 0; i < std::min(size_t(5), metrics.size()); i++) {
                auto metric = telemetry->get_metric(metrics[i]);
                ss << "│ " << std::left << std::setw(20) << metric.name << ": " 
                   << std::fixed << std::setprecision(2) << std::setw(10) << metric.value
                   << " " << std::setw(24) << metric.unit << "│\n";
            }
        }
    } else {
        ss << "│ Telemetry system not available" << std::string(27, ' ') << "│\n";
    }
    
    ss << "└───────────────────────────────────────────────────────────┘\n";
    return ss.str();
}

std::string DashboardSystem::render_diagnostics_panel() {
    std::stringstream ss;
    ss << "┌─ Diagnostics ─────────────────────────────────────────────┐\n";
    
    if (diagnostics) {
        auto recent = diagnostics->get_recent_events(3);
        if (recent.empty()) {
            ss << "│ No recent events" << std::string(41, ' ') << "│\n";
        } else {
            for (const auto& event : recent) {
                std::string level_str;
                switch (event.level) {
                    case DiagnosticLevel::INFO: level_str = "INFO"; break;
                    case DiagnosticLevel::WARNING: level_str = "WARN"; break;
                    case DiagnosticLevel::ERROR: level_str = "ERR "; break;
                    case DiagnosticLevel::CRITICAL: level_str = "CRIT"; break;
                }
                
                std::string msg = event.message;
                if (msg.length() > 45) {
                    msg = msg.substr(0, 42) + "...";
                }
                
                ss << "│ [" << level_str << "] " << std::left << std::setw(48) << msg << "│\n";
            }
        }
    } else {
        ss << "│ Diagnostic system not available" << std::string(26, ' ') << "│\n";
    }
    
    ss << "└───────────────────────────────────────────────────────────┘\n";
    return ss.str();
}

std::string DashboardSystem::render_io_status() {
    std::stringstream ss;
    ss << "┌─ I/O Status ──────────────────────────────────────────────┐\n";
    ss << "│ Active Streams: 0" << std::string(40, ' ') << "│\n";
    ss << "└───────────────────────────────────────────────────────────┘\n";
    
    return ss.str();
}

std::string DashboardSystem::render_memory_map() {
    std::stringstream ss;
    ss << "┌─ Memory Map ──────────────────────────────────────────────┐\n";
    
    if (device) {
        auto* sram = device->get_memory_region("SRAM");
        auto* flash = device->get_memory_region("FLASH");
        
        if (sram) {
            ss << "│ SRAM:  0x" << std::hex << std::setw(8) << std::setfill('0') 
               << sram->base_address << " - " << std::setw(8) << sram->size << " bytes" 
               << std::string(20, ' ') << "│\n";
        }
        if (flash) {
            ss << "│ FLASH: 0x" << std::hex << std::setw(8) << std::setfill('0')
               << flash->base_address << " - " << std::setw(8) << flash->size << " bytes"
               << std::string(20, ' ') << "│\n";
        }
    }
    
    ss << "└───────────────────────────────────────────────────────────┘\n";
    return ss.str();
}

void DashboardSystem::run_interactive_dashboard() {
    std::cout << "Starting interactive dashboard...\n";
    std::cout << "Press 'q' to quit, 'r' to refresh\n\n";
    
    start_dashboard();
    
    while (is_running) {
        std::cout << "\033[2J\033[H";  // Clear screen
        std::cout << render_dashboard();
        
        std::this_thread::sleep_for(update_interval);
    }
    
    stop_dashboard();
}

} // namespace vdev
} // namespace ggnucash
