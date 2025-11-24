# LZ4 Scatter-Gather (SG) Buffers Support in Linux Kernel block layer

This project extends the LZ4 compression algorithm implementation in the Linux kernel to support SG-buffers (based on `struct bio_vec`), 
eliminating the need for additional data copying.

## Key Features

- Kernel-space implementation of LZ4 with SG buffers support
- Block device module for tests and experiments
- Test environment for validation
- API extension in `lz4e/include/lz4e.h`

## Extended API

Signature of the modified compression function, which handles sequences of `struct bio_vec` instead of contiguous buffers:
```c
/*
 * src: source buffer as a list of bio_vec's
 * dst: destination buffer as a list of bio_vec's
 * srcIter: iterator into 'src' at the start of data to compress
 * dstIter: iterator into 'dst' starting from which the bytes are written
 * wrkmem: pointer to the allocated working memory
 * returns: number of bytes written to 'dst', or 0 if compression fails
 */
int LZ4E_compress_default(const struct bio_vec *src, struct bio_vec *dst,
		struct bvec_iter *srcIter, struct bvec_iter *dstIter, void *wrkmem);
```

See more details: [doc/API.md](doc/API.md).

## Requirements

- Kernel version: 6.17.5

Building the modules requires kernel headers to be present. In Ubuntu, for example, you can install them by running:
```bash
sudo apt-get install linux-headers-$(uname -r)
```

Using the block device also requires LZ4 kernel modules to be inserted into your kernel.
You can insert them by running:
```bash
modprobe lz4 lz4_compress lz4_decompress
```

## Installation

1. Build and insert extended compression with block device:
   ```bash
   make && make insert
   ```

2. Build and insert extended compression library only:
   ```bash
   make lib && make lib_insert
   ```

See more options: [doc/Usage.md](doc/Usage.md).

## Usage

1. Use extended compression in your module: [doc/Usage.md#using-the-library](doc/Usage.md#using-the-library)

2. Create a compressed block device:
   ```bash
   echo -n "<path_to_underlying_device>" > /sys/module/lz4e_bdev/parameters/mapper
   ```

3. Remove the compressed block device:
   ```bash
   echo -n "unmap" > /sys/module/lz4e_bdev/parameters/unmapper
   ```

## Testing

Run the complete test suite with:
```bash
make test
```

Some tests use [fio](https://fio.readthedocs.io/en/latest/fio_doc.html) utility, so make sure it is installed.

## Implementation details

Detailed implementation description can be found at: [doc/Compression.md](doc/Compression.md).
Essentially, we replace all uses of pointer arithmetic in favor of using `struct bio_vec` iterators to access and modify the data.
