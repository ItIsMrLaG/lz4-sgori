include $(PWD)/flags.mk

lz4e-y := module/lz4e_module.o \
	module/lz4e_dev.o \
	module/lz4e_under_dev.o \
	module/lz4e_req.o \
	module/lz4e_chunk.o \
	module/lz4e_stats.o

lz4e-y += lz4e/lz4e_compress.o \
	lz4e/lz4e_decompress.o

obj-m := lz4e.o
