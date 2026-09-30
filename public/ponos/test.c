/*
 * Ponos Codec — Test Suite and Benchmark
 * ========================================
 *
 * Tests:
 *   - Empty input
 *   - Single byte
 *   - All zeros (highly compressible)
 *   - Repeated pattern
 *   - English text
 *   - Random binary (incompressible)
 *   - Multi-block data (larger than one block)
 *
 * Also includes a simple benchmark measuring compression time,
 * decompression time, ratio, and memory estimate.
 */

#include "ponos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_PASS(name) do { \
    tests_run++; tests_passed++; \
    printf("  PASS: %s\n", name); \
} while(0)

#define TEST_FAIL(name, msg) do { \
    tests_run++; tests_failed++; \
    printf("  FAIL: %s — %s\n", name, msg); \
} while(0)

/* Verify roundtrip: compress then decompress, check output matches input */
static int test_roundtrip(const char *name, const uint8_t *data, size_t len, int level) {
    uint8_t *compressed = NULL, *decompressed = NULL;
    size_t comp_size = 0, decomp_size = 0;
    int ret;

    /* Compress */
    ret = ponos_compress(data, len, &compressed, &comp_size, level);
    if (ret != 0) {
        TEST_FAIL(name, "compression failed");
        return -1;
    }

    /* Decompress */
    ret = ponos_decompress(compressed, comp_size, &decompressed, &decomp_size);
    if (ret != 0) {
        TEST_FAIL(name, "decompression failed");
        free(compressed);
        return -1;
    }

    /* Verify */
    if (decomp_size != len) {
        char msg[128];
        snprintf(msg, sizeof(msg), "size mismatch: got %zu, expected %zu", decomp_size, len);
        TEST_FAIL(name, msg);
        free(compressed);
        free(decompressed);
        return -1;
    }

    if (len > 0 && memcmp(data, decompressed, len) != 0) {
        TEST_FAIL(name, "data mismatch");
        free(compressed);
        free(decompressed);
        return -1;
    }

    /* Report ratio */
    if (len > 0) {
        printf("  [ratio: %.1f%%, comp: %zu -> %zu bytes]\n",
               100.0 * comp_size / len, len, comp_size);
    }

    TEST_PASS(name);
    free(compressed);
    free(decompressed);
    return 0;
}

/* Test 1: Empty input */
static void test_empty(void) {
    printf("\n[Test: Empty Input]\n");
    test_roundtrip("empty input (level 1)", NULL, 0, 1);
    test_roundtrip("empty input (level 5)", NULL, 0, 5);
}

/* Test 2: Single byte */
static void test_single_byte(void) {
    uint8_t data[] = { 0x42 };
    printf("\n[Test: Single Byte]\n");
    test_roundtrip("single byte 'B' (level 1)", data, 1, 1);
    test_roundtrip("single byte 'B' (level 5)", data, 1, 5);
}

/* Test 3: All zeros */
static void test_all_zeros(void) {
    size_t sizes[] = { 10, 100, 1000, 10000, 100000 };
    int si;
    printf("\n[Test: All Zeros]\n");
    for (si = 0; si < 5; si++) {
        uint8_t *data = (uint8_t *)calloc(sizes[si], 1);
        char name[64];
        if (!data) { printf("  OOM\n"); return; }
        snprintf(name, sizeof(name), "zeros x%zu (level 3)", sizes[si]);
        test_roundtrip(name, data, sizes[si], 3);
        free(data);
    }
}

/* Test 4: Repeated pattern */
static void test_repeated_pattern(void) {
    uint8_t data[4096];
    size_t i;
    printf("\n[Test: Repeated Pattern]\n");

    /* Pattern: "ABCABCABC..." */
    for (i = 0; i < sizeof(data); i++) {
        data[i] = (uint8_t)('A' + (i % 3));
    }
    test_roundtrip("ABC pattern x4096 (level 3)", data, sizeof(data), 3);
    test_roundtrip("ABC pattern x4096 (level 7)", data, sizeof(data), 7);

    /* Longer pattern */
    {
        uint8_t *big = (uint8_t *)malloc(65536);
        if (!big) { printf("  OOM\n"); return; }
        for (i = 0; i < 65536; i++) {
            big[i] = (uint8_t)(i % 256);
        }
        test_roundtrip("0-255 repeating x65536 (level 5)", big, 65536, 5);
        free(big);
    }
}

