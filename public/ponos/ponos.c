/*
 * Ponos Lossless Compression Codec — Implementation
 * ===================================================
 *
 * This file implements the complete Ponos codec:
 *   - CRC32 (table-based, portable)
 *   - Binary range coder (encoder and decoder)
 *   - Adaptive probability models
 *   - LZ77 match finder (hash chains)
 *   - Optimal parser (dynamic programming)
 *   - Block compression and decompression
 *   - File format handling
 *
 * Design principles:
 *   - Pure C11, no extensions, no intrinsics, no asm
 *   - Portable: works on any platform with 32-bit+ integers
 *   - Asymmetric: slow compressor, fast decompressor
 *   - Low decoder memory: < 3 MiB for 1 MiB blocks
 */

#include "ponos.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* =========================================================================
 * Section 1: Constants and Configuration
 * ========================================================================= */

/* Range coder precision */
#define RC_PROB_BITS    12
#define RC_PROB_TOTAL   (1u << RC_PROB_BITS)   /* 4096 */
#define RC_TOP_VALUE    (1u << 24)              /* Normalization threshold */

/* LZ77 parameters */
#define LZ_MIN_MATCH    3
#define LZ_MAX_MATCH    65535
#define LZ_HASH_BITS    18          /* Hash table size = 2^18 = 262144 */
#define LZ_HASH_SIZE    (1u << LZ_HASH_BITS)
#define LZ_HASH_MASK    (LZ_HASH_SIZE - 1)
#define LZ_HASH_SEED    0x9E3779B9u /* Golden ratio constant */

/* Model sizes */
#define NUM_MATCH_CTX   32          /* is_match contexts */
#define NUM_LIT_CTX     256         /* literal contexts (previous byte) */
#define LIT_TREE_DEPTH  8           /* binary tree depth for 256 symbols */
#define NUM_LEN_CTX     17          /* length bit contexts (0..16) */
#define NUM_DIST_SLOTS  24          /* distance slots */
#define NUM_DIST_CTX    (NUM_DIST_SLOTS * 2)  /* distance bit contexts */

/* Optimal parser */
#define OPT_MAX_MATCH_PER_POS 64    /* Max matches to consider per position */

/* =========================================================================
 * Section 2: CRC32 (Portable Table-Based)
 * ========================================================================= */

static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

static void crc32_init_table(void) {
    uint32_t i, j;
    if (crc32_table_initialized) return;
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (j = 0; j < 8; j++) {
            if (c & 1)
                c = 0xEDB88320u ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc32_table[i] = c;
    }
    crc32_table_initialized = 1;
}

static uint32_t crc32_compute(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    crc32_init_table();
    for (i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* =========================================================================
 * Section 3: Little-Endian I/O Helpers
 * ========================================================================= */

static void write_u8(uint8_t *buf, uint8_t val) {
    buf[0] = val;
}

static void write_u32(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)(val);
    buf[1] = (uint8_t)(val >> 8);
    buf[2] = (uint8_t)(val >> 16);
    buf[3] = (uint8_t)(val >> 24);
}

static void write_u64(uint8_t *buf, uint64_t val) {
    buf[0] = (uint8_t)(val);
    buf[1] = (uint8_t)(val >> 8);
    buf[2] = (uint8_t)(val >> 16);
    buf[3] = (uint8_t)(val >> 24);
    buf[4] = (uint8_t)(val >> 32);
    buf[5] = (uint8_t)(val >> 40);
    buf[6] = (uint8_t)(val >> 48);
    buf[7] = (uint8_t)(val >> 56);
}

static uint8_t read_u8(const uint8_t *buf) {
    return buf[0];
}

static uint32_t read_u32(const uint8_t *buf) {
    return (uint32_t)buf[0]
         | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16)
         | ((uint32_t)buf[3] << 24);
}

static uint64_t read_u64(const uint8_t *buf) {
    return (uint64_t)buf[0]
         | ((uint64_t)buf[1] << 8)
         | ((uint64_t)buf[2] << 16)
         | ((uint64_t)buf[3] << 24)
         | ((uint64_t)buf[4] << 32)
         | ((uint64_t)buf[5] << 40)
         | ((uint64_t)buf[6] << 48)
         | ((uint64_t)buf[7] << 56);
}

/* =========================================================================
 * Section 4: Range Coder — Encoder
 * =========================================================================
 *
 * Binary range coder with 12-bit adaptive probabilities.
 *
 * State:
 *   low:   uint64_t — lower bound of current interval (64-bit for carry)
 *   range: uint32_t — width of current interval
 *
 * Encoding a bit with probability p0 (probability of 0, in [1, 4095]):
 *   split = (range >> 12) * p0
 *   If bit == 0: range = split
 *   If bit == 1: low += split; range -= split
 *
 * Normalization: while range < 2^24, output top byte and shift.
 * Carry propagation: handled by 64-bit low + cache mechanism.
 */

typedef struct {
    uint64_t low;           /* 64-bit: upper bits handle carry */
    uint32_t range;         /* Interval width */
    uint8_t *out;           /* Output buffer */
    size_t out_pos;         /* Current write position */
    size_t out_cap;         /* Output buffer capacity */
    uint8_t cache;          /* Cached byte for carry propagation */
    uint32_t cache_size;    /* Number of pending 0xFF bytes */
} RangeEnc;

static void rc_enc_init(RangeEnc *e, uint8_t *out, size_t cap) {
    e->low = 0;
    e->range = 0xFFFFFFFFu;
    e->out = out;
    e->out_pos = 0;
    e->out_cap = cap;
    e->cache = 0;
    e->cache_size = 1;
}

static void rc_enc_output_byte(RangeEnc *e, uint8_t b) {
    if (e->out_pos < e->out_cap) {
        e->out[e->out_pos++] = b;
    }
}

/*
 * Shift low left by 8, outputting the determined byte.
 * Handles carry propagation using the cache mechanism.
 */
