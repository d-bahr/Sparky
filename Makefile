all: bin/chess

bin:
	mkdir -p bin

SKIP_C_FILES := OpeningBookGeneration.c OpeningBookGenerated.c

bin/chess: $(filter-out $(SKIP_C_FILES), $(wildcard *.c)) $(wildcard ./tables/*.c) $(wildcard *.h) $(wildcard ./tables/*.h) | bin
	gcc -std=gnu11 -march=native -mbmi -mbmi2 -mlzcnt -D_POSIX_C_SOURCE=200809L -I. -I./tables -O3 -g -pthread $(filter-out $(SKIP_C_FILES), $(wildcard *.c)) $(wildcard ./tables/*.c) -o bin/chess

bin/unit-tests: $(filter-out main.c $(SKIP_C_FILES), $(wildcard *.c)) $(wildcard ./tables/*.c) $(wildcard *.h) $(wildcard ./tables/*.h) tools/UnitTests.c | bin
	gcc -std=gnu11 -march=native -mbmi -mbmi2 -mlzcnt -D_POSIX_C_SOURCE=200809L -I. -I./tables -O3 -g -pthread $(filter-out main.c $(SKIP_C_FILES), $(wildcard *.c)) $(wildcard ./tables/*.c) tools/UnitTests.c -o bin/unit-tests

clean:
	rm -f bin/chess

unit-tests: bin/unit-tests

clean-unit-tests:
	rm -f bin/unit-tests

PHONY: all unit-tests clean clean-unit-tests
