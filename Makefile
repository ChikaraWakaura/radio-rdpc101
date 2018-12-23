KERNEL_HEADERS=/lib/modules/$(shell uname -r)/build
KERNEL_BASE=/lib/modules/$(shell uname -r)/kernel

obj-m := radio-rdpc101.o

all:
	$(MAKE) -C $(KERNEL_HEADERS) M=$(shell pwd) modules

clean:
	$(MAKE) -C $(KERNEL_HEADERS) M=$(shell pwd) clean

install:
	cp radio-rdpc101.ko $(KERNEL_BASE)/drivers/media/radio
	depmod -a

uninstall:
	rm $(KERNEL_BASE)/drivers/media/radio/radio-rdpc101.ko
	depmod -a