static void rc_enc_shift_low(RangeEnc *e) {
    /* Check if the top byte of the 32-bit portion is determined */
    if ((uint32_t)e->low < 0xFF000000u || (uint32_t)(e->low >> 32) != 0) {
        uint8_t temp = e->cache;
        uint8_t carry = (uint8_t)(uint32_t)(e->low >> 32);

        /* Output cached byte + carry */
        rc_enc_output_byte(e, (uint8_t)(temp + carry));

        /* Output pending 0xFF bytes + carry */
        while (e->cache_size > 0) {
            rc_enc_output_byte(e, (uint8_t)(0xFF + carry));
            e->cache_size--;
        }

        /* New cache: next byte of low */
        e->cache = (uint8_t)((uint32_t)(e->low >> 24));
    } else {
        /* Top byte is 0xFF — ambiguous, cache it */
        e->cache_size++;
    }

    /* Shift: keep lower 32 bits, shift left by 8 */
    e->low = (uint64_t)((uint32_t)e->low << 8);
}

/* Normalize: output bytes while range is too small */
static void rc_enc_normalize(RangeEnc *e) {
    while (e->range < RC_TOP_VALUE) {
        rc_enc_shift_low(e);
        e->range <<= 8;
    }
}

/* Encode a single bit with adaptive probability p0 (prob of 0) */
static void rc_enc_bit(RangeEnc *e, int bit, uint16_t p0) {
    uint32_t split;

    /* Clamp probability to valid range */
    if (p0 < 1) p0 = 1;
    if (p0 > (uint16_t)(RC_PROB_TOTAL - 1)) p0 = (uint16_t)(RC_PROB_TOTAL - 1);

    split = (e->range >> RC_PROB_BITS) * (uint32_t)p0;

    if (bit == 0) {
        e->range = split;
    } else {
        e->low += (uint64_t)split;
        e->range -= split;
    }

    rc_enc_normalize(e);
}

/* Flush encoder: output remaining bytes */
static void rc_enc_flush(RangeEnc *e) {
    int i;
    for (i = 0; i < 5; i++) {
        rc_enc_shift_low(e);
    }
}

/* =========================================================================
 * Section 5: Range Coder — Decoder
 * =========================================================================
 *
 * Mirror of the encoder. Reads bytes from input and maintains the
 * same interval state. Decoding is the inverse of encoding.
 */

typedef struct {
    uint32_t code;          /* Current position in interval (32-bit) */
    uint32_t range;         /* Interval width */
    const uint8_t *in;      /* Input buffer */
    size_t in_pos;          /* Current read position */
    size_t in_len;          /* Input buffer length */
} RangeDec;

static uint8_t rc_dec_read_byte(RangeDec *d) {
    if (d->in_pos < d->in_len) {
        return d->in[d->in_pos++];
    }
    return 0; /* Past end: return 0 (safe for corrupted input) */
}

static void rc_dec_init(RangeDec *d, const uint8_t *in, size_t len) {
    int i;
    d->code = 0;
    d->range = 0xFFFFFFFFu;
    d->in = in;
    d->in_pos = 0;
    d->in_len = len;

    /*
     * Read 5 bytes into code. Since code is uint32_t, the first byte
     * overflows out. This is correct because the encoder's first output
     * byte is always 0 (initial cache value, no carry yet).
     */
    for (i = 0; i < 5; i++) {
        d->code = (d->code << 8) | rc_dec_read_byte(d);
    }
}

/* Normalize: read bytes while range is too small */
static void rc_dec_normalize(RangeDec *d) {
    while (d->range < RC_TOP_VALUE) {
        d->code = (d->code << 8) | rc_dec_read_byte(d);
        d->range <<= 8;
    }
}

/* Decode a single bit with probability p0 (prob of 0) */
static int rc_dec_bit(RangeDec *d, uint16_t p0) {
    uint32_t split;
    int bit;

    if (p0 < 1) p0 = 1;
    if (p0 > (uint16_t)(RC_PROB_TOTAL - 1)) p0 = (uint16_t)(RC_PROB_TOTAL - 1);

    split = (d->range >> RC_PROB_BITS) * (uint32_t)p0;

    if (d->code < split) {
        /* Symbol is 0 */
        d->range = split;
        bit = 0;
    } else {
        /* Symbol is 1 */
        d->code -= split;
        d->range -= split;
        bit = 1;
    }

    rc_dec_normalize(d);
    return bit;
}

/* =========================================================================
 * Section 6: Adaptive Probability Models
 * =========================================================================
 *
 * Each probability is stored as a 12-bit value in [1, 4095].
 * Update rule: prob += (target - prob) >> shift
 * This provides exponential moving average adaptation.
 */

#define PROB_INIT (RC_PROB_TOTAL / 2)  /* Initial probability: 2048 (50/50) */
#define PROB_SHIFT 5                    /* Adaptation speed */

typedef uint16_t Prob;

/* Initialize a probability to 50/50 */
static void prob_init(Prob *p) {
    *p = PROB_INIT;
}

/* Update probability after observing a bit */
static void prob_update(Prob *p, int bit) {
    if (bit == 0) {
        /* Increase probability of 0 */
        *p += (Prob)((RC_PROB_TOTAL - *p) >> PROB_SHIFT);
    } else {
        /* Decrease probability of 0 */
        *p -= (Prob)(*p >> PROB_SHIFT);
    }
    /* Clamp to valid range */
    if (*p < 1) *p = 1;
    if (*p > (Prob)(RC_PROB_TOTAL - 1)) *p = (Prob)(RC_PROB_TOTAL - 1);
}

/* =========================================================================
 * Section 7: Codec Models
 * =========================================================================
 *
 * The codec uses the following adaptive models:
 *
 * 1. is_match[32]: Whether current token is a match
 *    Context: (prev_byte >> 5) * 8 + state
 *    state: 0 = after literal, 1-3 = after 1-3 consecutive matches
 *
 * 2. literal[256][255]: Binary tree for literal bytes
 *    Context: previous decoded byte
 *    Each context has a binary tree of 255 probabilities (depth 8)
 *
 * 3. length[17]: Match length encoding
 *    Binary representation, MSB first, one context per bit position
 *
 * 4. distance_slot[24]: Distance slot (coarse distance)
 * 5. distance_bit[24]: Extra bits within distance slot
 */

typedef struct {
    /* is_match model: 32 contexts */
    Prob is_match[NUM_MATCH_CTX];

    /* literal model: 256 contexts x 255 probs each (binary tree) */
    /* To save memory in decoder, we use a flat array */
    Prob *lit;  /* [NUM_LIT_CTX * (LIT_TREE_DEPTH * 2 - 1)] or similar */
    int lit_allocated;

    /* length model: bit contexts */
    Prob len_bit[NUM_LEN_CTX];
    Prob len_gt[NUM_LEN_CTX];  /* "greater than" contexts for unary prefix */

    /* distance model */
    Prob dist_slot[NUM_DIST_SLOTS];
    Prob dist_bit[NUM_DIST_CTX];
} CodecModels;

