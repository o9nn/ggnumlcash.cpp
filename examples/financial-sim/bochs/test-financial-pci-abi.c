#include <stddef.h>
#include <stdio.h>

#include "include/ggnucash/financial_pci.h"

_Static_assert(sizeof(struct ggnucash_transaction_record) == 40, "transaction ABI size changed");
_Static_assert(offsetof(struct ggnucash_transaction_record, transaction_id) == 8,
               "transaction_id ABI offset changed");
_Static_assert(offsetof(struct ggnucash_transaction_record, amount_minor) == 24,
               "amount_minor ABI offset changed");
_Static_assert(sizeof(struct ggnucash_device_info) == 24, "device info ABI size changed");
_Static_assert(sizeof(struct ggnucash_submit) == 56, "submit ioctl ABI size changed");

int main(void)
{
    struct ggnucash_transaction_record record = {
        .version = GGNUCASH_PCI_ABI_VERSION,
        .size = sizeof(record),
        .transaction_id = 1,
        .debit_account = 1101,
        .credit_account = 4100,
        .amount_minor = 100,
        .currency_numeric = 840,
    };

    if (!ggnucash_transaction_record_valid(&record)) {
        return 1;
    }
    record.credit_account = record.debit_account;
    if (ggnucash_transaction_record_valid(&record)) {
        return 1;
    }
    puts("Financial PCI ABI tests passed");
    return 0;
}
