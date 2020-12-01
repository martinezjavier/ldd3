
# Comment/uncomment the following line to enable/disable debugging
#DEBUG = y


ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DSCULLD_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

LDDINC=$(PWD)/../include
EXTRA_CFLAGS += $(DEBFLAGS) -I$(LDDINC)

TARGET = sculld

ifneq ($(KERNELRELEASE),)

sculld-objs := main.o mmap.o scull-shared/scull-async.o

obj-m	:= sculld.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	echo "Copying Module.symvers to resolve lddbus exports"
#	cp $(PWD)/../lddbus/Module.symvers $(PWD)
	$(MAKE) -C $(KERNELDIR) M=$(PWD) KBUILD_EXTRA_SYMBOLS=$(PWD)/../lddbus/Module.symvers modules

endif


install:
	install -d $(INSTALLDIR)
	install -c $(TARGET).o $(INSTALLDIR)

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.mod modules.order *.symvers scull-shared/scull-async.o


depend .depend dep:
	$(CC) $(EXTRA_CFLAGS) -M *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif
