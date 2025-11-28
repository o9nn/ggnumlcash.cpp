#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <queue>
#include <chrono>
#include <mutex>
#include <condition_variable>

// ============================================================================
// GGNuCash Virtual Device Architecture
// ============================================================================
// This header defines a complete virtual hardware platform for the GGNuCash
// Financial Hardware Platform, modeling physical PCB components, interfaces,
// ports, and firmware as if it were a real physical device.
// ============================================================================

namespace ggnucash {
namespace vdev {

// Forward declarations
class VirtualPCB;
class DeviceDriver;
class IOStream;
class FirmwareController;

// ============================================================================
// Hardware Pin and Port Definitions
// ============================================================================

enum class PinMode {
    INPUT,
    OUTPUT,
    INPUT_PULLUP,
    INPUT_PULLDOWN,
    ANALOG_INPUT,
    PWM_OUTPUT,
    INTERRUPT
};

enum class PinState {
    LOW = 0,
    HIGH = 1,
    FLOATING,
    ANALOG
};

struct Pin {
    uint32_t number;
    std::string name;
    PinMode mode;
    PinState state;
    uint16_t analog_value;  // 0-4095 (12-bit ADC)
    bool interrupt_enabled;
    std::function<void()> interrupt_handler;
    
    Pin() : number(0), mode(PinMode::INPUT), state(PinState::FLOATING), 
            analog_value(0), interrupt_enabled(false) {}
};

// ============================================================================
// I/O Interface Definitions
// ============================================================================

enum class BusProtocol {
    UART,
    SPI,
    I2C,
    CAN,
    USB,
    ETHERNET,
    GPIO
};

struct UARTConfig {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    char parity;  // 'N'=none, 'E'=even, 'O'=odd
    bool flow_control;
    
    UARTConfig() : baud_rate(115200), data_bits(8), stop_bits(1), 
                   parity('N'), flow_control(false) {}
};

struct SPIConfig {
    uint32_t clock_speed;
    uint8_t mode;  // 0-3
    uint8_t bit_order;  // 0=MSB first, 1=LSB first
    uint8_t chip_select;
    
    SPIConfig() : clock_speed(1000000), mode(0), bit_order(0), chip_select(0) {}
};

struct I2CConfig {
    uint32_t clock_speed;
    uint8_t address;
    bool addressing_10bit;
    
    I2CConfig() : clock_speed(100000), address(0x00), addressing_10bit(false) {}
};

// ============================================================================
// I/O Stream Base Class
// ============================================================================

class IOStream {
protected:
    BusProtocol protocol;
    bool is_open;
    std::queue<uint8_t> rx_buffer;
    std::queue<uint8_t> tx_buffer;
    size_t rx_buffer_size;
    size_t tx_buffer_size;
    mutable std::mutex buffer_mutex;  // mutable for const member functions
    
public:
    IOStream(BusProtocol proto, size_t rx_size = 1024, size_t tx_size = 1024)
        : protocol(proto), is_open(false), 
          rx_buffer_size(rx_size), tx_buffer_size(tx_size) {}
    
    virtual ~IOStream() {}
    
    virtual bool open() = 0;
    virtual bool close() = 0;
    virtual size_t write(const uint8_t* data, size_t length) = 0;
    virtual size_t read(uint8_t* data, size_t length) = 0;
    virtual size_t available() const = 0;
    virtual bool flush() = 0;
    
    BusProtocol get_protocol() const { return protocol; }
    bool is_stream_open() const { return is_open; }
};

// ============================================================================
// UART Stream Implementation
// ============================================================================

class UARTStream : public IOStream {
private:
    UARTConfig config;
    std::string port_name;
    
public:
    UARTStream(const std::string& port = "/dev/ttyUSB0") 
        : IOStream(BusProtocol::UART), port_name(port) {}
    
    bool open() override;
    bool close() override;
    size_t write(const uint8_t* data, size_t length) override;
    size_t read(uint8_t* data, size_t length) override;
    size_t available() const override;
    bool flush() override;
    
    void configure(const UARTConfig& cfg) { config = cfg; }
    UARTConfig get_config() const { return config; }
};

// ============================================================================
// SPI Stream Implementation
// ============================================================================

class SPIStream : public IOStream {
private:
    SPIConfig config;
    std::string device_path;
    
public:
    SPIStream(const std::string& dev = "/dev/spidev0.0")
        : IOStream(BusProtocol::SPI), device_path(dev) {}
    
    bool open() override;
    bool close() override;
    size_t write(const uint8_t* data, size_t length) override;
    size_t read(uint8_t* data, size_t length) override;
    size_t available() const override;
    bool flush() override;
    
