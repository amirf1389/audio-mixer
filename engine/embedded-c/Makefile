# Embedded C core: builds the tests with libm and with the built-in math (AMC_NO_LIBM), runs them against engine/tests/vectors.txt
CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -Werror -O2
VECTORS = ../tests/vectors.txt
test: build/core_test build/core_test_nolibm
	build/core_test $(VECTORS)
	build/core_test_nolibm $(VECTORS)
build/core_test: src/am_core.c test/core_test.c include/am_core.h src/am_math.h
	mkdir -p build && $(CC) $(CFLAGS) -o $@ src/am_core.c test/core_test.c -lm
build/core_test_nolibm: src/am_core.c test/core_test.c include/am_core.h src/am_math.h
	mkdir -p build && $(CC) $(CFLAGS) -DAMC_NO_LIBM -c src/am_core.c -o build/am_core_nolibm.o && $(CC) -std=c99 -O2 -o $@ build/am_core_nolibm.o test/core_test.c -lm
clean:
	rm -rf build
