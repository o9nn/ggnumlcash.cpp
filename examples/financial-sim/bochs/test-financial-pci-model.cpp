#include "financial-pci-model.h"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void write_record(ggnucash::bochs::FinancialPciModel &device,
                  const ggnucash_transaction_record &record)
{
    const uint32_t words[] = {
        record.version,
        record.size,
        static_cast<uint32_t>(record.transaction_id),
        static_cast<uint32_t>(record.transaction_id >> 32),
        record.debit_account,
        record.credit_account,
        static_cast<uint32_t>(record.amount_minor),
        static_cast<uint32_t>(static_cast<uint64_t>(record.amount_minor) >> 32),
        record.currency_numeric,
        record.reserved,
    };

    device.write(GGNUCASH_PCI_REG_LENGTH, sizeof(record));
    for (uint32_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        device.write(GGNUCASH_PCI_REG_DATA + i * 4, words[i]);
    }
}

} // namespace

int main()
{
    using ggnucash::bochs::FinancialPciModel;
    FinancialPciModel device;

    expect(device.read(GGNUCASH_PCI_REG_ID) == GGNUCASH_PCI_ID, "device ID");
    expect(device.read(GGNUCASH_PCI_REG_ABI_VERSION) == GGNUCASH_PCI_ABI_VERSION, "ABI version");
    expect(device.read(GGNUCASH_PCI_REG_CAPABILITIES) == GGNUCASH_PCI_CAP_PIO_TX, "PIO capability");
    expect(device.read(GGNUCASH_PCI_REG_STATUS) == GGNUCASH_PCI_STATUS_READY, "initial ready status");
    expect(device.read(GGNUCASH_PCI_REG_ID, 2) == 0xffffffff, "reject non-dword reads");

    ggnucash_transaction_record record = {};
    record.version = GGNUCASH_PCI_ABI_VERSION;
    record.size = sizeof(record);
    record.transaction_id = 1;
    record.debit_account = 1101;
    record.credit_account = 4100;
    record.amount_minor = 25000;
    record.currency_numeric = 840;

    write_record(device, record);
    device.write(GGNUCASH_PCI_REG_COMMAND, GGNUCASH_PCI_COMMAND_SUBMIT);
    expect(device.read(GGNUCASH_PCI_REG_TX_COUNT) == 1, "valid transaction increments count");
    expect(device.read(GGNUCASH_PCI_REG_STATUS) == GGNUCASH_PCI_STATUS_READY, "successful submit completes");

    device.write(GGNUCASH_PCI_REG_LENGTH, sizeof(record) - 4);
    device.write(GGNUCASH_PCI_REG_COMMAND, GGNUCASH_PCI_COMMAND_SUBMIT);
    expect(device.read(GGNUCASH_PCI_REG_ERROR) == GGNUCASH_PCI_ERROR_BAD_LENGTH, "bad length is reported");
    expect(device.read(GGNUCASH_PCI_REG_STATUS) ==
           (GGNUCASH_PCI_STATUS_READY | GGNUCASH_PCI_STATUS_ERROR), "bad length sets error status");
    expect(device.read(GGNUCASH_PCI_REG_TX_COUNT) == 1, "invalid transaction does not increment count");

    write_record(device, record);
    device.write(GGNUCASH_PCI_REG_DATA + 20, record.debit_account);
    device.write(GGNUCASH_PCI_REG_COMMAND, GGNUCASH_PCI_COMMAND_SUBMIT);
    expect(device.read(GGNUCASH_PCI_REG_ERROR) == GGNUCASH_PCI_ERROR_BAD_RECORD, "invalid record is rejected");

    device.write(GGNUCASH_PCI_REG_COMMAND, 0xffffffff);
    expect(device.read(GGNUCASH_PCI_REG_ERROR) == GGNUCASH_PCI_ERROR_BAD_COMMAND, "unknown command is reported");

    device.write(GGNUCASH_PCI_REG_COMMAND, GGNUCASH_PCI_COMMAND_RESET);
    expect(device.read(GGNUCASH_PCI_REG_TX_COUNT) == 0, "reset clears transaction count");
    expect(device.read(GGNUCASH_PCI_REG_ERROR) == GGNUCASH_PCI_ERROR_NONE, "reset clears errors");
    expect(device.read(GGNUCASH_PCI_REG_STATUS) == GGNUCASH_PCI_STATUS_READY, "reset returns ready state");

    std::cout << "Financial PCI model tests passed\n";
    return 0;
}