static int models_init(CodecModels *m) {
    int i;

    /* is_match */
    for (i = 0; i < NUM_MATCH_CTX; i++) {
        prob_init(&m->is_match[i]);
    }

    /* literal: allocate binary tree probabilities */
    /* For each of 256 contexts, we need (2^8 - 1) = 255 probabilities */
    m->lit = (Prob *)malloc(sizeof(Prob) * NUM_LIT_CTX * 255);
    if (!m->lit) return -1;
    m->lit_allocated = 1;

    for (i = 0; i < NUM_LIT_CTX * 255; i++) {
        prob_init(&m->lit[i]);
    }

    /* length */
    for (i = 0; i < NUM_LEN_CTX; i++) {
        prob_init(&m->len_bit[i]);
        prob_init(&m->len_gt[i]);
    }

    /* distance */
    for (i = 0; i < NUM_DIST_SLOTS; i++) {
        prob_init(&m->dist_slot[i]);
    }
    for (i = 0; i < NUM_DIST_CTX; i++) {
        prob_init(&m->dist_bit[i]);
    }

    return 0;
}

static void models_free(CodecModels *m) {
    if (m->lit_allocated && m->lit) {
        free(m->lit);
        m->lit = NULL;
        m->lit_allocated = 0;
    }
}

/* =========================================================================
 * Section 8: Literal Encoding/Decoding (Binary Tree)
 * =========================================================================
 *
 * Each literal byte is encoded as 8 binary decisions using a binary tree.
 * The tree is traversed from root to leaf, with the previous byte as context.
 *
 * Tree layout for context c:
 *   prob[c * 255 + 0] = root (bit 7)
 *   prob[c * 255 + 1] = left child of root (bit 6 if bit7=0)
 *   prob[c * 255 + 2] = right child of root (bit 6 if bit7=1)
 *   ...
 *   Node index for path bits b7..b(k+1): 2^(8-k) - 2 + accumulated
 *   Actually, simpler: node = 1, then for each bit: node = node*2 + bit
 *   Index in array = node - 1 (since node starts at 1)
 */

static void enc_literal(RangeEnc *e, CodecModels *m, uint8_t prev_byte, uint8_t byte) {
    int node = 1;
    int bit_pos;

    for (bit_pos = 7; bit_pos >= 0; bit_pos--) {
        int bit = (byte >> bit_pos) & 1;
        Prob *p = &m->lit[(int)prev_byte * 255 + (node - 1)];
        rc_enc_bit(e, bit, *p);
        prob_update(p, bit);
        node = node * 2 + bit;
    }
}

static uint8_t dec_literal(RangeDec *d, CodecModels *m, uint8_t prev_byte) {
    int node = 1;
    int bit_pos;
    uint8_t byte = 0;

    for (bit_pos = 7; bit_pos >= 0; bit_pos--) {
        int bit;
        Prob *p = &m->lit[(int)prev_byte * 255 + (node - 1)];
        bit = rc_dec_bit(d, *p);
        prob_update(p, bit);
        byte = (uint8_t)(byte | (bit << bit_pos));
        node = node * 2 + bit;
    }
    return byte;
}

/* =========================================================================
 * Section 9: Length Encoding/Decoding
 * =========================================================================
 *
 * Match length is encoded as (length - LZ_MIN_MATCH), giving a value 0..65532.
 * Encoding scheme:
 *   1. Determine number of bits needed (0..16)
 *   2. Encode the bit-length using a unary-like code with contexts
 *   3. Encode the actual bits MSB-first with per-position contexts
 *
 * For small lengths (0-3), we encode directly with 2 bits.
 * For larger lengths, we use the slot+extra scheme.
 */

static int length_num_bits(uint32_t val) {
    int n = 0;
    while (val > 0) {
        n++;
        val >>= 1;
    }
    return n;
}

static void enc_length(RangeEnc *e, CodecModels *m, uint32_t length) {
    uint32_t val = length - LZ_MIN_MATCH;  /* 0-based */
    int nbits, i;

    if (val < 4) {
        /* Short length: encode "short" flag = 0, then 2 bits */
        rc_enc_bit(e, 0, m->len_gt[0]);
        prob_update(&m->len_gt[0], 0);
        for (i = 1; i >= 0; i--) {
            int bit = (val >> i) & 1;
            rc_enc_bit(e, bit, m->len_bit[i]);
            prob_update(&m->len_bit[i], bit);
        }
    } else {
        /* Long length: encode "short" flag = 1, then slot + extra */
        rc_enc_bit(e, 1, m->len_gt[0]);
        prob_update(&m->len_gt[0], 1);

        nbits = length_num_bits(val);
        /* nbits is 3..16. Encode (nbits - 3) using unary: */
        for (i = 0; i < 14; i++) {
            int bit = (i < nbits - 3) ? 1 : 0;
            rc_enc_bit(e, bit, m->len_gt[i + 1]);
            prob_update(&m->len_gt[i + 1], bit);
            if (bit == 0) break;
        }

        /* Encode the bits of val, MSB first */
        for (i = nbits - 1; i >= 0; i--) {
            int bit = (val >> i) & 1;
            int ctx = (i < NUM_LEN_CTX) ? i : NUM_LEN_CTX - 1;
            rc_enc_bit(e, bit, m->len_bit[ctx]);
            prob_update(&m->len_bit[ctx], bit);
        }
    }
}

