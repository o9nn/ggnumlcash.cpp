#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ggnucash/financial_pci.h"

static int parse_u32(const char *text, __u32 *value)
{
    char *end = NULL;
    unsigned long parsed;

    if (text[0] == '-') {
        return -1;
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno || end == text || *end != '\0' || parsed > UINT32_MAX) {
        return -1;
    }
    *value = (__u32) parsed;
    return 0;
}

static int parse_u64(const char *text, __u64 *value)
{
    char *end = NULL;
    unsigned long long parsed;

    if (text[0] == '-') {
        return -1;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || end == text || *end != '\0' || parsed == 0) {
        return -1;
    }
    *value = (__u64) parsed;
    return 0;
}

static int parse_amount(const char *text, __s64 *value)
{
    char *end = NULL;
    long long parsed;

    errno = 0;
    parsed = strtoll(text, &end, 10);
    if (errno || end == text || *end != '\0' || parsed <= 0) {
        return -1;
    }
    *value = (__s64) parsed;
    return 0;
}

static int show_info(int fd)
{
    struct ggnucash_device_info info;

    if (ioctl(fd, GGNUCASH_IOCTL_GET_INFO, &info) < 0) {
        perror("GGNUCASH_IOCTL_GET_INFO");
        return 1;
    }
    printf("ABI: %u\nCapabilities: 0x%08x\nStatus: 0x%08x\n"
           "Error: %u\nAccepted transactions: %" PRIu64 "\n",
           info.abi_version, info.capabilities, info.status, info.error,
           (uint64_t) info.transaction_count);
    return 0;
}

static int submit_transaction(int fd, int argc, char **argv)
{
    struct ggnucash_submit request = { 0 };

    if (argc != 8 ||
        parse_u64(argv[3], &request.record.transaction_id) ||
        parse_u32(argv[4], &request.record.debit_account) ||
        parse_u32(argv[5], &request.record.credit_account) ||
        parse_amount(argv[6], &request.record.amount_minor)) {
        fprintf(stderr, "usage: %s submit DEVICE TX_ID DEBIT CREDIT AMOUNT_MINOR CURRENCY_NUMERIC\n",
                argv[0]);
        return 2;
    }
    if (parse_u32(argv[7], &request.record.currency_numeric)) {
        fprintf(stderr, "invalid numeric currency code\n");
        return 2;
    }

    request.record.version = GGNUCASH_PCI_ABI_VERSION;
    request.record.size = sizeof(request.record);
    if (!ggnucash_transaction_record_valid(&request.record)) {
        fprintf(stderr, "transaction record is invalid\n");
        return 2;
    }

    if (ioctl(fd, GGNUCASH_IOCTL_SUBMIT, &request) < 0) {
        perror("GGNUCASH_IOCTL_SUBMIT");
        if (errno == EIO) {
            fprintf(stderr, "device error %u, status 0x%08x\n",
                    request.error, request.status);
        }
        return 1;
    }
    printf("Accepted transaction %" PRIu64 "; device count=%" PRIu64 "\n",
           (uint64_t) request.record.transaction_id,
           (uint64_t) request.transaction_count);
    return 0;
}

int main(int argc, char **argv)
{
    int fd;
    int result;

    if (argc < 3) {
        fprintf(stderr, "usage: %s info DEVICE | submit DEVICE TX_ID DEBIT CREDIT AMOUNT_MINOR CURRENCY_NUMERIC\n",
                argv[0]);
        return 2;
    }

    fd = open(argv[2], O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror(argv[2]);
        return 1;
    }

    if (strcmp(argv[1], "info") == 0 && argc == 3) {
        result = show_info(fd);
    } else if (strcmp(argv[1], "submit") == 0) {
        result = submit_transaction(fd, argc, argv);
    } else {
        fprintf(stderr, "unknown command or wrong number of arguments\n");
        result = 2;
    }

    close(fd);
    return result;
}