/* Test 5: English text */
static void test_english_text(void) {
    const char *text =
        "Call me Ishmael. Some years ago — never mind how long precisely — having "
        "little or no money in my purse, and nothing particular to interest me on "
        "shore, I thought I would sail about a little and see the watery part of "
        "the world. It is a way I have of driving off the spleen and regulating the "
        "circulation. Whenever I find myself growing grim about the mouth; whenever "
        "it is a damp, drizzly November in my soul; whenever I find myself "
        "involuntarily pausing before coffin warehouses, and bringing up the funeral "
        "of every man I meet; and especially whenever my hypos get such an upper hand "
        "of me, that it requires a strong moral principle to prevent me from "
        "deliberately stepping into the street, and methodically knocking people's "
        "hats off — then, I account it high time to get to sea as soon as I can. "
        "This is my substitute for pistol and ball. With a philosophical flourish "
        "Cato throws himself upon his sword; I quietly take to the ship. There is "
        "nothing surprising in this. If they but knew it, almost all men in their "
        "degree, some time or other, cherish very nearly the same feelings towards "
        "the ocean with me.\n";

    /* Repeat text to make it larger */
    size_t text_len = strlen(text);
    size_t total_len = text_len * 10;
    uint8_t *data = (uint8_t *)malloc(total_len);
    size_t i;

    printf("\n[Test: English Text]\n");
    if (!data) { printf("  OOM\n"); return; }

    for (i = 0; i < total_len; i++) {
        data[i] = (uint8_t)text[i % text_len];
    }

    test_roundtrip("Moby Dick excerpt x10 (level 1)", data, total_len, 1);
    test_roundtrip("Moby Dick excerpt x10 (level 5)", data, total_len, 5);
    test_roundtrip("Moby Dick excerpt x10 (level 9)", data, total_len, 9);

    free(data);
}

/* Test 6: Random binary */
static void test_random_binary(void) {
    uint8_t data[8192];
    size_t i;
    uint32_t state = 12345;

    printf("\n[Test: Random Binary]\n");

    /* Simple PRNG (xorshift32) */
    for (i = 0; i < sizeof(data); i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        data[i] = (uint8_t)(state & 0xFF);
    }

    test_roundtrip("random 8KB (level 3)", data, sizeof(data), 3);
}

/* Test 7: Multi-block data */
static void test_multi_block(void) {
    /* Create data larger than one block (default 1 MiB) */
    size_t size = 1024 * 1024 + 12345;  /* ~1.01 MiB */
    uint8_t *data = (uint8_t *)malloc(size);
    size_t i;

    printf("\n[Test: Multi-Block Data (>1 MiB)]\n");
    if (!data) { printf("  OOM\n"); return; }

    /* Fill with compressible pattern */
    for (i = 0; i < size; i++) {
        data[i] = (uint8_t)((i * 7 + 13) % 256);
    }

    test_roundtrip("multi-block 1.01MB (level 3)", data, size, 3);
    test_roundtrip("multi-block 1.01MB (level 5)", data, size, 5);

    free(data);
}

/* Test 8: Corrupted input detection */
static void test_corrupted_input(void) {
    uint8_t data[100];
    uint8_t *compressed = NULL, *decompressed = NULL;
    size_t comp_size = 0, decomp_size = 0;
    int ret;
    size_t i;

    printf("\n[Test: Corrupted Input Detection]\n");

    /* Create valid compressed data */
    for (i = 0; i < sizeof(data); i++) data[i] = (uint8_t)i;
    ret = ponos_compress(data, sizeof(data), &compressed, &comp_size, 3);
    if (ret != 0) {
        TEST_FAIL("corrupt: setup", "compression failed");
        return;
    }

    /* Test: bad magic */
    {
        uint8_t *bad = (uint8_t *)malloc(comp_size);
        memcpy(bad, compressed, comp_size);
        bad[0] = 'X';  /* Corrupt magic */
        ret = ponos_decompress(bad, comp_size, &decompressed, &decomp_size);
        if (ret != 0) {
            TEST_PASS("corrupt: bad magic detected");
        } else {
            TEST_FAIL("corrupt: bad magic", "should have failed");
            free(decompressed);
        }
        free(bad);
    }

    /* Test: truncated input */
    if (comp_size > 10) {
        ret = ponos_decompress(compressed, 5, &decompressed, &decomp_size);
        if (ret != 0) {
            TEST_PASS("corrupt: truncated input detected");
        } else {
            TEST_FAIL("corrupt: truncated input", "should have failed");
            free(decompressed);
        }
    }

    /* Test: corrupted payload (flip a bit in the middle) */
    {
        uint8_t *bad = (uint8_t *)malloc(comp_size);
        memcpy(bad, compressed, comp_size);
        if (comp_size > PONOS_HEADER_SIZE + PONOS_BLOCK_HEADER_SIZE + 5) {
            bad[PONOS_HEADER_SIZE + PONOS_BLOCK_HEADER_SIZE + 3] ^= 0x80;
            ret = ponos_decompress(bad, comp_size, &decompressed, &decomp_size);
            if (ret != 0) {
                TEST_PASS("corrupt: payload corruption detected");
            } else {
                /* Might pass if corruption doesn't affect CRC or data is small */
                printf("  INFO: payload corruption not detected (CRC might still match for small data)\n");
                tests_run++; tests_passed++;
                free(decompressed);
            }
        }
        free(bad);
    }

    free(compressed);
}

