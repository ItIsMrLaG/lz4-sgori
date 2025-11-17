# LZ4 Scatter-Gather (SG) Buffers Support in Linux Kernel block layer

This project extends the LZ4 compression algorithm implementation in the Linux kernel to support SG-buffers (based on `struct bio_vec`), 
eliminating the need for additional data copying.

## Key Features

- Kernel-space implementation of LZ4 with SG buffers support
- Block device module for tests and experiments
- Test environment for validation
- API extension in `include/lz4e/lz4e.h`

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

See [our docs](doc/API.md) for more details.

## Installation

1. Build the module:
   ```bash
   make build
   ```

2. Insert the module into the kernel:
   ```bash
   sudo insmod lz4e.ko
   ```

## Block-dev usage

1. Create a compressed block device:
   ```bash
   echo -n "<path_to_underlying_device>" > /sys/module/lz4e/parameters/mapper
   ```

2. Remove the compressed block device:
   ```bash
   echo -n "unmap" > /sys/module/lz4e/parameters/unmapper
   ```

## Testing

Run the complete test suite with:
```bash
test/test_all.sh
```

Some tests use [fio](https://fio.readthedocs.io/en/latest/fio_doc.html) utility, so make sure it is installed.

## Implementation details

Detailed implementation description can be found in the [documentation](doc/Compression.md).
Essentially, we replace all uses of pointer arithmetic in favor of using `struct bio_vec` iterators to access and modify the data.
