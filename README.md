# Ponos — Asymmetric Lossless Compression Codec

A portable, pure C11 lossless compression codec. Named after the Greek spirit of hard toil and endurance.

## Philosophy

**Asymmetric design**: The compressor (Ponos) does all the heavy, slow work — exhaustive search, optimal parsing, adaptive entropy coding. The decompressor is fast, simple, and uses minimal memory.

- Compression may be VERY slow (especially at high levels)
- Decompression is FAST
- High compression ratio
- Low decoder memory usage
- No CPU extensions required

## Building

```bash
make            # Build the CLI tool
make test       # Build and run tests
make clean      # Clean build artifacts
```

Requires: GCC (or any C11 compiler), Make.

```bash
gcc -std=c11 -O2 -Wall -Wextra -pedantic -o ponos ponos.c main.c
```

## Usage

```bash
# Compress
./ponos c input.txt output.pono [level]

# Decompress
./ponos d output.pono restored.txt
```

Compression levels 1-9:
- Levels 1-2: Fast, greedy parsing
- Levels 3-4: Greedy with lazy matching
- Levels 5-9: Optimal parsing (DP), slower but better ratio

## API

```c
#include "ponos.h"

// Compress
int ponos_compress(const uint8_t *src, size_t src_len,
                   uint8_t **dst, size_t *dst_len, int level);

// Decompress
int ponos_decompress(const uint8_t *src, size_t src_len,
                     uint8_t **dst, size_t *dst_len);
```

Returns 0 on success, nonzero on error. Caller frees output with `free()`.

## Format

- Magic: `PONO`
- Little-endian
- Block-based (default 1 MiB, configurable 64 KiB – 4 MiB)
- CRC32 integrity per block
- Binary range coder with adaptive probability models

## Design

### Encoder (slow, thorough)
- LZ77 with 4-byte hash chains
- Search depth up to 256 (level 9)
- Optimal parsing via dynamic programming (level ≥ 5)
- Adaptive range coder with context models
- May use up to ~64 MiB RAM for 1 MiB block

### Decoder (fast, simple)
- Read range-coded symbols
- Decode literal or match
- Copy from window for matches
- No hash tables, no search, no DP
- Under ~3 MiB memory for 1 MiB blocks

## Requirements

- Pure C11 (no extensions)
- No SSE/AVX/NEON/BMI
- No intrinsics or inline assembly
- No compiler builtins
- Only standard headers: stdint.h, stddef.h, stdlib.h, string.h, stdio.h, errno.h
- Single-threaded
- No external libraries

## License

Public domain / MIT (choose your preference).
