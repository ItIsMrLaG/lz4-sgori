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
It is also possible to build the block device separately by running:
```bash
make bdev
```
Although, it requires symbols obtained from compiling the library.

## Installing

Following commands require root privileges. If you wish to run using `sudo` it is recommended to use `-E` flag
to preserve the current environment.

After compiling the modules, they can be dynamically inserted into the running kernel (via `insmod`) by calling
```bash
make insert
```
```bash
make lib-insert
```
```bash
make bdev-insert
```
for all modules, library, or the block device respectively.

Alternatively, you can install them into the `modules` directory of your kernel by running one of the following:
```bash
make install
```
```bash
make lib-install
```
```bash
make bdev-install
```
After that, the modules can be inserted using `modprobe`.

## Cleanup

After your work is done, you can remove the modules from the kernel by running (as root or with `sudo -E`):
```bash
make remove
```
```bash
make lib-remove
```
```bash
make bdev-remove
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
- [top-module Kbuild](https://github.com/ItIsMrLaG/lz4-sgori/blob/main/Kbuild);
- [setting KBUILD_EXTRA_SYMBOLS](https://github.com/ItIsMrLaG/lz4-sgori/blob/main/lz4e_bdev/Kbuild).

After the symbols can be accessed by your module, to use functions provided by the header
[`lz4e.h`](https://github.com/ItIsMrLaG/lz4-sgori/blob/main/lz4e/include/lz4e.h)
you can add it to your includes using gcc's `-I` flag, or by directly copying it into your sources.

## Using the block device

After module `lz4e_bdev` is inserted into the kernel, its parameters can be accessed using sysfs:
```bash
/sys/module/lz4e_bdev/parameters
├── /sys/module/lz4e_bdev/parameters/mapper        # create a proxy block device over the given one
├── /sys/module/lz4e_bdev/parameters/unmapper      # remove the proxy block device
├── /sys/module/lz4e_bdev/parameters/acceleration  # LZ4 acceleration factor
├── /sys/module/lz4e_bdev/parameters/comp_type     # path for compression (cont, vect, strm, extd)
├── /sys/module/lz4e_bdev/parameters/stats_reset   # reset I/O statistics
├── /sys/module/lz4e_bdev/parameters/stats_r_[...] # individual I/O stats for read
└── /sys/module/lz4e_bdev/parameters/stats_w_[...] # individual I/O stats for write
```

For example, you can create a block device by running:
```bash
echo -n "<path_to_underlying_device>" > /sys/module/lz4e_bdev/parameters/mapper
```
To remove the created device, run:
```bash
echo -n "<any input>" > /sys/module/lz4e_bdev/parameters/unmapper
```

With `comp_type` you can select the way I/O requests are processed by LZ4. Currently, there are 4 options:
- `cont` — copy data to a preallocated contiguous buffer, run standard LZ4;
- `vect` — run standard LZ4 compression/decompression for each of bvecs;
- `strm` — same as `vect`, but use [Streaming API](https://github.com/lz4/lz4/blob/dev/examples/streaming_api_basics.md) to improve compression ratio;
- `extd` — use extended LZ4 for scatter-gather buffers.

`comp_type` is set to `extd` by default, to change it, for example, to `strm`, you would run:
```bash
echo -n "strm" > /sys/module/lz4e_bdev/parameters/comp_type
```

Using the `acceleration` parameter you can speed up the compression, at the cost of compression ratio.
According to [documentation](https://elixir.bootlin.com/linux/v6.19.8/source/include/linux/lz4.h#L200) in the kernel,
each successive value provides roughly +~3% to speed. By default, the acceleration factor is 1.

## I/O statistics

`lz4e_bdev` has a range of request statistics, both for read and write. Each individual value can be obtained from sysfs using read-only parameters.
Statistics are collected for read and write separately. At the moment, they consist of:
- `reqs_total` — total amount of I/O requests;
- `reqs_failed` — number of failed requests;
- `min_vec` — minimum size in bytes of processed multi-page I/O vector;
- `max_vec` — maximum size in bytes of processed multi-page I/O vector;
- `vecs` — number of processed multi-page I/O vectors;
- `segments` — number of processed single-page segments;
- `decomp_size` — total size of data before compression in bytes;
- `comp_size` — total size of data after compression in bytes;
- `mem_usage` — size in bytes of memory used for running compression;
- `copy_ns` — time spent copying data in nanoseconds (can be zero depending on `comp_type`);
- `comp_ns` — time elapsed during compression in nanoseconds;
- `decomp_ns` — time elapsed during decompression in nanoseconds;
- `total_ns` — total time elapsed during I/O processing in nanoseconds (including work of the underlying device).
Parameters for read and write operations are prefixed with `stats_r_` and `stats_w_` respectively.

All stats can be reset using `stats_reset` parameter:
```bash
echo -n "<any input>" > /sys/module/lz4e_bdev/parameters/stats_reset
```

Also, using `make stats-pprint` you can run a script that would print formatted statistics to stdout.
By default it prints a summary for read and write combined. You can specify which stats to print using options `-r`, `-w`, `-a` (for "read", "write", "all").
For example, to print read and write stats, run `make stats-pprint ARGS="-rw"`.

## Testing

To run the complete test suite, which consists of basic functionality tests as well as more complex ones using
[fio](https://fio.readthedocs.io/en/latest/fio_doc.html) utility, run:
```bash
make test
```
To run a faster and simpler suite for the block device, use:
```bash
make test-fast
```
You can also run only the `fio` tests with:
```bash
make test-fio
```

## Running the experiment

Options for `run_experiment.py`:
- `--dataset` — path to the dataset directory. Default: `./experiment/dataset`
- `--result` — path to intermediate results directory. Default: `./experiment/result`
- `--graph` — path to generated graphs directory. Default: `./experiment/graph`
- `--under-dev` — path to the underlying device, over which to create proxy. Default: `None`, but create block RAM disk with `brd`
- `--dev-size` — size in KB of block RAM device to create, in case `--under-dev` is `None`. Default: `1048576`, or 1GB
- `--bs` — block size in IEC format to use with `dd`. Default: `1M`
- `--runs` — number of test runs to make for each file and each compression type. Default: `5`
- `--acceleration` — acceleration factor to use with LZ4. Default: `1`
- `--no-graph` — whether to skip graph generation. Default: `false`

Options for `generate_graphs.py`:
- `--result` — path to intermediate results directory. Default: `./experiment/result`
- `--graph` — path to generated graphs directory. Default: `./experiment/graph`

To run the experiment with default options, use:
```bash
make expt
```
To generate graphs from intermediate results using default options, run:
```bash
make expt-graph
```
To clean any experiment output, run:
```bash
make expt-clean
```

Using command line options with `make` is also supported. You can pass options using the `ARGS` variable, for example:
```bash
make expt ARGS="--bs 64k --runs 10 --no-graph"
```
Instead of using `ARGS`, you can set individual options using their respective variable as well:
```bash
make expt DATASET="./my_data" BS="4M" NO_GRAPH="true"
```
