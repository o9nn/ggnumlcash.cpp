CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror

.PHONY: all clean
all: ggnucashctl

ggnucashctl: ggnucashctl.c ../include/ggnucash/financial_pci.h
	$(CC) $(CFLAGS) -I../include -o $@ ggnucashctl.c

clean:
	rm -f ggnucashctl
