#include "virtual-device.h"
#include "financial-device-driver.h"
#include <iostream>

using namespace ggnucash::vdev;

int main() {
    std::cout << "=== Virtual Device Test ===\n";
    
    // Create device
    std::cout << "Creating virtual PCB...\n";
    auto device = std::make_shared<VirtualPCB>("TEST-001", "TestModel");
    
    // Initialize
    std::cout << "Initializing device...\n";
    if (!device->initialize()) {
        std::cerr << "Failed to initialize\n";
        return 1;
    }
    
    // Start
    std::cout << "Starting device...\n";
    if (!device->start()) {
        std::cerr << "Failed to start\n";
        return 1;
    }
    
    // Show status
    std::cout << "\n" << device->get_status_report() << "\n";
    
    // Create and load driver
    std::cout << "\nLoading financial driver...\n";
    auto driver = std::make_shared<FinancialDeviceDriver>();
    
    if (!driver->load(device.get())) {
        std::cerr << "Failed to load driver\n";
        return 1;
    }
    
    if (!driver->initialize()) {
        std::cerr << "Failed to initialize driver\n";
        return 1;
    }
    
    // Run self-test
    std::cout << "\nRunning self-test...\n";
    driver->run_self_test();
    
    // Show diagnostics
    std::cout << "\n" << driver->get_hardware_diagnostics() << "\n";
    
    std::cout << "\n✓ All tests passed!\n";
    
    return 0;
}
