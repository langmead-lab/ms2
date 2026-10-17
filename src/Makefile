# MS application: build from project root with make -C src
CXX = g++
CXXFLAGS = -std=c++17 -I../orbit/include -I. -O3 -pthread

# The commit recorded in the ftabs this binary builds; outside a git checkout
# (as from git archive), pass MS_COMMIT=... on the command line.
MS_COMMIT ?= $(shell c=$$(git rev-parse --short=12 HEAD 2>/dev/null) && { git diff --quiet HEAD -- . 2>/dev/null || c=$$c-dirty; echo $$c; } || echo unknown)
CXXFLAGS += -DMS_COMMIT='"$(MS_COMMIT)"'

MS_SRCS = ms.cpp tsv.cpp ms_io.cpp index_format.cpp ms_test.cpp inspect.cpp tms_test.cpp fmd_test.cpp

MS_HDRS = $(wildcard *.hpp) $(shell find ../orbit/include -name '*.hpp')

ms2: $(MS_SRCS) $(MS_HDRS)
	$(CXX) $(CXXFLAGS) -o ms2 $(MS_SRCS) -lz

# Benchmark build: assertions off, as ms2-bench.  The test commands refuse to
# run in it, since the suites check their results with assert.
ms2-bench: $(MS_SRCS) $(MS_HDRS)
	$(CXX) $(CXXFLAGS) -DNDEBUG -o ms2-bench $(MS_SRCS) -lz

bench: ms2-bench

# Benchmark build that also counts LF fast-forward rows and reposition walk
# lengths in ms2 batch (see MsStats), as ms2-stats.
ms2-stats: $(MS_SRCS) $(MS_HDRS)
	$(CXX) $(CXXFLAGS) -DNDEBUG -DMS_STATS -o ms2-stats $(MS_SRCS) -lz

# Benchmark build that also records, for ms2 batch and tms-batch with
# --interleave 0, where in each read the repositions, the rows they read and
# the query time fall (see step_stats.hpp), as ms2-steps.
ms2-steps: $(MS_SRCS) $(MS_HDRS)
	$(CXX) $(CXXFLAGS) -DNDEBUG -DMS_STATS -DTMS_STATS -DMS_STEP_STATS -o ms2-steps $(MS_SRCS) -lz

# Debug build: -O0 -g for breakpoints (e.g. lldb ./ms2, then run test ../data)
debug: CXXFLAGS += -O0 -g
debug: ms2

# Microbenchmark of LF and psi step chains on a tms index
tms_chain_bench: tms_chain_bench.cpp index_format.cpp $(MS_HDRS)
	$(CXX) $(CXXFLAGS) -o tms_chain_bench tms_chain_bench.cpp index_format.cpp

.PHONY: test test-threads test-reader test-ftab all clean debug bench
test: ms2
	./ms2 test ../data

# Read cutting at every block size and record offset, under the address and undefined
# behavior sanitizers
read_chunks_test: read_chunks_test.cpp read_chunks.hpp
	$(CXX) -std=c++17 -I. -O1 -g -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -o read_chunks_test read_chunks_test.cpp

test-reader: read_chunks_test
	./read_chunks_test

# Output identity across interleave, thread count and block size
test-threads: ms2
	./test_threads.sh ./ms2 ../data

# Stored ftabs: ftab-build, ftab-verify and byte identity with --ftab-file
test-ftab: ms2
	./test_ftab.sh ./ms2 ../data

all: ms2

clean:
	rm -f ms2 ms2-bench ms2-stats ms2-steps tms_chain_bench read_chunks_test