static uint32_t dec_length(RangeDec *d, CodecModels *m) {
    int is_long = rc_dec_bit(d, m->len_gt[0]);
    prob_update(&m->len_gt[0], is_long);

    if (!is_long) {
        /* Short length: 2 bits */
        uint32_t val = 0;
        int i;
        for (i = 1; i >= 0; i--) {
            int bit = rc_dec_bit(d, m->len_bit[i]);
            prob_update(&m->len_bit[i], bit);
            val = (val << 1) | (uint32_t)bit;
        }
        return val + LZ_MIN_MATCH;
    } else {
        /* Long length: decode bit count, then bits */
        int nbits = 3;
        int i;
        uint32_t val;

        for (i = 0; i < 14; i++) {
            int bit = rc_dec_bit(d, m->len_gt[i + 1]);
            prob_update(&m->len_gt[i + 1], bit);
            if (bit == 1) {
                nbits++;
            } else {
                break;
            }
        }

        /* Decode the bits */
        val = 0;
        for (i = nbits - 1; i >= 0; i--) {
            int bit;
            int ctx = (i < NUM_LEN_CTX) ? i : NUM_LEN_CTX - 1;
            bit = rc_dec_bit(d, m->len_bit[ctx]);
            prob_update(&m->len_bit[ctx], bit);
            val = (val << 1) | (uint32_t)bit;
        }

        return val + LZ_MIN_MATCH;
    }
}

/* =========================================================================
 * Section 10: Distance Encoding/Decoding
 * =========================================================================
 *
 * Distance is encoded using a slot + extra bits scheme:
 *   Slots 0-3: distances 1-4 (direct, no extra bits)
 *   Slot k (k >= 4): distance in [2^(k-1)+1, 2^k], extra bits = k-2
 *
 * The slot is encoded using adaptive probabilities.
 * Extra bits are encoded with per-position contexts.
 */

static int dist_to_slot(uint32_t dist) {
    if (dist <= 4) return (int)(dist - 1);
    /* Find highest bit position */
    int slot = 3;
    uint32_t d = dist - 1;
    while (d > ((1u << slot) - 1)) {
        slot++;
    }
    return slot;
}

static int slot_extra_bits(int slot) {
    if (slot < 4) return 0;
    return slot - 2;
}

static uint32_t slot_base(int slot) {
    if (slot < 4) return (uint32_t)(slot + 1);
    return (1u << (slot - 1)) + 1;
}

static void enc_distance(RangeEnc *e, CodecModels *m, uint32_t dist) {
    int slot = dist_to_slot(dist);
    int extra = slot_extra_bits(slot);
    uint32_t base = slot_base(slot);
    uint32_t extra_val = dist - base;
    int i;

    /* Encode slot using unary-like code */
    for (i = 0; i < NUM_DIST_SLOTS; i++) {
        int bit = (i < slot) ? 1 : 0;
        rc_enc_bit(e, bit, m->dist_slot[i]);
        prob_update(&m->dist_slot[i], bit);
        if (bit == 0) break;
    }

    /* Encode extra bits */
    for (i = extra - 1; i >= 0; i--) {
        int bit = (extra_val >> i) & 1;
        int ctx = slot * 2 + (i < 2 ? i : 1);
        if (ctx >= NUM_DIST_CTX) ctx = NUM_DIST_CTX - 1;
        rc_enc_bit(e, bit, m->dist_bit[ctx]);
        prob_update(&m->dist_bit[ctx], bit);
    }
}

static uint32_t dec_distance(RangeDec *d, CodecModels *m) {
    int slot = 0;
    int extra, i;
    uint32_t base, extra_val, dist;

    /* Decode slot */
    for (i = 0; i < NUM_DIST_SLOTS; i++) {
        int bit = rc_dec_bit(d, m->dist_slot[i]);
        prob_update(&m->dist_slot[i], bit);
        if (bit == 1) {
            slot++;
        } else {
            break;
        }
    }

    /* Decode extra bits */
    extra = slot_extra_bits(slot);
    base = slot_base(slot);
    extra_val = 0;

    for (i = extra - 1; i >= 0; i--) {
        int bit;
        int ctx = slot * 2 + (i < 2 ? i : 1);
        if (ctx >= NUM_DIST_CTX) ctx = NUM_DIST_CTX - 1;
        bit = rc_dec_bit(d, m->dist_bit[ctx]);
        prob_update(&m->dist_bit[ctx], bit);
        extra_val = (extra_val << 1) | (uint32_t)bit;
    }

    dist = base + extra_val;
    return dist;
}

/* =========================================================================
 * Section 11: LZ77 Match Finder (Hash Chains)
 * =========================================================================
 *
 * Uses 4-byte hashing with chained hash table.
 * Hash function: multiplicative hash using golden ratio constant.
 * Chain: each entry points to the previous position with the same hash.
 */

typedef struct {
    uint32_t *hash;       /* Hash table: hash[h] = most recent position with hash h */
    uint32_t *chain;      /* Chain: chain[pos] = previous position with same hash */
    uint32_t hash_size;   /* Size of hash table */
    uint32_t max_depth;   /* Maximum chain traversal depth */
} MatchFinder;

static uint32_t lz_hash4(const uint8_t *p) {
    uint32_t v = (uint32_t)p[0]
               | ((uint32_t)p[1] << 8)
               | ((uint32_t)p[2] << 16)
               | ((uint32_t)p[3] << 24);
    return (v * LZ_HASH_SEED) >> (32 - LZ_HASH_BITS);
}

static int mf_init(MatchFinder *mf, uint32_t hash_bits, uint32_t max_depth, size_t block_size) {
    mf->hash_size = 1u << hash_bits;
    mf->max_depth = max_depth;

    mf->hash = (uint32_t *)malloc(sizeof(uint32_t) * mf->hash_size);
    if (!mf->hash) return -1;

    mf->chain = (uint32_t *)malloc(sizeof(uint32_t) * (block_size + 1));
    if (!mf->chain) {
        free(mf->hash);
        mf->hash = NULL;
        return -1;
    }

    /* Initialize hash table to "no entry" */
    memset(mf->hash, 0xFF, sizeof(uint32_t) * mf->hash_size);
    memset(mf->chain, 0xFF, sizeof(uint32_t) * (block_size + 1));

    return 0;
}

static void mf_free(MatchFinder *mf) {
    if (mf->hash) { free(mf->hash); mf->hash = NULL; }
    if (mf->chain) { free(mf->chain); mf->chain = NULL; }
}

