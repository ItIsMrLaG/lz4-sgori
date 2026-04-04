# Home

LZ4 is a fast lossless compression algorithm, implementation of which is included in the Linux Kernel.
For its extreme (de)compression speed it is often used with block layer I/O. Here are some of known applications of LZ4:
- Linux Kernel ZRAM: <https://docs.kernel.org/admin-guide/blockdev/zram.html>
- ZFS: <https://openzfs.github.io/openzfs-docs/Performance%20and%20Tuning/Workload%20Tuning.html#compression>
- ClickHouse: <https://clickhouse.com/docs/data-compression/compression-modes>

Block layer I/O's can often contain large amounts of data. Handling that data as one contiguous chunk would be complicated:
allocating such a chunk can be impossible due to fragmentation. For that reason, we would rather work with a bunch of smaller contiguous buffers
that are separated in memory. A sequence of such buffers can be called a *scatter-gather* buffer.

Standard LZ4 implementation in the Linux Kernel, however, can only handle contiguous buffers.
Using it with scatter-gather buffers would require either:
- copying data to a preallocated buffer, which would require additional time and space;
- running compression by parts, which could decrease the compression ratio.

This repository provides a modification of LZ4, that adds out-of-the-box support for scatter-gather buffers in the Linux Kernel.
It also features a block device module which works as a proxy over an existing device, meant to be used for testing the modified algorithm
and comparing it to the standard implementation in the block layer setting.

## Contents
- [API](API.md) — standard and extended LZ4 interface description;
- [BlockFormat](BlockFormat.md) — LZ4 block format description, independent of implementation;
- [Compression](Compression.md) — standard LZ4 compression algorithm, implementation details for the extended version;
- [Experiment](Experiment.md) — experiment comparing performances of different LZ4 variations in the kernel block layer;
- [Usage](Usage.md) — a guide on using the modified LZ4, as well as the proxy block device.
