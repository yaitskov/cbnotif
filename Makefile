EXTRA_CFLAGS=-std=gnu99
ccflags-y := -std=gnu99

obj-m := first.o


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	sudo rmmod first ; echo ok;
	sudo insmod first.ko
	sleep 2; sync; sudo tail /var/log/kern.log 


clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -rf *~ *.o *.ko *mod.c Module.symvers