/* Find the longest match at position pos */
static uint32_t mf_find_match(MatchFinder *mf, const uint8_t *data,
                               size_t block_size, uint32_t pos,
                               uint32_t *match_dist) {
    uint32_t hash;
    uint32_t cur;
    uint32_t best_len = LZ_MIN_MATCH - 1;
    uint32_t best_dist = 0;
    uint32_t depth = 0;
    uint32_t max_len;
    uint32_t hash_mask = mf->hash_size - 1;

    *match_dist = 0;

    /* Need at least 4 bytes for hash */
    if (pos + 4 > block_size) return 0;

    max_len = (uint32_t)(block_size - pos);
    if (max_len > LZ_MAX_MATCH) max_len = LZ_MAX_MATCH;

    hash = lz_hash4(data + pos) & hash_mask;
    cur = mf->hash[hash];

    while (cur != 0xFFFFFFFFu && depth < mf->max_depth) {
        uint32_t dist = pos - cur;
        uint32_t len = 0;

        if (dist > 0 && dist <= block_size) {
            /* Compare bytes */
            while (len < max_len && data[cur + len] == data[pos + len]) {
                len++;
            }
            if (len > best_len) {
                best_len = len;
                best_dist = dist;
                if (len >= max_len) break;
            }
        }

        /* Follow chain */
        if (cur < block_size + 1) {
            cur = mf->chain[cur];
        } else {
            break;
        }
        depth++;
    }

    if (best_len >= LZ_MIN_MATCH) {
        *match_dist = best_dist;
        return best_len;
    }
    return 0;
}

/* Insert position into hash chain */
static void mf_insert(MatchFinder *mf, const uint8_t *data,
                       uint32_t hash_mask, uint32_t pos) {
    uint32_t hash;
    if (pos + 4 > (uint32_t)(mf->hash_size * 4)) {
        /* Safety: only hash if we have enough data */
    }
    hash = lz_hash4(data + pos) & hash_mask;
    mf->chain[pos] = mf->hash[hash];
    mf->hash[hash] = pos;
}

/* =========================================================================
 * Section 12: Match Context Computation
 * ========================================================================= */

/* Compute is_match context from previous byte and match state */
static int match_context(uint8_t prev_byte, int match_state) {
    int byte_class = prev_byte >> 5;  /* 0-7 */
    int state = match_state & 3;      /* 0-3 */
    return byte_class * 4 + state;
}

/* =========================================================================
 * Section 13: Optimal Parser (Dynamic Programming)
 * =========================================================================
 *
 * For compression level >= 5, we use optimal parsing:
 *
 * 1. Find longest match at each position using the hash chain finder
 * 2. Run DP from right to left:
 *    cost[i] = min(literal_cost + cost[i+1],
 *                  min over matches: match_cost + cost[i + match_len])
 * 3. Trace forward to produce optimal token sequence
 *
 * Cost model uses approximate bit costs:
 *   literal_cost ≈ 8 bits (uniform estimate)
 *   match_cost ≈ 1 + log2(len) + log2(dist) bits
 */

typedef struct {
    uint8_t is_match;   /* 0 = literal, 1 = match */
    uint8_t literal;    /* literal byte (if is_match == 0) */
    uint16_t match_len; /* match length (if is_match == 1) */
    uint32_t match_dist;/* match distance (if is_match == 1) */
} OptToken;

/* Approximate bit cost for encoding a match */
static int cost_match(uint32_t length, uint32_t dist) {
    int len_bits = 0, dist_bits = 0;
    uint32_t v;

    /* Length cost: ~log2(length) + overhead */
    v = length - LZ_MIN_MATCH;
    if (v < 4) {
        len_bits = 3;  /* 1 flag + 2 bits */
    } else {
        int nbits = 0;
        while (v > 0) { nbits++; v >>= 1; }
        len_bits = 1 + (nbits - 2) + nbits;  /* flag + unary + bits */
    }

    /* Distance cost: slot + extra bits */
    if (dist <= 4) {
        dist_bits = dist;  /* unary for slot */
    } else {
        int slot = 0;
        uint32_t d = dist - 1;
        while (d > ((1u << slot) - 1)) slot++;
        dist_bits = slot + slot_extra_bits(slot);
    }

    return 1 + len_bits + dist_bits;  /* 1 for is_match flag */
}

