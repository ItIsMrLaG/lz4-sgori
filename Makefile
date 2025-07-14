KVER ?= $(shell uname -r)
SRC_DIR := /lib/modules/$(KVER)/build

all: build

build:
		$(MAKE) -j -C $(SRC_DIR) M=$(PWD) modules
clean:
		$(MAKE) -j -C $(SRC_DIR) M=$(PWD) clean
