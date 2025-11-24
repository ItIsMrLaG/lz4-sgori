# Usage

This page provides a guide for setting up and using the testing block device, as well as interacting with the library [API](API.md).

## Building

You can build both the lib and block dev by running
```bash
make all
```
or just `make`. After compiling, module object files `lz4e_compress.ko`, `lz4e_decompress.ko` and `lz4e_bdev.ko`
can be found in the output directory `build`.

If you wish to build only the library you can run:
```bash
make lib
```
Building only the block device is also possible by running
```bash
make bdev
```
, although it requires symbols obtained from compiling the library.

## Installing

Following commands require root privileges. If you wish to run using `sudo` it is recommended to use `-E` flag
to preserve the current environment.

After compiling the modules, they can be dynamically inserted into the running kernel (via `insmod`) by calling
```bash
make insert
```
```bash
make lib_insert
```
```bash
make bdev_insert
```
for all modules, library, or the block device respectively.

Alternatively, you can install them into the `modules` directory of your kernel by running one of the following:
```bash
make install
```
```bash
make lib_install
```
```bash
make bdev_install
```
After that, the modules can be inserted using `modprobe`.

## Cleanup

After your work is done, you can remove the modules from the kernel by running (as root or with `sudo -E`):
```bash
make remove
```
```bash
make lib_remove
```
```bash
make bdev_remove
```

To clear the output directory `build`, you can run:
```bash
make clean
```

## Using the library

To use the functions described in [API](API.md) in your own code, modules `lz4e_compress` and `lz4e_decompress` must be
inserted into your kernel. After that, to be able to access exported symbols you can either:
- compile your module against ours using a top-level Makefile/Kbuild file;
- set `KBUILD_EXTRA_SYMBOLS` variable in your Makefile to contain an absolute path to `Module.symvers` file of the built library.

See more details: <https://docs.kernel.org/kbuild/modules.html#symbols-from-another-external-module>.

As examples for both cases, you can see how the block dev module is compiled when running `make` and `make bdev`:
- [top-module Kbuild](../Kbuild);
- [setting KBUILD_EXTRA_SYMBOLS](../lz4e_bdev/Makefile).

After the symbols can be accessed by your module, to use functions provided by the header [`lz4e.h`](../lz4e/include/lz4e.h) you can add it to your includes
using gcc's `-I` flag, or by directly copying it into your sources.

## Using the block device

After module `lz4e_bdev` is inserted into the kernel, its parameters can be accessed using sysfs:
```bash
/sys/module/lz4e_bdev/parameters
├── /sys/module/lz4e_bdev/parameters/mapper   # create a proxy block device over the given one
├── /sys/module/lz4e_bdev/parameters/unmapper # remove the proxy block device
└── /sys/module/lz4e_bdev/parameters/stats    # access I/O request statistics
```

For example, you can create a block device by running:
```bash
echo -n "<path_to_underlying_device>" > /sys/module/lz4e_bdev/parameters/mapper
```
To remove the created device, run:
```bash
echo -n "unmap" > /sys/module/lz4e_bdev/parameters/unmapper
```
To print I/O statistics of device, use:
```bash
cat /sys/module/lz4e_bdev/parameters/stats
```
And to reset the request statistics:
```bash
echo -n "reset" > /sys/module/lz4e_bdev/parameters/stats
```
