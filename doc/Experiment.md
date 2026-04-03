# Experiment

The target of the experiment is to compare the performance of different applications of LZ4 compression algorithm in the kernel.
More specifically, we want to use LZ4 in the block layer by compressing data of I/O requests, for utilizing the disk space more efficiently.
This means handling incoming data, represented as a sequence of contiguous vectors (`struct bio_vec`), stored in a structure representing
the block I/O (`struct bio`). More info on these structures, as well as standard and extended LZ4 API, can be found at [API.md](API.md).

The difference in the methods to follow lies in the way each bvec in processed by the compression (and also decompression).
Currently, we are running the following LZ4 variations.

1) `cont`, or Contiguous.
   Here we process incoming and resulting data as large contiguous chunks. Doing this requires allocating large enough buffers for data to fit,
   as well as copying incoming data into one of the buffers (called `src`). After coping, data can be compressed into `dst`
   using the standard LZ4 API in the kernel (`LZ4_compress_fast`). After compressing, we would decompress resulting data from `dst` back into `src`
   using `LZ4_decompress_safe`. The compression algorithm requires roughly 16KB of working memory to run.
   Processing a single I/O would also require allocating `src` and `dst` buffers.

2) `vect`, or Vectorized.
   To process bvecs, we will compress each of them individually, using the same API as for the previous case.
   Doing this, we eliminate the need for allocating the `src` buffer, but creating `dst` buffers is necessary for all compression variants.
   In this case, we would allocate a single `dst` buffer for each incoming single-page segment. There, however, exist multi-page bvecs,
   but here we treat them as a set of single-page ones for simplicity reasons. It is important to mention, that when using LZ4 there is a chance that
   the data will not compress at all and will grow in size. Allocating large enough buffer for data to fit anyway (size found using `LZ4_COMPRESSBOUND` macro)
   makes the algorithm run faster, as it doesn't need to check for buffer overflows. When we compress by single-page,
   the memory overhead (difference of compress bound and original size) is larger compared to the Contiguous case. As for compression ratio,
   it becomes lower, because we cannot find matches in previous segments. This method is expected to have roughly the same throughput as Contiguous case,
   not taking data copying into account.

3) `strm`, or Streaming.
   It is the same as Vectorized, but here we use the [LZ4 Streaming API](https://github.com/lz4/lz4/blob/dev/examples/streaming_api_basics.md)
   to improve the compression ratio. Functions used for compression and decompression are `LZ4_compress_fast_continue` and
   `LZ4_decompress_safe_continue` respectively. In this case, we are able to find matches in previous bvec,
   but only a single contiguous one, and the size of the window for match searching is still limited to 64KB. This variation will still, generally,
   have a lower compression ratio as opposed to Contiguous case, depending on the layout of vectors in I/O.
   It will only be equal in case I/O is comprised of 64KB contiguous chunks. Streaming case is expected to be somewhat slower than the Vectorized one,
   because it requires manipulations with dictionary for each individual compression.

4) `extd`, or Extended.
   Use the original version of LZ4 extended for scatter-gather buffers, which allows us to use LZ4 directly on the whole set of bvecs.
   More implementation details can be found at [Compression.md](Compression.md).
   For compression, we use function `LZ4E_compress_fast`. As well as vectorized cases, we don't need to allocate any `src` buffers.
   Also, the memory overhead for `dst` buffer is the same as for Contiguous case, because we can calculate compress bound for the whole buffer at once.
   For working memory we need 1 more KB on top of the standard 16KB. The Extended variant is expected to be slower than Streaming one,
   while providing stronger compression. In fact, we expect the Extended version to have the same compression ratio as Contiguous one.
   The reason for lower throughput is the need of mapping each page into the virtual address space, as well as lower overall locality.

## Environment

The experimental environment consists of the following:
- `dataset` — directory for datasets, files for experiment if found recursively in all subdirectories;
- `result` — output directory for intermediate experiment results;
- `graph` — output directory for generated graphs;
- `run_experiment.py` — main script for running the experiment;
- `generate_graphs.py` — script for generating graphs from intermediate results.
