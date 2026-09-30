/*
 * Ponos Lossless Compression Codec
 * =================================
 * A portable, asymmetric lossless compression codec in pure C11.
 *
 * Philosophy: The compressor (Ponos — spirit of hard toil) does all the
 * heavy, slow work. The decompressor is fast, simple, and low-memory.
 *
 * Format:
 *   Magic: "PONO"
 *   Header: magic[4], version(u8), block_size(u32), original_size(u64), num_blocks(u32)
 *   Per block: compressed_size(u32), uncompressed_size(u32), crc32(u32), payload
 *
 * API:
 *   int ponos_compress(const uint8_t *src, size_t src_len,
 *                      uint8_t **dst, size_t *dst_len, int level);
 *   int ponos_decompress(const uint8_t *src, size_t src_len,
 *                        uint8_t **dst, size_t *dst_len);
 *
 * Returns 0 on success, nonzero on error. Caller frees output with free().
 *
 * Compression levels 1-9. Higher = slower but better compression.
 * Level >= 5 enables optimal parsing.
 */

#ifndef PONOS_H
#define PONOS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Magic bytes identifying a Ponos compressed stream */
#define PONOS_MAGIC_0 'P'
#define PONOS_MAGIC_1 'O'
#define PONOS_MAGIC_2 'N'
#define PONOS_MAGIC_3 'O'

/* Current format version */
#define PONOS_VERSION 1

/* Default block size: 1 MiB */
#define PONOS_DEFAULT_BLOCK_SIZE (1u << 20)

/* Block size limits */
#define PONOS_MIN_BLOCK_SIZE (1u << 16)   /* 64 KiB */
#define PONOS_MAX_BLOCK_SIZE (1u << 22)   /* 4 MiB */

/* Compression levels */
#define PONOS_MIN_LEVEL 1
#define PONOS_MAX_LEVEL 9

/* Header size in bytes: magic(4) + version(1) + block_size(4) + original_size(8) + num_blocks(4) */
#define PONOS_HEADER_SIZE 21

/* Block header size: compressed_size(4) + uncompressed_size(4) + crc32(4) */
#define PONOS_BLOCK_HEADER_SIZE 12

/*
 * ponos_compress - Compress data using the Ponos codec.
 *
 * @src:     Input data buffer
 * @src_len: Length of input data in bytes
 * @dst:     Output pointer (allocated by this function, caller must free)
 * @dst_len: Output length (set by this function)
 * @level:   Compression level (1-9). Higher = slower, better ratio.
 *
 * Returns 0 on success, nonzero on error.
 * On success, *dst points to allocated memory that the caller must free().
 * On error, *dst is set to NULL.
 */
int ponos_compress(const uint8_t *src, size_t src_len,
                   uint8_t **dst, size_t *dst_len, int level);

/*
 * ponos_decompress - Decompress Ponos-compressed data.
 *
 * @src:     Compressed data buffer
 * @src_len: Length of compressed data in bytes
 * @dst:     Output pointer (allocated by this function, caller must free)
 * @dst_len: Output length (set by this function)
 *
 * Returns 0 on success, nonzero on error.
 * On success, *dst points to allocated memory that the caller must free().
 * On error, *dst is set to NULL.
 */
int ponos_decompress(const uint8_t *src, size_t src_len,
                     uint8_t **dst, size_t *dst_len);

#ifdef __cplusplus
}
#endif

#endif /* PONOS_H */