    void configure(const SPIConfig& cfg) { config = cfg; }
    SPIConfig get_config() const { return config; }
};

// ============================================================================
// I2C Stream Implementation
// ============================================================================

class I2CStream : public IOStream {
private:
    I2CConfig config;
    std::string bus_path;
    
public:
    I2CStream(const std::string& bus = "/dev/i2c-1")
        : IOStream(BusProtocol::I2C), bus_path(bus) {}
    
    bool open() override;
    bool close() override;
    size_t write(const uint8_t* data, size_t length) override;
    size_t read(uint8_t* data, size_t length) override;
    size_t available() const override;
    bool flush() override;
    
    void configure(const I2CConfig& cfg) { config = cfg; }
    I2CConfig get_config() const { return config; }
    
    // I2C specific operations
    bool write_register(uint8_t reg, uint8_t value);
    uint8_t read_register(uint8_t reg);
};

// ============================================================================
// Memory-Mapped I/O Region
// ============================================================================

struct MemoryRegion {
    uint64_t base_address;
    uint64_t size;
    std::vector<uint8_t> data;
    bool is_volatile;
    bool is_writable;
    std::string name;
    
    MemoryRegion(uint64_t addr, uint64_t sz, const std::string& n)
        : base_address(addr), size(sz), is_volatile(true), 
          is_writable(true), name(n) {
        data.resize(size, 0);
    }
    
    uint8_t read_byte(uint64_t offset);
    uint16_t read_word(uint64_t offset);
    uint32_t read_dword(uint64_t offset);
    void write_byte(uint64_t offset, uint8_t value);
    void write_word(uint64_t offset, uint16_t value);
    void write_dword(uint64_t offset, uint32_t value);
};

// ============================================================================
// DMA (Direct Memory Access) Controller
// ============================================================================

struct DMATransfer {
    uint64_t source_addr;
    uint64_t dest_addr;
    size_t length;
    bool completed;
    std::chrono::steady_clock::time_point start_time;
    
    DMATransfer() : source_addr(0), dest_addr(0), length(0), completed(false) {}
};

class DMAController {
private:
    std::vector<DMATransfer> active_transfers;
    std::mutex transfer_mutex;
    uint32_t max_channels;
    
public:
    DMAController(uint32_t channels = 8) : max_channels(channels) {}
    
    int start_transfer(uint64_t src, uint64_t dst, size_t len);
    bool is_transfer_complete(int channel);
    void abort_transfer(int channel);
    void process_transfers();
};

// ============================================================================
// Interrupt Controller
// ============================================================================

enum class InterruptType {
    HARDWARE,
    SOFTWARE,
    TIMER,
    DMA,
    IO,
    FAULT
};

struct Interrupt {
    uint32_t vector;
    InterruptType type;
    uint8_t priority;
    bool enabled;
    std::function<void()> handler;
    
    Interrupt() : vector(0), type(InterruptType::HARDWARE), 
                  priority(0), enabled(false) {}
};

class InterruptController {
private:
    std::vector<Interrupt> interrupts;
    std::queue<uint32_t> pending_interrupts;
    std::mutex interrupt_mutex;
    bool global_interrupt_enable;
    
public:
    InterruptController() : global_interrupt_enable(true) {
        interrupts.resize(256);  // Support up to 256 interrupt vectors
    }
    
    bool register_interrupt(uint32_t vector, InterruptType type, 
                           uint8_t priority, std::function<void()> handler);
    void trigger_interrupt(uint32_t vector);
    void enable_interrupt(uint32_t vector);
    void disable_interrupt(uint32_t vector);
    void enable_global_interrupts();
    void disable_global_interrupts();
    void process_interrupts();
};

// ============================================================================
// Virtual PCB (Printed Circuit Board)
// ============================================================================

enum class DeviceState {
    UNINITIALIZED,
    INITIALIZING,
    READY,
    RUNNING,
    SLEEPING,
    ERROR,
    SHUTDOWN
};

class VirtualPCB {
private:
    std::string device_id;
    std::string model_number;
    std::string firmware_version;
    DeviceState state;
    
    // Hardware components
    std::map<uint32_t, Pin> pins;
    std::vector<std::shared_ptr<MemoryRegion>> memory_regions;
    std::unique_ptr<DMAController> dma_controller;
    std::unique_ptr<InterruptController> interrupt_controller;
    
    // I/O interfaces
    std::map<std::string, std::shared_ptr<IOStream>> io_streams;
    
    // Clocks and timing
    std::chrono::steady_clock::time_point boot_time;
    std::chrono::steady_clock::time_point last_update;
    uint64_t system_ticks;
    
