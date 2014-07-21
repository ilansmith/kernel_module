KDIR:=/lib/modules/$(shell uname -r)/build
PWD:=$(shell pwd)

KO=mymodule
OBJS=mymodule.o

# what are we compiling?
# NOTE obj-m, not obj -m, NOT mymodules.c, Just:
obj-m := $(OBJS)

.PHONY: clean sl export

# finally...
default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(shell rm -f sys_module, new_module.tar.gz)

sl:
	@echo "creating soft link: sys_module -> /sys/modules/$(KO)"
	@ln -sf /sys/module/$(KO) sys_module

export:
	@echo "exporting to new_module.tar.gz..."
	@tar czf new_module.tar.gz Makefile .gitignore

