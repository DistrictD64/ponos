/*
 * Ponos Codec — Command Line Interface
 * =====================================
 *
 * Usage:
 *   ponos c input output [level]   Compress input to output
 *   ponos d input output           Decompress input to output
 *
 * Level: 1-9 (default: 5)
 *
 * Exit codes:
 *   0 = success
 *   1 = error
 */

#include "ponos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* Read entire file into memory */
static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f;
    long fsize;
    uint8_t *buf;
    size_t nread;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: fseek failed on '%s': %s\n", path, strerror(errno));
        fclose(f);
        return NULL;
    }

    fsize = ftell(f);
    if (fsize < 0) {
        fprintf(stderr, "Error: ftell failed on '%s': %s\n", path, strerror(errno));
        fclose(f);
        return NULL;
    }

    rewind(f);

    *out_size = (size_t)fsize;

    if (fsize == 0) {
        fclose(f);
        buf = (uint8_t *)malloc(1);
        if (!buf) {
            fprintf(stderr, "Error: out of memory\n");
            return NULL;
        }
        return buf;
    }

    buf = (uint8_t *)malloc((size_t)fsize);
    if (!buf) {
        fprintf(stderr, "Error: out of memory allocating %ld bytes\n", fsize);
        fclose(f);
        return NULL;
    }

    nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);

    if (nread != (size_t)fsize) {
        fprintf(stderr, "Error: short read on '%s' (got %zu, expected %ld)\n",
                path, nread, fsize);
        free(buf);
        return NULL;
    }

    return buf;
}

/* Write buffer to file */
static int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f;
    size_t nwritten;

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s' for writing: %s\n",
                path, strerror(errno));
        return -1;
    }

    if (size > 0) {
        nwritten = fwrite(data, 1, size, f);
        if (nwritten != size) {
            fprintf(stderr, "Error: short write on '%s' (wrote %zu, expected %zu)\n",
                    path, nwritten, size);
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    return 0;
}

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Ponos Lossless Compression Codec\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s c <input> <output> [level]  Compress (level 1-9, default 5)\n", prog);
    fprintf(stderr, "  %s d <input> <output>          Decompress\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s c file.txt file.pono 7      Compress with level 7\n", prog);
    fprintf(stderr, "  %s d file.pono file.txt        Decompress\n", prog);
}

int main(int argc, char *argv[]) {
    const char *input_path, *output_path;
    uint8_t *input_data = NULL, *output_data = NULL;
    size_t input_size = 0, output_size = 0;
    int mode;  /* 'c' = compress, 'd' = decompress */
    int level = 5;
    int ret;
    double t_start, t_end;

    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    mode = argv[1][0];
    if (mode != 'c' && mode != 'd') {
        fprintf(stderr, "Error: mode must be 'c' (compress) or 'd' (decompress)\n");
        print_usage(argv[0]);
        return 1;
    }

    input_path = argv[2];
    output_path = argv[3];

    if (mode == 'c' && argc >= 5) {
        level = atoi(argv[4]);
        if (level < PONOS_MIN_LEVEL || level > PONOS_MAX_LEVEL) {
            fprintf(stderr, "Error: level must be %d-%d\n",
                    PONOS_MIN_LEVEL, PONOS_MAX_LEVEL);
            return 1;
        }
    }

    /* Read input */
    fprintf(stderr, "Reading '%s'...\n", input_path);
    input_data = read_file(input_path, &input_size);
    if (!input_data) return 1;
    fprintf(stderr, "  Input size: %zu bytes\n", input_size);

    if (mode == 'c') {
        /* Compress */
        fprintf(stderr, "Compressing (level %d)...\n", level);
        t_start = get_time_sec();

        ret = ponos_compress(input_data, input_size, &output_data, &output_size, level);

        t_end = get_time_sec();

        if (ret != 0) {
            fprintf(stderr, "Error: compression failed (code %d)\n", ret);
            free(input_data);
            return 1;
        }

        fprintf(stderr, "  Output size: %zu bytes\n", output_size);
        if (input_size > 0) {
            fprintf(stderr, "  Ratio: %.2f%%\n", 100.0 * output_size / input_size);
        }
        fprintf(stderr, "  Time: %.3f seconds\n", t_end - t_start);
        if (input_size > 0 && (t_end - t_start) > 0) {
            fprintf(stderr, "  Speed: %.2f MB/s\n",
                    (double)input_size / (t_end - t_start) / 1e6);
        }

    } else {
        /* Decompress */
        fprintf(stderr, "Decompressing...\n");
        t_start = get_time_sec();

        ret = ponos_decompress(input_data, input_size, &output_data, &output_size);

        t_end = get_time_sec();

        if (ret != 0) {
            fprintf(stderr, "Error: decompression failed (code %d)\n", ret);
            free(input_data);
            return 1;
        }

        fprintf(stderr, "  Output size: %zu bytes\n", output_size);
        fprintf(stderr, "  Time: %.3f seconds\n", t_end - t_start);
        if (output_size > 0 && (t_end - t_start) > 0) {
            fprintf(stderr, "  Speed: %.2f MB/s\n",
                    (double)output_size / (t_end - t_start) / 1e6);
        }
    }

    /* Write output */
    fprintf(stderr, "Writing '%s'...\n", output_path);
    if (write_file(output_path, output_data, output_size) != 0) {
        free(input_data);
        free(output_data);
        return 1;
    }

    fprintf(stderr, "Done.\n");

    free(input_data);
    free(output_data);
    return 0;
}