    // Power management
    float voltage_3v3;
    float voltage_5v;
    float current_consumption_ma;
    float temperature_celsius;
    
    std::mutex device_mutex;
    
public:
    VirtualPCB(const std::string& id, const std::string& model);
    ~VirtualPCB();
    
    // Lifecycle management
    bool initialize();
    bool start();
    bool stop();
    bool reset();
    bool shutdown();
    
    // Pin operations
    bool configure_pin(uint32_t pin_num, PinMode mode);
    bool set_pin_state(uint32_t pin_num, PinState state);
    PinState get_pin_state(uint32_t pin_num);
    bool set_analog_value(uint32_t pin_num, uint16_t value);
    uint16_t get_analog_value(uint32_t pin_num);
    
    // I/O stream management
    bool add_io_stream(const std::string& name, std::shared_ptr<IOStream> stream);
    std::shared_ptr<IOStream> get_io_stream(const std::string& name);
    
    // Memory management
    bool add_memory_region(const std::string& name, uint64_t addr, uint64_t size);
    MemoryRegion* get_memory_region(const std::string& name);
    uint8_t read_memory(uint64_t address);
    void write_memory(uint64_t address, uint8_t value);
    
    // Component access
    DMAController* get_dma_controller() { return dma_controller.get(); }
    InterruptController* get_interrupt_controller() { return interrupt_controller.get(); }
    
    // Status and monitoring
    DeviceState get_state() const { return state; }
    std::string get_device_id() const { return device_id; }
    std::string get_model() const { return model_number; }
    std::string get_firmware_version() const { return firmware_version; }
    float get_voltage_3v3() const { return voltage_3v3; }
    float get_voltage_5v() const { return voltage_5v; }
    float get_current() const { return current_consumption_ma; }
    float get_temperature() const { return temperature_celsius; }
    uint64_t get_uptime_ms() const;
    
    // Update cycle
    void update();
    
    // Diagnostic
    std::string get_status_report() const;
};

// ============================================================================
// Firmware Controller
// ============================================================================

class FirmwareController {
private:
    VirtualPCB* pcb;
    std::string firmware_path;
    bool is_loaded;
    
    // Firmware state
    std::map<std::string, std::string> config_params;
    std::vector<std::function<void()>> boot_sequence;
    
public:
    FirmwareController(VirtualPCB* board) : pcb(board), is_loaded(false) {}
    
    bool load_firmware(const std::string& path);
    bool execute_boot_sequence();
    bool configure_parameter(const std::string& param, const std::string& value);
    std::string get_parameter(const std::string& param);
    
    // Firmware operations
    void handle_watchdog_timer();
    void handle_power_state_change(DeviceState new_state);
    void handle_error_condition(const std::string& error);
};

// ============================================================================
// Device Driver Interface
// ============================================================================

class DeviceDriver {
protected:
    std::string driver_name;
    std::string driver_version;
    VirtualPCB* device;
    bool is_loaded;
    bool is_initialized;
    
public:
    DeviceDriver(const std::string& name, const std::string& version)
        : driver_name(name), driver_version(version), 
          device(nullptr), is_loaded(false), is_initialized(false) {}
    
    virtual ~DeviceDriver() {}
    
    virtual bool load(VirtualPCB* pcb) = 0;
    virtual bool initialize() = 0;
    virtual bool probe() = 0;
    virtual bool remove() = 0;
    
    std::string get_name() const { return driver_name; }
    std::string get_version() const { return driver_version; }
    bool is_driver_loaded() const { return is_loaded; }
    bool is_driver_initialized() const { return is_initialized; }
};

// ============================================================================
// Device Registry
// ============================================================================

class DeviceRegistry {
private:
    static DeviceRegistry* instance;
    std::map<std::string, std::shared_ptr<VirtualPCB>> devices;
    std::map<std::string, std::shared_ptr<DeviceDriver>> drivers;
    std::mutex registry_mutex;
    
    DeviceRegistry() {}
    
public:
    static DeviceRegistry* get_instance();
    
    bool register_device(const std::string& id, std::shared_ptr<VirtualPCB> device);
    bool unregister_device(const std::string& id);
    std::shared_ptr<VirtualPCB> get_device(const std::string& id);
    std::vector<std::string> list_devices();
    
    bool register_driver(const std::string& name, std::shared_ptr<DeviceDriver> driver);
    bool unregister_driver(const std::string& name);
    std::shared_ptr<DeviceDriver> get_driver(const std::string& name);
    std::vector<std::string> list_drivers();
};

} // namespace vdev
} // namespace ggnucash
