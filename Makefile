KERNEL_VERSION := $(shell uname -r)
KERNEL_SOURCES_DIR := /lib/modules/$(KERNEL_VERSION)/build

LIB_NAME := lz4e
COMPRESS_NAME := $(LIB_NAME)_compress
DECOMPRESS_NAME := $(LIB_NAME)_decompress
BDEV_NAME := $(LIB_NAME)_bdev

TARGET_ALL := $(PWD)
TARGET_LIB := $(TARGET_ALL)/$(LIB_NAME)
TARGET_BDEV := $(TARGET_ALL)/$(BDEV_NAME)

OUTPUT_ALL := $(PWD)/build
OUTPUT_LIB := $(OUTPUT_ALL)/$(LIB_NAME)
OUTPUT_BDEV := $(OUTPUT_ALL)/$(BDEV_NAME)

COMPRESS_OBJ := $(OUTPUT_LIB)/$(COMPRESS_NAME).ko
DECOMPRESS_OBJ := $(OUTPUT_LIB)/$(DECOMPRESS_NAME).ko
BDEV_OBJ := $(OUTPUT_BDEV)/$(BDEV_NAME).ko

TEST_DIR_NAME := test
EXPERIMENT_DIR_NAME := experiment
SCRIPTS_DIR_NAME := scripts

TEST_ALL := ./$(TEST_DIR_NAME)/test_all.sh
TEST_FAST := ./$(TEST_DIR_NAME)/bash_tests/test_all.sh
TEST_FIO := ./$(TEST_DIR_NAME)/fio_tests/test_all.sh

RUN_EXPERIMENT := ./$(EXPERIMENT_DIR_NAME)/run_experiment.py
GENERATE_GRAPHS := ./$(EXPERIMENT_DIR_NAME)/generate_graphs.py

EXPERIMENT_DIR := $(PWD)/$(EXPERIMENT_DIR_NAME)
OUTPUT_EXPERIMENT := $(EXPERIMENT_DIR)/build \
		     $(EXPERIMENT_DIR)/result \
		     $(EXPERIMENT_DIR)/graph \
		     $(EXPERIMENT_DIR)/*.egg-info

CHECK_FORMAT := ./$(SCRIPTS_DIR_NAME)/check_with_clang_format.sh
CHECK_TIDY := ./$(SCRIPTS_DIR_NAME)/check_with_clang_tidy.sh
FIX_FORMAT := ./$(SCRIPTS_DIR_NAME)/fix_with_clang_format.sh
FIX_TIDY := ./$(SCRIPTS_DIR_NAME)/fix_with_clang_tidy.sh

STATS_PPRINT := ./$(SCRIPTS_DIR_NAME)/stats_pretty_print.sh
WIKI_SYNC := ./$(SCRIPTS_DIR_NAME)/synchronize_wiki.sh

# ---------------- All, lib and block dev ----------------

.PHONY: all
all:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(TARGET_ALL) MO=$(OUTPUT_ALL) modules

.PHONY: install
install:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(TARGET_ALL) MO=$(OUTPUT_ALL) modules_install

.PHONY: clean
clean:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(TARGET_ALL) MO=$(OUTPUT_ALL) clean
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
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(TARGET_LIB) MO=$(OUTPUT_LIB) modules

.PHONY: lib-install
lib-install:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(TARGET_LIB) MO=$(OUTPUT_LIB) modules_install

.PHONY: lib-clean
lib-clean:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(TARGET_LIB) MO=$(OUTPUT_LIB) clean
	rm -rf $(OUTPUT_LIB)

.PHONY: lib-insert
lib-insert:
	insmod $(COMPRESS_OBJ)
	insmod $(DECOMPRESS_OBJ)

.PHONY: lib-remove
lib-remove:
	rmmod $(DECOMPRESS_NAME) || true
	rmmod $(COMPRESS_NAME) || true

.PHONY: lib-reinsert
lib-reinsert:
	$(MAKE) lib-remove && $(MAKE) lib-insert

# ---------------- Block dev only ----------------

.PHONY: bdev
bdev:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(TARGET_BDEV) MO=$(OUTPUT_BDEV) modules

.PHONY: bdev-install
bdev-install:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(TARGET_BDEV) MO=$(OUTPUT_BDEV) modules_install

.PHONY: bdev-clean
bdev-clean:
	$(MAKE) -j -C $(KERNEL_SOURCES_DIR) M=$(TARGET_BDEV) MO=$(OUTPUT_BDEV) clean
	rm -rf $(OUTPUT_BDEV)

.PHONY: bdev-insert
bdev-insert:
	insmod $(BDEV_OBJ)

.PHONY: bdev-remove
bdev-remove:
	rmmod $(BDEV_NAME) || true

.PHONY: bdev-reinsert
bdev-reinsert:
	$(MAKE) bdev-remove && $(MAKE) bdev-insert

# ---------------- Testing ----------------

.PHONY: test
test:
	$(MAKE) && $(TEST_ALL)

.PHONY: test-fast
test-fast:
	$(MAKE) && $(TEST_FAST)

.PHONY: test-fio
test-fio:
	$(MAKE) && $(TEST_FIO)

# ---------------- Experiments ----------------

.PHONY: expt
expt:
	$(MAKE) && $(RUN_EXPERIMENT)

.PHONY: expt-graph
expt-graph:
	$(GENERATE_GRAPHS)

.PHONY: expt-clean
expt-clean:
	rm -rf $(OUTPUT_EXPERIMENT)

# ---------------- Helper scripts ----------------

.PHONY: check-format
check-format:
	$(CHECK_FORMAT)

.PHONY: check-tidy
check-tidy:
	$(CHECK_TIDY)

.PHONY: fix-format
fix-format:
	$(FIX_FORMAT)

.PHONY: fix-tidy
fix-tidy:
	$(FIX_TIDY)

# ---------------- Printing stats ----------------

STATS_PPRINT_ARGS :=
READ ?= false
WRITE ?= false
ALL ?= false

ifeq ($(READ),true)
	STATS_PPRINT_ARGS += -r
endif

ifeq ($(WRITE),true)
	STATS_PPRINT_ARGS += -w
endif

ifeq ($(ALL),true)
	STATS_PPRINT_ARGS += -a
endif

ifdef ARGS
	STATS_PPRINT_ARGS := $(ARGS)
endif

.PHONY: stats-pprint
stats-pprint:
	$(STATS_PPRINT) $(STATS_PPRINT_ARGS)

# ---------------- Wiki synchronization ----------------

WIKI_SYNC_ARGS :=
ASSUME_YES ?= false

ifdef WIKI_PATH
	WIKI_SYNC_ARGS += -w $(WIKI_PATH)
endif

ifdef MESSAGE
	WIKI_SYNC_ARGS += -m $(MESSAGE)
endif

ifdef BASE_COMMIT
	WIKI_SYNC_ARGS += -b $(BASE_COMMIT)
endif

ifeq ($(ASSUME_YES),true)
	WIKI_SYNC_ARGS += -y
endif

ifdef ARGS
	WIKI_SYNC_ARGS := $(ARGS)
endif

.PHONY: wiki-sync
wiki-sync:
	$(WIKI_SYNC) $(WIKI_SYNC_ARGS)
