KDIR:=/lib/modules/$(shell uname -r)/build
PWD:=$(shell pwd)

KO=mymodule
OBJS=mymodule.o

# what are we compiling?
# NOTE obj-m, not obj -m, NOT mymodules.c, Just:
obj-m := $(OBJS)

.PHONY: clean sl export

# finally...
default: sl
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(shell rm -f sys_module kernel_headers new_module.tar.gz)

sl:
	@echo "creating soft link: sys_module -> /sys/modules/$(KO)"
	@ln -sf /sys/module/$(KO) sys_module
	@echo "creating soft link: kernel_headers -> $(KDIR)"
	@ln -sf $(KDIR) kernel_headers

export:
	@echo "exporting to new_module.tar.gz..."
	@tar czf new_module.tar.gz Makefile .gitignore

