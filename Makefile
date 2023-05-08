ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
obj-m := periph_blk.o

else
# normal makefile
K_DIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(K_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(K_DIR) M=$(PWD) clean
	$(RM) *~

endif
