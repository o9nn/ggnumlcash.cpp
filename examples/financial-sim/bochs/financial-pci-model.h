#pragma once

#include <array>
#include <cstdint>

#include "include/ggnucash/financial_pci.h"

namespace ggnucash {
namespace bochs {

class FinancialPciModel {
public:
    FinancialPciModel();

    uint32_t read(uint32_t offset, unsigned width = 4) const;
    void write(uint32_t offset, uint32_t value, unsigned width = 4);
    void reset();

private:
    void submit();
    uint32_t status;
    uint32_t length;
    uint32_t error;
    uint32_t transaction_count;
    std::array<uint8_t, GGNUCASH_PCI_MAX_RECORD_SIZE> data;
};

} // namespace bochs
} // namespace ggnucash
