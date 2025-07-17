#ifndef LZ4E
#define LZ4E

int LZ4E_compress_default(const char *source, char *dest, int inputSize,
			  int maxOutputSize, void *wrkmem);

int LZ4E_decompress_safe(const char *source, char *dest, int compressedSize,
			 int maxDecompressedSize);

#endif /* LZ4E */
