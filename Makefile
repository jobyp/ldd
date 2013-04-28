ifneq ($(KERNELRELEASE),)
	obj-m:=hw.o
else
	KDIR:=/lib/modules/$(shell uname -r)/build
	PWD:=$(shell pwd)

.PHONY: all
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

.PHONY: clean
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@rm -f *~
endif