.PHONY: all clean archive

obj-m += hanvon8.o
 
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
 
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

archive:
	tar f - --exclude=.git -C ../ -c hanvon8 | gzip -c9 > ../hanvon8-`date +%Y%m%d`.tgz
