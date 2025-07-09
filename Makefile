KERNEL_VERSION := $(shell uname -r)
KERNEL_SOURCES_DIR := /lib/modules/$(KERNEL_VERSION)/build

OUTPUT_DIR := $(PWD)/build
COMPILE_COMMANDS := $(PWD)/compile_commands.json

.PHONY: clean

.PHONY: clean

obj-m := blk_comp.o
blk_comp-y := blk_comp_module.o blk_comp_dev.o underlying_dev.o gendisk_utils.o

all: build

build:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(PWD) MO=$(OUTPUT_DIR) modules
clean:
	rm -rf $(OUTPUT_DIR) $(COMPILE_COMMANDS)
