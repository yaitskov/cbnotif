obj-m := first.o


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules


clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -rf *~ *.o *.ko *mod.c Module.symvers