/* Run optimal parser on a block */
static int optimal_parse(const uint8_t *data, uint32_t block_size,
                          MatchFinder *mf, OptToken *tokens,
                          uint32_t *num_tokens) {
    uint32_t *cost;         /* cost[i] = min cost to encode data[0..i-1] */
    uint8_t *choice;        /* choice[i] = 0 for literal, 1 for match */
    uint16_t *choice_len;
    uint32_t *choice_dist;
    uint32_t *match_lens;   /* Pre-computed longest match at each position */
    uint32_t *match_dists;  /* Pre-computed match distance at each position */
    uint32_t i;
    uint32_t hash_mask = mf->hash_size - 1;

    cost = (uint32_t *)malloc(sizeof(uint32_t) * (block_size + 1));
    choice = (uint8_t *)malloc(sizeof(uint8_t) * (block_size + 1));
    choice_len = (uint16_t *)malloc(sizeof(uint16_t) * (block_size + 1));
    choice_dist = (uint32_t *)malloc(sizeof(uint32_t) * (block_size + 1));
    match_lens = (uint32_t *)malloc(sizeof(uint32_t) * block_size);
    match_dists = (uint32_t *)malloc(sizeof(uint32_t) * block_size);

    if (!cost || !choice || !choice_len || !choice_dist || !match_lens || !match_dists) {
        if (cost) free(cost);
        if (choice) free(choice);
        if (choice_len) free(choice_len);
        if (choice_dist) free(choice_dist);
        if (match_lens) free(match_lens);
        if (match_dists) free(match_dists);
        return -1;
    }

    /* Phase 1: Find matches left-to-right (building hash chains correctly) */
    memset(match_lens, 0, sizeof(uint32_t) * block_size);
    memset(match_dists, 0, sizeof(uint32_t) * block_size);

    for (i = 0; i < block_size; i++) {
        if (i + LZ_MIN_MATCH <= block_size) {
            uint32_t mdist;
            uint32_t mlen = mf_find_match(mf, data, block_size, i, &mdist);
            if (mlen >= LZ_MIN_MATCH) {
                match_lens[i] = mlen;
                match_dists[i] = mdist;
            }
        }
        /* Insert this position into hash chain */
        if (i + 4 <= block_size) {
            mf_insert(mf, data, hash_mask, i);
        }
    }

    /* Phase 2: Forward DP to find optimal path */
    /* cost[i] = minimum cost to encode data[0..i-1] */
    for (i = 0; i <= block_size; i++) {
        cost[i] = 0xFFFFFFFFu;  /* infinity */
    }
    cost[0] = 0;

    for (i = 0; i < block_size; i++) {
        if (cost[i] == 0xFFFFFFFFu) continue;  /* unreachable */

        /* Option 1: literal at position i */
        {
            uint32_t c = cost[i] + 8;  /* ~8 bits for literal */
            if (c < cost[i + 1]) {
                cost[i + 1] = c;
                choice[i + 1] = 0;
                choice_len[i + 1] = 0;
                choice_dist[i + 1] = 0;
            }
        }

        /* Option 2: match at position i */
        if (match_lens[i] >= LZ_MIN_MATCH) {
            uint32_t mlen = match_lens[i];
            uint32_t mdist = match_dists[i];
            uint32_t len;

            /* Consider matches of length 3 to mlen */
            for (len = LZ_MIN_MATCH; len <= mlen; len++) {
                uint32_t c = cost[i] + (uint32_t)cost_match(len, mdist);
                if (c < cost[i + len]) {
                    cost[i + len] = c;
                    choice[i + len] = 1;
                    choice_len[i + len] = (uint16_t)len;
                    choice_dist[i + len] = mdist;
                }
            }
        }
    }

    /* Phase 3: Trace backward to produce token sequence */
    {
        uint32_t pos = block_size;
        uint32_t ntok = 0;
        uint32_t temp_count = 0;

        /* Count tokens first */
        pos = block_size;
        while (pos > 0) {
            if (choice[pos] == 0) {
                /* Literal: came from pos-1 via literal */
                pos--;
            } else {
                /* Match: came from pos - match_len via match */
                pos -= choice_len[pos];
            }
            temp_count++;
        }

        /* Now trace forward and fill tokens */
        /* We need to reverse the trace, so store in reverse */
        {
            OptToken *rev_tokens = (OptToken *)malloc(sizeof(OptToken) * temp_count);
            uint32_t ridx;

            if (!rev_tokens) {
                free(cost); free(choice); free(choice_len);
                free(choice_dist); free(match_lens); free(match_dists);
                return -1;
            }

            pos = block_size;
            ridx = 0;
            while (pos > 0) {
                if (choice[pos] == 0) {
                    /* Literal at pos-1 */
                    rev_tokens[ridx].is_match = 0;
                    rev_tokens[ridx].literal = data[pos - 1];
                    pos--;
                } else {
                    /* Match ending at pos */
                    uint16_t mlen = choice_len[pos];
                    uint32_t start = pos - mlen;
                    rev_tokens[ridx].is_match = 1;
                    rev_tokens[ridx].match_len = mlen;
                    rev_tokens[ridx].match_dist = choice_dist[pos];
                    (void)start;
                    pos -= mlen;
                }
                ridx++;
            }

            /* Reverse into output tokens */
            for (i = 0; i < temp_count; i++) {
                tokens[i] = rev_tokens[temp_count - 1 - i];
            }
            *num_tokens = temp_count;
            free(rev_tokens);
        }
    }

    free(cost);
    free(choice);
    free(choice_len);
    free(choice_dist);
    free(match_lens);
    free(match_dists);
    return 0;
}

/* =========================================================================
 * Section 14: Block Encoder
 * =========================================================================
 *
 * Encodes a single block:
 * 1. Find matches using LZ77
 * 2. Optionally run optimal parser (level >= 5)
 * 3. Encode tokens using range coder with adaptive models
 */

/* Encode block using greedy parsing with lazy matching */
static int encode_block_greedy(const uint8_t *data, uint32_t block_size,
                                int level, RangeEnc *rc, CodecModels *models) {
    MatchFinder mf;
    uint32_t hash_bits, max_depth;
    uint32_t pos = 0;
    uint8_t prev_byte = 0;
    int match_state = 0;
    uint32_t hash_mask;

    /* Configure match finder based on level */
    if (level <= 2) { hash_bits = 14; max_depth = 4; }
    else if (level <= 4) { hash_bits = 16; max_depth = 16; }
    else if (level <= 6) { hash_bits = 18; max_depth = 64; }
    else if (level <= 8) { hash_bits = 20; max_depth = 128; }
    else { hash_bits = 22; max_depth = 256; }

    if (mf_init(&mf, hash_bits, max_depth, block_size) != 0) {
        return -1;
    }
    hash_mask = mf.hash_size - 1;

    while (pos < block_size) {
        uint32_t match_dist;
        uint32_t match_len = 0;

        /* Try to find a match */
        if (pos + LZ_MIN_MATCH <= block_size) {
            match_len = mf_find_match(&mf, data, block_size, pos, &match_dist);
        }

        /* Lazy matching: check if next position has a better match */
        if (match_len >= LZ_MIN_MATCH && level >= 3 && pos + 1 < block_size) {
            uint32_t next_dist;
            uint32_t next_len = 0;

            /* Insert current position first */
            if (pos + 4 <= block_size) {
                mf_insert(&mf, data, hash_mask, pos);
            }

            if (pos + 1 + LZ_MIN_MATCH <= block_size) {
                next_len = mf_find_match(&mf, data, block_size, pos + 1, &next_dist);
            }

            if (next_len > match_len + 1) {
                /* Better match at next position: emit literal now */
                int ctx = match_context(prev_byte, match_state);
                rc_enc_bit(rc, 0, models->is_match[ctx]);
                prob_update(&models->is_match[ctx], 0);
                enc_literal(rc, models, prev_byte, data[pos]);
                prev_byte = data[pos];
                match_state = 0;
                pos++;
                continue;
            }
        }

        if (match_len >= LZ_MIN_MATCH) {
            /* Encode match */
            int ctx = match_context(prev_byte, match_state);
            rc_enc_bit(rc, 1, models->is_match[ctx]);
            prob_update(&models->is_match[ctx], 1);
            enc_length(rc, models, match_len);
            enc_distance(rc, models, match_dist);

            /* Insert positions into hash chain */
            {
                uint32_t j;
                for (j = 0; j < match_len && pos + j + 4 <= block_size; j++) {
                    mf_insert(&mf, data, hash_mask, pos + j);
                }
            }

            prev_byte = data[pos + match_len - 1];
            match_state = (match_state < 3) ? match_state + 1 : 3;
            pos += match_len;
        } else {
            /* Encode literal */
            int ctx = match_context(prev_byte, match_state);
            rc_enc_bit(rc, 0, models->is_match[ctx]);
            prob_update(&models->is_match[ctx], 0);
            enc_literal(rc, models, prev_byte, data[pos]);

            if (pos + 4 <= block_size) {
                mf_insert(&mf, data, hash_mask, pos);
            }

            prev_byte = data[pos];
            match_state = 0;
            pos++;
        }
    }

    mf_free(&mf);
    return 0;
}

