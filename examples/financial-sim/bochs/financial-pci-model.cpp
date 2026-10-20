#include "financial-pci-model.h"

#include <cstring>
#include <limits>

namespace ggnucash {
namespace bochs {

namespace {

uint32_t load_le32(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t load_le64(const uint8_t *bytes)
{
    return static_cast<uint64_t>(load_le32(bytes)) |
           (static_cast<uint64_t>(load_le32(bytes + 4)) << 32);
}

int64_t signed_le64(const uint8_t *bytes)
{
    const uint64_t value = load_le64(bytes);
    int64_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

} // namespace

FinancialPciModel::FinancialPciModel()
{
    reset();
}

uint32_t FinancialPciModel::read(uint32_t offset, unsigned width) const
{
    if (width != 4 || (offset & 3) != 0 || offset > GGNUCASH_PCI_BAR_SIZE - 4) {
        return 0xffffffff;
    }

    switch (offset) {
        case GGNUCASH_PCI_REG_ID:
            return GGNUCASH_PCI_ID;
        case GGNUCASH_PCI_REG_ABI_VERSION:
            return GGNUCASH_PCI_ABI_VERSION;
        case GGNUCASH_PCI_REG_CAPABILITIES:
            return GGNUCASH_PCI_CAP_PIO_TX;
        case GGNUCASH_PCI_REG_STATUS:
            return status;
        case GGNUCASH_PCI_REG_LENGTH:
            return length;
        case GGNUCASH_PCI_REG_TX_COUNT:
            return transaction_count;
        case GGNUCASH_PCI_REG_ERROR:
            return error;
        default:
            if (offset >= GGNUCASH_PCI_REG_DATA &&
                offset < GGNUCASH_PCI_REG_DATA + GGNUCASH_PCI_MAX_RECORD_SIZE) {
                return load_le32(data.data() + offset - GGNUCASH_PCI_REG_DATA);
            }
            return 0;
    }
}

void FinancialPciModel::write(uint32_t offset, uint32_t value, unsigned width)
{
    if (width != 4 || (offset & 3) != 0 || offset > GGNUCASH_PCI_BAR_SIZE - 4) {
        return;
    }

    switch (offset) {
        case GGNUCASH_PCI_REG_COMMAND:
            if (value == GGNUCASH_PCI_COMMAND_RESET) {
                reset();
            } else if (value == GGNUCASH_PCI_COMMAND_SUBMIT) {
                submit();
            } else {
                error = GGNUCASH_PCI_ERROR_BAD_COMMAND;
                status = GGNUCASH_PCI_STATUS_READY | GGNUCASH_PCI_STATUS_ERROR;
            }
            break;
        case GGNUCASH_PCI_REG_LENGTH:
            length = value;
            break;
        case GGNUCASH_PCI_REG_ERROR:
            if (value == GGNUCASH_PCI_ERROR_NONE) {
                error = GGNUCASH_PCI_ERROR_NONE;
                status = GGNUCASH_PCI_STATUS_READY;
            }
            break;
        default:
            if (offset >= GGNUCASH_PCI_REG_DATA &&
                offset < GGNUCASH_PCI_REG_DATA + GGNUCASH_PCI_MAX_RECORD_SIZE) {
                const uint32_t index = offset - GGNUCASH_PCI_REG_DATA;
                data[index] = static_cast<uint8_t>(value);
                data[index + 1] = static_cast<uint8_t>(value >> 8);
                data[index + 2] = static_cast<uint8_t>(value >> 16);
                data[index + 3] = static_cast<uint8_t>(value >> 24);
            }
            break;
    }
}

void FinancialPciModel::reset()
{
    status = GGNUCASH_PCI_STATUS_READY;
    length = 0;
    error = GGNUCASH_PCI_ERROR_NONE;
    transaction_count = 0;
    data.fill(0);
}

void FinancialPciModel::submit()
{
    status = GGNUCASH_PCI_STATUS_BUSY;
    error = GGNUCASH_PCI_ERROR_NONE;

    if (length != sizeof(struct ggnucash_transaction_record) ||
        length > GGNUCASH_PCI_MAX_RECORD_SIZE) {
        error = GGNUCASH_PCI_ERROR_BAD_LENGTH;
    } else {
        struct ggnucash_transaction_record record = {};
        record.version = load_le32(data.data());
        record.size = load_le32(data.data() + 4);
        record.transaction_id = load_le64(data.data() + 8);
        record.debit_account = load_le32(data.data() + 16);
        record.credit_account = load_le32(data.data() + 20);
        const uint64_t amount_bits = load_le64(data.data() + 24);
        std::memcpy(&record.amount_minor, &amount_bits, sizeof(record.amount_minor));
        record.currency_numeric = load_le32(data.data() + 32);
        record.reserved = load_le32(data.data() + 36);

        if (!ggnucash_transaction_record_valid(&record)) {
            error = GGNUCASH_PCI_ERROR_BAD_RECORD;
        } else if (transaction_count == std::numeric_limits<uint32_t>::max()) {
            error = GGNUCASH_PCI_ERROR_COUNT_OVERFLOW;
        } else {
            ++transaction_count;
        }
    }

    status = GGNUCASH_PCI_STATUS_READY;
    if (error != GGNUCASH_PCI_ERROR_NONE) {
        status |= GGNUCASH_PCI_STATUS_ERROR;
    }
}

} // namespace bochs
} // namespace ggnucash
