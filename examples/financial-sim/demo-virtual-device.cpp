#include "virtual-device.h"
#include "financial-device-driver.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

using namespace ggnucash::vdev;

// Demo: Complete workflow showing virtual device capabilities
void run_comprehensive_demo() {
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  GGNuCash Virtual Device - Comprehensive Demonstration  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    
    // ========================================================================
    // Phase 1: Device Initialization
    // ========================================================================
    std::cout << "PHASE 1: Device Initialization\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    
    auto device = std::make_shared<VirtualPCB>("GGNC-DEMO-001", "GGNuCash-X1-Pro");
    std::cout << "✓ Created virtual PCB: " << device->get_device_id() << "\n";
    
    device->initialize();
    std::cout << "✓ Initialized hardware\n";
    
    device->start();
    std::cout << "✓ Device started and running\n";
    std::cout << "  Uptime: " << device->get_uptime_ms() << "ms\n";
    std::cout << "  Temperature: " << device->get_temperature() << "°C\n";
    std::cout << "  Power: " << device->get_voltage_3v3() << "V / " 
              << device->get_voltage_5v() << "V\n\n";
    
    // ========================================================================
    // Phase 2: Driver Loading and Configuration
    // ========================================================================
    std::cout << "PHASE 2: Driver Loading\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    
    auto driver = std::make_shared<FinancialDeviceDriver>();
    driver->load(device.get());
    driver->initialize();
    std::cout << "✓ Loaded " << driver->get_name() << " v" << driver->get_version() << "\n";
    std::cout << "✓ Mapped 15 financial accounts to GPIO pins\n\n";
    
    // Register with global registry
    auto* registry = DeviceRegistry::get_instance();
    registry->register_device(device->get_device_id(), device);
    registry->register_driver(driver->get_name(), driver);
    std::cout << "✓ Registered with device registry\n\n";
    
    // ========================================================================
    // Phase 3: Telemetry and Monitoring Setup
    // ========================================================================
    std::cout << "PHASE 3: Telemetry and Monitoring\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    
    TelemetrySystem telemetry;
    telemetry.add_metric("voltage_3v3", "V");
    telemetry.add_metric("temperature", "°C");
    telemetry.add_metric("total_assets", "USD");
    telemetry.add_metric("total_liabilities", "USD");
    telemetry.add_metric("net_worth", "USD");
    telemetry.start_collection();
    std::cout << "✓ Telemetry system active with 5 metrics\n\n";
    
    DiagnosticSystem diagnostics;
    diagnostics.log_event(DiagnosticLevel::INFO, "Demo", "System initialized successfully");
    std::cout << "✓ Diagnostic system ready\n\n";
    
    // ========================================================================
    // Phase 4: Financial Transaction Simulation
    // ========================================================================
    std::cout << "PHASE 4: Financial Transaction Simulation\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    
    // Transaction 1: Initial capital investment
    std::cout << "Transaction 1: Owner Investment\n";
    driver->update_account_balance("1101", 100000.0);  // Cash
    driver->update_account_balance("3100", 100000.0);  // Owner's Equity
    
    double cash = driver->read_account_balance("1101");
    std::cout << "  Cash (GPIO0): $" << std::fixed << std::setprecision(2) << cash << "\n";
    std::cout << "  Pin voltage: " << (device->get_analog_value(0) / 4095.0 * 3.3) << "V\n";
    
    telemetry.update_metric("total_assets", 100000.0);
    telemetry.update_metric("total_liabilities", 0.0);
    telemetry.update_metric("net_worth", 100000.0);
    diagnostics.log_event(DiagnosticLevel::INFO, "Transaction", "Initial investment recorded");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    device->update();
    std::cout << "\n";
    
    // Transaction 2: Equipment purchase
    std::cout << "Transaction 2: Equipment Purchase\n";
    driver->update_account_balance("1101", 85000.0);   // Cash reduced
    driver->update_account_balance("1201", 15000.0);   // Equipment acquired
    
    std::cout << "  Cash (GPIO0): $" << driver->read_account_balance("1101") << "\n";
    std::cout << "  Equipment (GPIO3): $" << driver->read_account_balance("1201") << "\n";
    
    telemetry.update_metric("total_assets", 100000.0); // Still same total
    diagnostics.log_event(DiagnosticLevel::INFO, "Transaction", "Equipment purchased");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    device->update();
    std::cout << "\n";
    
    // Transaction 3: Service revenue
    std::cout << "Transaction 3: Service Revenue\n";
    driver->update_account_balance("1101", 97500.0);   // Cash increased
    driver->update_account_balance("4200", 12500.0);   // Service revenue
    
    std::cout << "  Cash (GPIO0): $" << driver->read_account_balance("1101") << "\n";
    std::cout << "  Service Revenue (GPIO41): $" << driver->read_account_balance("4200") << "\n";
    
    telemetry.update_metric("total_assets", 112500.0);
    telemetry.update_metric("net_worth", 112500.0);
    diagnostics.log_event(DiagnosticLevel::INFO, "Transaction", "Service revenue recorded");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    device->update();
    std::cout << "\n";
    
    // Transaction 4: Operating expenses
    std::cout << "Transaction 4: Operating Expenses\n";
    driver->update_account_balance("1101", 93000.0);   // Cash reduced
    driver->update_account_balance("5101", 3000.0);    // Salaries
    driver->update_account_balance("5102", 1000.0);    // Rent
    driver->update_account_balance("5103", 500.0);     // Utilities
    
    std::cout << "  Cash (GPIO0): $" << driver->read_account_balance("1101") << "\n";
    std::cout << "  Salaries (GPIO48): $" << driver->read_account_balance("5101") << "\n";
    std::cout << "  Rent (GPIO49): $" << driver->read_account_balance("5102") << "\n";
    std::cout << "  Utilities (GPIO50): $" << driver->read_account_balance("5103") << "\n";
    
    telemetry.update_metric("total_assets", 108000.0);
    telemetry.update_metric("net_worth", 108000.0);
    diagnostics.log_event(DiagnosticLevel::INFO, "Transaction", "Operating expenses recorded");
    
    device->update();
    std::cout << "\n";
    
    // ========================================================================
    // Phase 5: Hardware Diagnostics
    // ========================================================================
    std::cout << "PHASE 5: Hardware Diagnostics\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    
    driver->run_self_test();
    std::cout << "\n";
    
    // ========================================================================
    // Phase 6: Health Checks
    // ========================================================================
    std::cout << "PHASE 6: System Health Check\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    
    bool device_healthy = diagnostics.check_device_health(device.get());
    bool memory_healthy = diagnostics.check_memory_integrity(device.get());
    bool io_healthy = diagnostics.check_io_health(device.get());
    
    std::cout << "✓ Device Health: " << (device_healthy ? "PASS" : "FAIL") << "\n";
    std::cout << "✓ Memory Integrity: " << (memory_healthy ? "PASS" : "FAIL") << "\n";
    std::cout << "✓ I/O Health: " << (io_healthy ? "PASS" : "FAIL") << "\n\n";
    
    // ========================================================================
    // Phase 7: Memory Inspection
    // ========================================================================
    std::cout << "PHASE 7: Memory Inspection\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    
    auto* sram = device->get_memory_region("SRAM");
    auto* flash = device->get_memory_region("FLASH");
    
    std::cout << "Memory Regions:\n";
    std::cout << "  SRAM: 0x" << std::hex << sram->base_address << std::dec 
              << " (" << (sram->size / 1024) << " KB)\n";
    std::cout << "  FLASH: 0x" << std::hex << flash->base_address << std::dec
              << " (" << (flash->size / 1024) << " KB)\n\n";
    
    // Write and read test
    sram->write_dword(0x100, 0xDEADBEEF);
    uint32_t value = sram->read_dword(0x100);
    std::cout << "Memory Test: Write 0xDEADBEEF, Read 0x" 
              << std::hex << value << std::dec << " ✓\n\n";
    
    // ========================================================================
    // Phase 8: Telemetry Report
    // ========================================================================
    std::cout << "PHASE 8: Telemetry Report\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    
    telemetry.update_metric("voltage_3v3", device->get_voltage_3v3());
    telemetry.update_metric("temperature", device->get_temperature());
    
    auto metrics = telemetry.list_metrics();
    std::cout << "Collected " << metrics.size() << " metrics:\n\n";
    
    for (const auto& name : metrics) {
        auto metric = telemetry.get_metric(name);
        std::cout << "  " << std::left << std::setw(20) << metric.name << ": ";
        std::cout << std::fixed << std::setprecision(2) << std::setw(10) << metric.value;
        std::cout << " " << metric.unit;
        std::cout << " (avg: " << metric.avg_value << ")\n";
    }
    std::cout << "\n";
    
    // ========================================================================
    // Phase 9: Diagnostic Events
    // ========================================================================
    std::cout << "PHASE 9: Diagnostic Events\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    
    auto events = diagnostics.get_recent_events(10);
    std::cout << "Recent Events (" << events.size() << "):\n";
    
    for (const auto& event : events) {
        std::string level;
        switch (event.level) {
            case DiagnosticLevel::INFO: level = "INFO "; break;
            case DiagnosticLevel::WARNING: level = "WARN "; break;
            case DiagnosticLevel::ERROR: level = "ERROR"; break;
            case DiagnosticLevel::CRITICAL: level = "CRIT "; break;
        }
        
        std::cout << "  [" << level << "] " << std::left << std::setw(12) 
                  << event.source << ": " << event.message << "\n";
    }
    std::cout << "\n";
    
    // ========================================================================
    // Phase 10: Final Status
    // ========================================================================
    std::cout << "PHASE 10: Final Status\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    std::cout << device->get_status_report();
    std::cout << "\n";
    
    std::cout << driver->get_hardware_diagnostics();
    
    // ========================================================================
    // Shutdown
    // ========================================================================
    std::cout << "\nShutting down device...\n";
    device->stop();
    device->shutdown();
    
    diagnostics.log_event(DiagnosticLevel::INFO, "Demo", "Demonstration completed successfully");
    
    std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║            Demonstration Completed Successfully         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
}

int main() {
    try {
        run_comprehensive_demo();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
