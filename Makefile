KDIR:=/lib/modules/$(shell uname -r)/build
PWD:=$(shell pwd)

KO=ias_blkdev
OBJS=ias_blkdev.o

obj-m := $(OBJS)

.PHONY: clean cleanall sl export

define build
	$(MAKE) -C $(KDIR) M=$(PWD) $1
endef

default: sl
	$(call build,modules)

clean:
	$(call build,clean)

cleanall:clean
	$(shell rm -f sys_module kernel_headers new_module.tar.gz tags)

sl:
	@echo "creating soft link: kernel_headers -> $(KDIR)"
	@ln -sf $(KDIR) kernel_headers

export:
	@echo "exporting to new_module.tar.gz..."
	@tar czf new_module.tar.gz Makefile .gitignore

