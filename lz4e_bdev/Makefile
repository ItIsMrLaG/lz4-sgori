include $(PWD)/flags.mk

ccflags-y += -I $(PWD)/lz4e

SYMBOLS_ALL := $(PWD)/build/Module.symvers
SYMBOLS_LIB := $(PWD)/build/lz4e/Module.symvers

ifneq ("$(wildcard $(SYMBOLS_ALL))", "")
	KBUILD_EXTRA_SYMBOLS += $(SYMBOLS_ALL)
else
	KBUILD_EXTRA_SYMBOLS += $(SYMBOLS_LIB)
endif

$(info KBUILD_EXTRA_SYMBOLS = $(KBUILD_EXTRA_SYMBOLS))

lz4e_bdev-y := lz4e_module.o \
	lz4e_dev.o \
	lz4e_under_dev.o \
	lz4e_req.o \
	lz4e_chunk.o \
	lz4e_stats.o

obj-m := lz4e_bdev.o
