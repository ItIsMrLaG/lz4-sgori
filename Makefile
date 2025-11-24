KERNEL_VERSION := $(shell uname -r)
KERNEL_SOURCES_DIR := /lib/modules/$(KERNEL_VERSION)/build

LIB_NAME := lz4e
COMPRESS_NAME := $(LIB_NAME)_compress
DECOMPRESS_NAME := $(LIB_NAME)_decompress
BDEV_NAME := $(LIB_NAME)_bdev

ALL := $(PWD)
LIB := $(ALL)/$(LIB_NAME)
BDEV := $(ALL)/$(BDEV_NAME)

OUTPUT_ALL := $(PWD)/build
OUTPUT_LIB := $(OUTPUT_ALL)/$(LIB_NAME)
OUTPUT_BDEV := $(OUTPUT_ALL)/$(BDEV_NAME)

COMPRESS_OBJ := $(OUTPUT_LIB)/$(COMPRESS_NAME).ko
DECOMPRESS_OBJ := $(OUTPUT_LIB)/$(DECOMPRESS_NAME).ko
BDEV_OBJ := $(OUTPUT_BDEV)/$(BDEV_NAME).ko

TEST_ALL := $(PWD)/test/test_all.sh

# ---------------- All, lib and block dev----------------

.PHONY: all
all:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(ALL) MO=$(OUTPUT_ALL) modules

.PHONY: install
install:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(ALL) MO=$(OUTPUT_ALL) modules_install

.PHONY: clean
clean:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(ALL) MO=$(OUTPUT_ALL) clean
	rm -rf $(OUTPUT_ALL)

.PHONY: insert
insert:
	insmod $(COMPRESS_OBJ)
	insmod $(DECOMPRESS_OBJ)
	insmod $(BDEV_OBJ)

.PHONY: remove
remove:
	rmmod $(BDEV_NAME) || true
	rmmod $(DECOMPRESS_NAME) || true
	rmmod $(COMPRESS_NAME) || true

.PHONY: reinsert
reinsert:
	$(MAKE) remove && $(MAKE) insert

# ---------------- Lib only ----------------

.PHONY: lib
lib:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(LIB) MO=$(OUTPUT_LIB) modules

.PHONY: lib_install
lib_install:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(LIB) MO=$(OUTPUT_LIB) modules_install

.PHONY: lib_clean
lib_clean:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(LIB) MO=$(OUTPUT_LIB) clean
	rm -rf $(OUTPUT_LIB)

.PHONY: lib_insert
lib_insert:
	insmod $(COMPRESS_OBJ)
	insmod $(DECOMPRESS_OBJ)

.PHONY: lib_remove
lib_remove:
	rmmod $(DECOMPRESS_NAME) || true
	rmmod $(COMPRESS_NAME) || true

.PHONY: lib_reinsert
lib_reinsert:
	$(MAKE) lib_remove && $(MAKE) lib_insert

# ---------------- Block dev only ----------------

.PHONY: bdev
bdev:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(BDEV) MO=$(OUTPUT_BDEV) modules

.PHONY: bdev_install
bdev_install:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(BDEV) MO=$(OUTPUT_BDEV) modules_install

.PHONY: bdev_clean
bdev_clean:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(BDEV) MO=$(OUTPUT_BDEV) clean
	rm -rf $(OUTPUT_BDEV)

.PHONY: bdev_insert
bdev_insert:
	insmod $(BDEV_OBJ)

.PHONY: bdev_remove
bdev_remove:
	rmmod $(BDEV_NAME) || true

.PHONY: bdev_reinsert
bdev_reinsert:
	$(MAKE) bdev_remove && $(MAKE) bdev_insert

# ---------------- Testing ----------------

.PHONY: test
test:
	$(MAKE) && $(SHELL) $(TEST_ALL)
