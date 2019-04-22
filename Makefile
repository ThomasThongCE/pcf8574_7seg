TARGET_MOUDLE := pcf8574_7seg
obj-m += $(TARGET_MOUDLE).o
#KERNEL_SRC := /path/to/your/kernel
SRC := $(shell pwd)

all:
	make -C $(KERNEL_SRC) M=$(SRC) modules
clean:
	make -C $(KERNEL_SRC) M=$(SRC) clear
clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