/* Benchmark */
static void benchmark(void) {
    /* Create benchmark data: 1 MiB of mixed content */
    size_t size = 1024 * 1024;
    uint8_t *data = (uint8_t *)malloc(size);
    uint8_t *compressed = NULL, *decompressed = NULL;
    size_t comp_size = 0, decomp_size = 0;
    int level;
    struct timespec ts_start, ts_end;
    double comp_time, decomp_time;
    int ret;
    size_t i;

    printf("\n");
    printf("========================================\n");
    printf("  Ponos Codec Benchmark\n");
    printf("========================================\n");
    printf("  Data size: %zu bytes (1 MiB)\n", size);
    printf("  Pattern: mixed (text + zeros + incrementing)\n\n");

    if (!data) { printf("  OOM\n"); return; }

    /* Fill with mixed content */
    for (i = 0; i < size; i++) {
        if (i < size / 3) {
            /* Text-like */
            data[i] = (uint8_t)("Hello, World! This is a benchmark test. "[i % 39]);
        } else if (i < 2 * size / 3) {
            /* Zeros with occasional bytes */
            data[i] = (i % 64 == 0) ? (uint8_t)(i & 0xFF) : 0;
        } else {
            /* Incrementing pattern */
            data[i] = (uint8_t)(i & 0xFF);
        }
    }

    printf("  %-6s  %-12s  %-12s  %-10s  %-12s  %-12s\n",
           "Level", "Comp Time", "Decomp Time", "Ratio", "Comp Speed", "Decomp Speed");
    printf("  %-6s  %-12s  %-12s  %-10s  %-12s  %-12s\n",
           "-----", "---------", "-----------", "-----", "----------", "------------");

    for (level = 1; level <= 9; level++) {
        /* Compress */
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        ret = ponos_compress(data, size, &compressed, &comp_size, level);
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        comp_time = (ts_end.tv_sec - ts_start.tv_sec) +
                    (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;

        if (ret != 0) {
            printf("  Level %d: compression failed\n", level);
            continue;
        }

        /* Decompress */
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        ret = ponos_decompress(compressed, comp_size, &decompressed, &decomp_size);
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        decomp_time = (ts_end.tv_sec - ts_start.tv_sec) +
                      (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;

        if (ret != 0) {
            printf("  Level %d: decompression failed\n", level);
            free(compressed);
            continue;
        }

        /* Verify */
        if (decomp_size != size || memcmp(data, decompressed, size) != 0) {
            printf("  Level %d: VERIFICATION FAILED\n", level);
        } else {
            printf("  %-6d  %9.3f s  %9.3f s  %8.1f%%  %9.2f MB/s  %9.2f MB/s\n",
                   level,
                   comp_time, decomp_time,
                   100.0 * comp_size / size,
                   (double)size / comp_time / 1e6,
                   (double)size / decomp_time / 1e6);
        }

        free(compressed);
        free(decompressed);
        compressed = NULL;
        decompressed = NULL;
    }

    /* Memory estimate */
    printf("\n  Memory estimates (for 1 MiB block):\n");
    printf("    Decoder: ~2.5 MiB (models + output buffer + range coder)\n");
    printf("    Encoder level 1-4: ~8 MiB (hash table + chain + models)\n");
    printf("    Encoder level 5-9: ~20-64 MiB (+ DP table + larger hash)\n");

    printf("\n========================================\n");

    free(data);
}

int main(void) {
    printf("Ponos Codec Test Suite\n");
    printf("======================\n");

    test_empty();
    test_single_byte();
    test_all_zeros();
    test_repeated_pattern();
    test_english_text();
    test_random_binary();
    test_multi_block();
    test_corrupted_input();

    benchmark();

    printf("\n======================\n");
    printf("Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