/* Encode block using optimal parsing */
/* Encode block using optimal parsing with correct prev_byte tracking */
static int encode_block_optimal(const uint8_t *data, uint32_t block_size,
                                    int level, RangeEnc *rc, CodecModels *models) {
    MatchFinder mf;
    OptToken *tokens;
    uint32_t num_tokens = 0;
    uint32_t hash_bits, max_depth;
    uint32_t i, pos = 0;
    uint8_t prev_byte = 0;
    int match_state = 0;

    if (level <= 6) { hash_bits = 18; max_depth = 32; }
    else if (level <= 8) { hash_bits = 20; max_depth = 128; }
    else { hash_bits = 22; max_depth = 256; }

    if (mf_init(&mf, hash_bits, max_depth, block_size) != 0) return -1;

    tokens = (OptToken *)malloc(sizeof(OptToken) * block_size);
    if (!tokens) { mf_free(&mf); return -1; }

    if (optimal_parse(data, block_size, &mf, tokens, &num_tokens) != 0) {
        free(tokens);
        mf_free(&mf);
        return -1;
    }
    mf_free(&mf);

    /* Encode tokens, tracking position for correct prev_byte */
    pos = 0;
    for (i = 0; i < num_tokens; i++) {
        int ctx = match_context(prev_byte, match_state);

        if (tokens[i].is_match == 0) {
            rc_enc_bit(rc, 0, models->is_match[ctx]);
            prob_update(&models->is_match[ctx], 0);
            enc_literal(rc, models, prev_byte, data[pos]);
            prev_byte = data[pos];
            match_state = 0;
            pos++;
        } else {
            uint32_t mlen = tokens[i].match_len;
            rc_enc_bit(rc, 1, models->is_match[ctx]);
            prob_update(&models->is_match[ctx], 1);
            enc_length(rc, models, mlen);
            enc_distance(rc, models, tokens[i].match_dist);
            prev_byte = data[pos + mlen - 1];
            match_state = (match_state < 3) ? match_state + 1 : 3;
            pos += mlen;
        }
    }

    free(tokens);
    return 0;
}

/* =========================================================================
 * Section 15: Block Decoder
 * =========================================================================
 *
 * Fast, simple decompression:
 * 1. Read range-coded symbols
 * 2. Decode is_match flag
 * 3. If literal: decode byte, write to output
 * 4. If match: decode length and distance, copy from window
 *
 * No hash tables, no match search, no DP — just decode and copy.
 */

static int decode_block(const uint8_t *compressed, size_t comp_size,
                         uint8_t *output, uint32_t uncomp_size) {
    RangeDec rc;
    CodecModels models;
    uint32_t pos = 0;
    uint8_t prev_byte = 0;
    int match_state = 0;

    if (models_init(&models) != 0) return -1;
    rc_dec_init(&rc, compressed, comp_size);

    while (pos < uncomp_size) {
        int ctx = match_context(prev_byte, match_state);
        int is_match = rc_dec_bit(&rc, models.is_match[ctx]);
        prob_update(&models.is_match[ctx], is_match);

        if (is_match == 0) {
            /* Literal */
            uint8_t byte = dec_literal(&rc, &models, prev_byte);
            output[pos] = byte;
            prev_byte = byte;
            match_state = 0;
            pos++;
        } else {
            /* Match */
            uint32_t length = dec_length(&rc, &models);
            uint32_t distance = dec_distance(&rc, &models);
            uint32_t j;

            /* Validate */
            if (distance > pos || distance == 0) {
                models_free(&models);
                return -1;  /* Corrupted data */
            }
            if (pos + length > uncomp_size) {
                models_free(&models);
                return -1;  /* Corrupted data */
            }

            /* Copy from window (byte-by-byte for overlapping matches) */
            for (j = 0; j < length; j++) {
                output[pos + j] = output[pos - distance + j];
            }

            prev_byte = output[pos + length - 1];
            match_state = (match_state < 3) ? match_state + 1 : 3;
            pos += length;
        }
    }

    models_free(&models);
    return 0;
}

/* =========================================================================
 * Section 16: Block Compression
 * ========================================================================= */

static int compress_block(const uint8_t *data, uint32_t block_size,
                           int level, uint8_t *out, size_t out_cap,
                           size_t *out_size) {
    RangeEnc rc;
    CodecModels models;

    if (models_init(&models) != 0) return -1;
    rc_enc_init(&rc, out, out_cap);

    if (level >= 5) {
        if (encode_block_optimal(data, block_size, level, &rc, &models) != 0) {
            models_free(&models);
            return -1;
        }
    } else {
        if (encode_block_greedy(data, block_size, level, &rc, &models) != 0) {
            models_free(&models);
            return -1;
        }
    }

    rc_enc_flush(&rc);
    *out_size = rc.out_pos;

    models_free(&models);
    return 0;
}

/* =========================================================================
 * Section 17: Public API — ponos_compress
 * ========================================================================= */

