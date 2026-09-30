# Ponos Lossless Compression Codec — Makefile
# =============================================
#
# Targets:
#   all       — Build the ponos CLI tool
#   test      — Build and run the test suite
#   bench     — Build and run the benchmark
#   clean     — Remove build artifacts
#
# Compiler settings:
#   - Pure C11, no extensions
#   - No CPU-specific flags (no -march=native, -msse, -mavx, -mbmi)
#   - Portable: works on any platform

CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -pedantic
LDFLAGS =

# Source files
PONOS_SRC = ponos.c
CLI_SRC = main.c
TEST_SRC = test.c

# Object files
PONOS_OBJ = ponos.o
CLI_OBJ = main.o
TEST_OBJ = test.o

# Targets
PONOS_BIN = ponos
TEST_BIN = ponos_test

.PHONY: all test bench clean

all: $(PONOS_BIN)

$(PONOS_BIN): $(PONOS_OBJ) $(CLI_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(TEST_BIN): $(PONOS_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c ponos.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_BIN)
	./$(TEST_BIN)

bench: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(PONOS_OBJ) $(CLI_OBJ) $(TEST_OBJ) $(PONOS_BIN) $(TEST_BIN)
