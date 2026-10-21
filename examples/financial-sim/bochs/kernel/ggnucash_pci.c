#include <linux/iopoll.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/uaccess.h>

#include "ggnucash/financial_pci.h"

#define GGNUCASH_PCI_TIMEOUT_US 1000000

struct ggnucash_pci {
    struct pci_dev *pdev;
    void __iomem *bar;
    struct miscdevice misc;
    struct kref references;
    struct mutex lock;
    char misc_name[32];
    bool removed;
};

static const struct pci_device_id ggnucash_pci_ids[] = {
    { PCI_DEVICE(GGNUCASH_PCI_VENDOR_ID, GGNUCASH_PCI_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, ggnucash_pci_ids);

static void ggnucash_release_device(struct kref *references);

static long ggnucash_get_info(struct ggnucash_pci *device, unsigned long arg)
{
    struct ggnucash_device_info info;

    if (device->removed) {
        return -ENODEV;
    }
    info.abi_version = ioread32(device->bar + GGNUCASH_PCI_REG_ABI_VERSION);
    info.capabilities = ioread32(device->bar + GGNUCASH_PCI_REG_CAPABILITIES);
    info.status = ioread32(device->bar + GGNUCASH_PCI_REG_STATUS);
    info.error = ioread32(device->bar + GGNUCASH_PCI_REG_ERROR);
    info.transaction_count = ioread32(device->bar + GGNUCASH_PCI_REG_TX_COUNT);

    if (copy_to_user((void __user *) arg, &info, sizeof(info))) {
        return -EFAULT;
    }
    return 0;
}

static void ggnucash_write_record(struct ggnucash_pci *device,
                                  const struct ggnucash_transaction_record *record)
{
    const u32 words[] = {
        record->version,
        record->size,
        lower_32_bits(record->transaction_id),
        upper_32_bits(record->transaction_id),
        record->debit_account,
        record->credit_account,
        lower_32_bits((u64) record->amount_minor),
        upper_32_bits((u64) record->amount_minor),
        record->currency_numeric,
        record->reserved,
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(words); ++i) {
        iowrite32(words[i], device->bar + GGNUCASH_PCI_REG_DATA + i * sizeof(u32));
    }
}

static long ggnucash_submit(struct ggnucash_pci *device, unsigned long arg)
{
    struct ggnucash_submit submit;
    u32 status;
    int ret;

    if (copy_from_user(&submit, (void __user *) arg, sizeof(submit))) {
        return -EFAULT;
    }
    if (!ggnucash_transaction_record_valid(&submit.record)) {
        return -EINVAL;
    }

    mutex_lock(&device->lock);
    if (device->removed) {
        ret = -ENODEV;
        goto out_unlock;
    }
    status = ioread32(device->bar + GGNUCASH_PCI_REG_STATUS);
    if (!(status & GGNUCASH_PCI_STATUS_READY) || (status & GGNUCASH_PCI_STATUS_BUSY)) {
        ret = -EBUSY;
        goto out_unlock;
    }

    iowrite32(sizeof(submit.record), device->bar + GGNUCASH_PCI_REG_LENGTH);
    ggnucash_write_record(device, &submit.record);
    iowrite32(GGNUCASH_PCI_COMMAND_SUBMIT, device->bar + GGNUCASH_PCI_REG_COMMAND);

    ret = read_poll_timeout(ioread32, status,
                            !(status & GGNUCASH_PCI_STATUS_BUSY),
                            1000, GGNUCASH_PCI_TIMEOUT_US, false,
                            device->bar + GGNUCASH_PCI_REG_STATUS);
    if (ret) {
        goto out_unlock;
    }

    submit.status = status;
    submit.error = ioread32(device->bar + GGNUCASH_PCI_REG_ERROR);
    submit.transaction_count = ioread32(device->bar + GGNUCASH_PCI_REG_TX_COUNT);
    if (copy_to_user((void __user *) arg, &submit, sizeof(submit))) {
        ret = -EFAULT;
        goto out_unlock;
    }
    if (status & GGNUCASH_PCI_STATUS_ERROR) {
        ret = -EIO;
    }

out_unlock:
    mutex_unlock(&device->lock);
    return ret;
}

static long ggnucash_ioctl(struct file *file, unsigned int command, unsigned long arg)
{
    struct ggnucash_pci *device = file->private_data;
    long ret;

    switch (command) {
        case GGNUCASH_IOCTL_GET_INFO:
            mutex_lock(&device->lock);
            ret = ggnucash_get_info(device, arg);
            mutex_unlock(&device->lock);
            return ret;
        case GGNUCASH_IOCTL_SUBMIT:
            return ggnucash_submit(device, arg);
        default:
            return -ENOTTY;
    }
}

static int ggnucash_open(struct inode *inode, struct file *file)
{
    struct miscdevice *misc = file->private_data;
    struct ggnucash_pci *device = container_of(misc, struct ggnucash_pci, misc);
    int ret;

    mutex_lock(&device->lock);
    if (device->removed) {
        mutex_unlock(&device->lock);
        return -ENODEV;
    }
    kref_get(&device->references);
    mutex_unlock(&device->lock);
    file->private_data = device;
    ret = nonseekable_open(inode, file);
    if (ret) {
        kref_put(&device->references, ggnucash_release_device);
    }
    return ret;
}

static void ggnucash_release_device(struct kref *references)
{
    struct ggnucash_pci *device = container_of(references, struct ggnucash_pci, references);

    kfree(device);
}

static int ggnucash_release_file(struct inode *inode, struct file *file)
{
    struct ggnucash_pci *device = file->private_data;

    kref_put(&device->references, ggnucash_release_device);
    return 0;
}

static const struct file_operations ggnucash_fops = {
    .owner = THIS_MODULE,
    .open = ggnucash_open,
    .release = ggnucash_release_file,
    .unlocked_ioctl = ggnucash_ioctl,
    .compat_ioctl = ggnucash_ioctl,
    .llseek = noop_llseek,
};

static int ggnucash_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct ggnucash_pci *device;
    u32 capabilities;
    int ret;

    ret = pci_enable_device(pdev);
    if (ret) {
        return ret;
    }
    if (!(pci_resource_flags(pdev, GGNUCASH_PCI_BAR) & IORESOURCE_IO) ||
        pci_resource_len(pdev, GGNUCASH_PCI_BAR) < GGNUCASH_PCI_BAR_SIZE) {
        ret = -ENODEV;
        goto err_disable;
    }
    ret = pci_request_region(pdev, GGNUCASH_PCI_BAR, "ggnucash-pci");
    if (ret) {
        goto err_disable;
    }

    device = kzalloc(sizeof(*device), GFP_KERNEL);
    if (!device) {
        ret = -ENOMEM;
        goto err_release_region;
    }
    device->pdev = pdev;
    kref_init(&device->references);
    mutex_init(&device->lock);
    device->bar = pci_iomap(pdev, GGNUCASH_PCI_BAR, GGNUCASH_PCI_BAR_SIZE);
    if (!device->bar) {
        ret = -ENOMEM;
        goto err_free_device;
    }

    if (ioread32(device->bar + GGNUCASH_PCI_REG_ID) != GGNUCASH_PCI_ID ||
        ioread32(device->bar + GGNUCASH_PCI_REG_ABI_VERSION) != GGNUCASH_PCI_ABI_VERSION) {
        ret = -ENODEV;
        goto err_iounmap;
    }
    capabilities = ioread32(device->bar + GGNUCASH_PCI_REG_CAPABILITIES);
    if (!(capabilities & GGNUCASH_PCI_CAP_PIO_TX)) {
        ret = -ENODEV;
        goto err_iounmap;
    }

    snprintf(device->misc_name, sizeof(device->misc_name), "ggnucash-%s",
             pci_name(pdev));
    device->misc.minor = MISC_DYNAMIC_MINOR;
    device->misc.name = device->misc_name;
    device->misc.fops = &ggnucash_fops;
    device->misc.parent = &pdev->dev;
    device->misc.mode = 0600;

    ret = misc_register(&device->misc);
    if (ret) {
        goto err_iounmap;
    }
    pci_set_drvdata(pdev, device);
    dev_info(&pdev->dev, "registered /dev/%s (ABI %u)\n",
             device->misc_name, GGNUCASH_PCI_ABI_VERSION);
    return 0;

err_iounmap:
    pci_iounmap(pdev, device->bar);
err_free_device:
    kfree(device);
err_release_region:
    pci_release_region(pdev, GGNUCASH_PCI_BAR);
err_disable:
    pci_disable_device(pdev);
    return ret;
}

static void ggnucash_remove(struct pci_dev *pdev)
{
    struct ggnucash_pci *device = pci_get_drvdata(pdev);

    misc_deregister(&device->misc);
    mutex_lock(&device->lock);
    device->removed = true;
    pci_iounmap(pdev, device->bar);
    pci_release_region(pdev, GGNUCASH_PCI_BAR);
    pci_disable_device(pdev);
    mutex_unlock(&device->lock);
    kref_put(&device->references, ggnucash_release_device);
}

static struct pci_driver ggnucash_driver = {
    .name = "ggnucash-pci",
    .id_table = ggnucash_pci_ids,
    .probe = ggnucash_probe,
    .remove = ggnucash_remove,
};
module_pci_driver(ggnucash_driver);

MODULE_AUTHOR("GGNuCash contributors");
MODULE_DESCRIPTION("GGNuCash financial PCI device driver");
MODULE_LICENSE("GPL");
