# GGNuCash PCI device MVP

This directory defines a small, versioned PCI I/O BAR interface for guest
software. It includes a standalone register-level reference model, a Linux PCI
driver, a userspace utility, and ABI/model tests. It does not change or depend
on the existing in-process `VirtualPCB` simulator.

## Integration boundary

There is no Bochs source tree, device registration configuration, or Bochs
build target in this repository. `FinancialPciModel` is the register behavior
reference, not a Bochs `bx_pci_device_c` plugin. A Bochs-specific PCI device
adapter and its plugin/build registration still need to be added against the
exact Bochs revision selected by the integrator. The adapter must implement
the register contract below and run the same protocol tests. No device should
be advertised in a guest until that adapter is installed.

The PCI identity `1234:4747` is a local Bochs-development placeholder, not an
assigned GGNuCash vendor identity. Keep it confined to test VMs and replace it
with an appropriately assigned identity before distributing a device or
driver. The Linux module matches only that identity.

## Guest-facing contract, ABI version 1

The device exposes BAR 0 as a 32-bit PCI I/O BAR of at least `0x120` bytes.
Registers and record data use little-endian dwords. Register accesses must be
aligned 32-bit reads or writes; unsupported accesses read as all ones or are
ignored. The initial implementation uses programmed I/O only: no DMA, PCI
interrupts, MSI/MSI-X, or asynchronous queue.

| Offset | Register | Access and meaning |
| --- | --- | --- |
| `0x00` | Device ID | Read-only `0x434e4747` (`GGNC`) |
| `0x04` | ABI version | Read-only `1` |
| `0x08` | Capabilities | Read-only; bit 0 means PIO transaction submission |
| `0x0c` | Status | Read-only; bit 0 ready, bit 1 busy, bit 2 error |
| `0x10` | Command | Write `1` to submit or `2` to reset |
| `0x14` | Length | Read/write payload length in bytes |
| `0x18` | Accepted transaction count | Read-only 32-bit count |
| `0x1c` | Error | Read-only device error code |
| `0x20`–`0x11f` | Data window | Read/write 256-byte payload window |

A submit payload is exactly one `ggnucash_transaction_record` (40 bytes).
Version 1 contains a nonzero transaction ID, distinct nonzero debit/credit
account codes, a positive amount in minor currency units, and a numeric
ISO-4217 currency code from 1 through 999. The record's version and size must
match the ABI; its reserved field must be zero. Records are submitted as ten
aligned dword writes to the data window, followed by the length and submit
command. An accepted record increments the volatile count; this device does
not post, persist, or reconcile accounting entries.

Error codes are `0` none, `1` unknown command, `2` wrong payload length,
`3` invalid record, and `4` count overflow. A failed command sets the status
error bit and does not increment the accepted count. Writing zero to the error
register clears the error. Reset clears the status error, count, length, and
payload. The contract is bounded to one serialized submission at a time.

The ioctl ABI is defined by
`include/ggnucash/financial_pci.h`. `GET_INFO` returns the version,
capabilities, status, error, and accepted count. `SUBMIT` validates and sends
one transaction record and returns the resulting status, error, and count.
The ioctl structures contain only fixed-width fields and no user pointers.

## Linux guest driver and utility

Build the out-of-tree module and userspace utility inside the Linux guest with
matching kernel headers:

```sh
cd examples/financial-sim/bochs/kernel
make
sudo insmod ggnucash_pci.ko

cd ../userspace
make
```

The driver verifies the PCI identity, BAR type/size, device ID, ABI version,
and PIO capability before registering a mode-`0600` device node named
`/dev/ggnucash-<PCI-address>`. The kernel driver serializes requests, bounds
the payload to the one fixed-size record, polls for completion with a timeout,
and rejects records that fail the shared ABI checks. It uses no DMA or
interrupts. On removal, existing file descriptors are kept safe and receive
`ENODEV` rather than accessing a removed BAR.

Inspect the interface or submit a sample transaction:

```sh
./ggnucashctl info /dev/ggnucash-0000:00:03.0
./ggnucashctl submit /dev/ggnucash-0000:00:03.0 1 1101 4100 25000 840
```

The final argument is the numeric currency code (for example, `840` for USD);
the amount is an integer number of minor units, never a floating-point value.
The device node is restricted to root by default. Administrators may grant
access to a dedicated group through normal device-node/udev policy.

## Build and test in this repository

The host-side tests do not require Bochs or Linux kernel headers:

```sh
cmake -B build
cmake --build build --target test-financial-pci-model test-financial-pci-abi
./build/bin/test-financial-pci-model
./build/bin/test-financial-pci-abi
```

Build the Linux module and utility only inside a Linux guest with the
corresponding kernel headers:

```sh
make -C examples/financial-sim/bochs/kernel
make -C examples/financial-sim/bochs/userspace
```

The existing simulator remains independently testable:

```sh
cmake --build build --target test-virtual-device
./build/bin/test-virtual-device
```

## Compatibility and delivery checklist

- Do not change the meaning or offsets of ABI v1 registers or fields. Add new
  behavior through capability bits or a new ABI version.
- Keep the ioctl structures fixed-width and pointer-free; add compile-time
  layout assertions whenever the ABI changes.
- Validate the same record constraints in the Bochs adapter, Linux driver,
  and host-side reference-model tests.
- Before a release, select and pin a Bochs revision, implement and build its
  native PCI adapter, install it in a test VM, and verify guest discovery,
  driver probe, successful and invalid submissions, timeout/error handling,
  reset, repeated bind/unbind, and removal with an open file.
- Test the driver against each supported guest kernel. The host CMake test
  targets do not build or validate the kernel module or Bochs adapter.
