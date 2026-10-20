obj-m += ggnucash_pci.o
ccflags-y += -I$(src)/../include

.PHONY: all clean
all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean
