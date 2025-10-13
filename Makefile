KERNEL_VERSION := $(shell uname -r)
KERNEL_SOURCES_DIR := /lib/modules/$(KERNEL_VERSION)/build

OUTPUT_DIR := $(PWD)/build
COMPILE_COMMANDS := $(PWD)/compile_commands.json

.PHONY: all
all: build

.PHONY: build
build:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(PWD) MO=$(OUTPUT_DIR) modules

.PHONY: clean
clean:
	rm -rf $(OUTPUT_DIR) $(COMPILE_COMMANDS)
