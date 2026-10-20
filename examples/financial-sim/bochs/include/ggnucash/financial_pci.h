#ifndef GGNUCASH_FINANCIAL_PCI_H
#define GGNUCASH_FINANCIAL_PCI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define GGNUCASH_PCI_VENDOR_ID       0x1234
#define GGNUCASH_PCI_DEVICE_ID       0x4747

#define GGNUCASH_PCI_ABI_VERSION     1
#define GGNUCASH_PCI_BAR             0
#define GGNUCASH_PCI_BAR_SIZE        0x120
#define GGNUCASH_PCI_MAX_RECORD_SIZE 0x100

#define GGNUCASH_PCI_REG_ID          0x00
#define GGNUCASH_PCI_REG_ABI_VERSION 0x04
#define GGNUCASH_PCI_REG_CAPABILITIES 0x08
#define GGNUCASH_PCI_REG_STATUS      0x0c
#define GGNUCASH_PCI_REG_COMMAND     0x10
#define GGNUCASH_PCI_REG_LENGTH      0x14
#define GGNUCASH_PCI_REG_TX_COUNT    0x18
#define GGNUCASH_PCI_REG_ERROR       0x1c
#define GGNUCASH_PCI_REG_DATA        0x20

#define GGNUCASH_PCI_ID              0x434e4747

#define GGNUCASH_PCI_CAP_PIO_TX      (1U << 0)

#define GGNUCASH_PCI_STATUS_READY    (1U << 0)
#define GGNUCASH_PCI_STATUS_BUSY     (1U << 1)
#define GGNUCASH_PCI_STATUS_ERROR    (1U << 2)

#define GGNUCASH_PCI_COMMAND_SUBMIT  1
#define GGNUCASH_PCI_COMMAND_RESET   2

#define GGNUCASH_PCI_ERROR_NONE              0
#define GGNUCASH_PCI_ERROR_BAD_COMMAND       1
#define GGNUCASH_PCI_ERROR_BAD_LENGTH        2
#define GGNUCASH_PCI_ERROR_BAD_RECORD        3
#define GGNUCASH_PCI_ERROR_COUNT_OVERFLOW    4

struct ggnucash_transaction_record {
    __u32 version;
    __u32 size;
    __u64 transaction_id;
    __u32 debit_account;
    __u32 credit_account;
    __s64 amount_minor;
    __u32 currency_numeric;
    __u32 reserved;
};

struct ggnucash_device_info {
    __u32 abi_version;
    __u32 capabilities;
    __u32 status;
    __u32 error;
    __u64 transaction_count;
};

struct ggnucash_submit {
    struct ggnucash_transaction_record record;
    __u32 status;
    __u32 error;
    __u64 transaction_count;
};

#define GGNUCASH_IOCTL_TYPE         'G'
#define GGNUCASH_IOCTL_GET_INFO     _IOR(GGNUCASH_IOCTL_TYPE, 0x00, struct ggnucash_device_info)
#define GGNUCASH_IOCTL_SUBMIT       _IOWR(GGNUCASH_IOCTL_TYPE, 0x01, struct ggnucash_submit)

static inline int ggnucash_transaction_record_valid(const struct ggnucash_transaction_record *record)
{
    return record != 0 &&
           record->version == GGNUCASH_PCI_ABI_VERSION &&
           record->size == sizeof(*record) &&
           record->transaction_id != 0 &&
           record->debit_account != 0 &&
           record->credit_account != 0 &&
           record->debit_account != record->credit_account &&
           record->amount_minor > 0 &&
           record->currency_numeric > 0 &&
           record->currency_numeric <= 999 &&
           record->reserved == 0;
}

#endif