int ponos_compress(const uint8_t *src, size_t src_len,
                   uint8_t **dst, size_t *dst_len, int level) {
    uint32_t block_size;
    uint32_t num_blocks;
    size_t total_out;
    size_t out_pos;
    uint8_t *out_buf;
    size_t out_cap;
    uint32_t i;

    *dst = NULL;
    *dst_len = 0;

    /* Validate parameters */
    if (level < PONOS_MIN_LEVEL || level > PONOS_MAX_LEVEL) {
        return EINVAL;
    }

    /* Handle empty input */
    if (src_len == 0) {
        *dst = (uint8_t *)malloc(PONOS_HEADER_SIZE);
        if (!*dst) return ENOMEM;
        /* Write header with 0 blocks */
        write_u8(*dst + 0, PONOS_MAGIC_0);
        write_u8(*dst + 1, PONOS_MAGIC_1);
        write_u8(*dst + 2, PONOS_MAGIC_2);
        write_u8(*dst + 3, PONOS_MAGIC_3);
        write_u8(*dst + 4, PONOS_VERSION);
        write_u32(*dst + 5, PONOS_DEFAULT_BLOCK_SIZE);
        write_u64(*dst + 9, 0);
        write_u32(*dst + 17, 0);
        *dst_len = PONOS_HEADER_SIZE;
        return 0;
    }

    block_size = PONOS_DEFAULT_BLOCK_SIZE;
    num_blocks = (uint32_t)((src_len + block_size - 1) / block_size);

    /* Allocate output buffer (worst case: slightly larger than input) */
    out_cap = PONOS_HEADER_SIZE + num_blocks * PONOS_BLOCK_HEADER_SIZE + src_len + src_len / 4 + 1024;
    out_buf = (uint8_t *)malloc(out_cap);
    if (!out_buf) return ENOMEM;

    /* Write file header */
    write_u8(out_buf + 0, PONOS_MAGIC_0);
    write_u8(out_buf + 1, PONOS_MAGIC_1);
    write_u8(out_buf + 2, PONOS_MAGIC_2);
    write_u8(out_buf + 3, PONOS_MAGIC_3);
    write_u8(out_buf + 4, PONOS_VERSION);
    write_u32(out_buf + 5, block_size);
    write_u64(out_buf + 9, (uint64_t)src_len);
    write_u32(out_buf + 17, num_blocks);

    out_pos = PONOS_HEADER_SIZE;

    /* Compress each block */
    for (i = 0; i < num_blocks; i++) {
        size_t block_start = (size_t)i * block_size;
        uint32_t this_block_size = block_size;
        size_t comp_buf_size;
        uint8_t *comp_buf;
        uint32_t crc;
        size_t block_out_start;

        if (block_start + this_block_size > src_len) {
            this_block_size = (uint32_t)(src_len - block_start);
        }

        /* Reserve space for block header */
        block_out_start = out_pos;
        out_pos += PONOS_BLOCK_HEADER_SIZE;

        /* Compress block */
        comp_buf = out_buf + out_pos;
        if (compress_block(src + block_start, this_block_size, level,
                           comp_buf, out_cap - out_pos, &comp_buf_size) != 0) {
            free(out_buf);
            return EIO;
        }

        /* Compute CRC of uncompressed data */
        crc = crc32_compute(src + block_start, this_block_size);

        /* Write block header */
        write_u32(out_buf + block_out_start, (uint32_t)comp_buf_size);
        write_u32(out_buf + block_out_start + 4, this_block_size);
        write_u32(out_buf + block_out_start + 8, crc);

        out_pos += comp_buf_size;
    }

    total_out = out_pos;

    /* Shrink output to actual size */
    {
        uint8_t *final_buf = (uint8_t *)realloc(out_buf, total_out);
        if (final_buf) out_buf = final_buf;
        /* If realloc fails, we still have the original (larger) buffer — that's OK */
    }

    *dst = out_buf;
    *dst_len = total_out;
    return 0;
}

/* =========================================================================
 * Section 18: Public API — ponos_decompress
 * ========================================================================= */

int ponos_decompress(const uint8_t *src, size_t src_len,
                     uint8_t **dst, size_t *dst_len) {
    uint32_t block_size;
    uint64_t original_size;
    uint32_t num_blocks;
    uint8_t *out_buf;
    size_t out_pos;
    size_t in_pos;
    uint32_t i;

    *dst = NULL;
    *dst_len = 0;

    /* Validate minimum size for header */
    if (src_len < PONOS_HEADER_SIZE) return EINVAL;

    /* Check magic */
    if (src[0] != PONOS_MAGIC_0 || src[1] != PONOS_MAGIC_1 ||
        src[2] != PONOS_MAGIC_2 || src[3] != PONOS_MAGIC_3) {
        return EINVAL;
    }

    /* Read header */
    {
        uint8_t version = read_u8(src + 4);
        if (version != PONOS_VERSION) {
            return EINVAL;  /* Unsupported version */
        }
    }
    block_size = read_u32(src + 5);
    original_size = read_u64(src + 9);
    num_blocks = read_u32(src + 17);

    /* Validate header fields */
    if (block_size < PONOS_MIN_BLOCK_SIZE || block_size > PONOS_MAX_BLOCK_SIZE) {
        return EINVAL;
    }
    if (original_size > (uint64_t)SIZE_MAX / 2) {
        return ENOMEM;
    }

    /* Handle empty input */
    if (original_size == 0 && num_blocks == 0) {
        *dst = (uint8_t *)malloc(1);
        if (!*dst) return ENOMEM;
        *dst_len = 0;
        return 0;
    }

    /* Allocate output buffer */
    out_buf = (uint8_t *)malloc((size_t)original_size + 1);
    if (!out_buf) return ENOMEM;

    in_pos = PONOS_HEADER_SIZE;
    out_pos = 0;

    /* Decompress each block */
    for (i = 0; i < num_blocks; i++) {
        uint32_t comp_size, uncomp_size, crc_stored, crc_computed;
        const uint8_t *comp_data;

        /* Read block header */
        if (in_pos + PONOS_BLOCK_HEADER_SIZE > src_len) {
            free(out_buf);
            return EINVAL;
        }

        comp_size = read_u32(src + in_pos);
        uncomp_size = read_u32(src + in_pos + 4);
        crc_stored = read_u32(src + in_pos + 8);
        in_pos += PONOS_BLOCK_HEADER_SIZE;

        /* Validate */
        if (in_pos + comp_size > src_len) {
            free(out_buf);
            return EINVAL;
        }
        if (out_pos + uncomp_size > (size_t)original_size) {
            free(out_buf);
            return EINVAL;
        }

        comp_data = src + in_pos;

        /* Decompress block */
        if (decode_block(comp_data, comp_size, out_buf + out_pos, uncomp_size) != 0) {
            free(out_buf);
            return EIO;
        }

        /* Verify CRC */
        crc_computed = crc32_compute(out_buf + out_pos, uncomp_size);
        if (crc_computed != crc_stored) {
            free(out_buf);
            return EIO;  /* Data corruption detected */
        }

        in_pos += comp_size;
        out_pos += uncomp_size;
    }

    /* Verify total size */
    if (out_pos != (size_t)original_size) {
        free(out_buf);
        return EIO;
    }

    *dst = out_buf;
    *dst_len = (size_t)original_size;
    return 0;
}
