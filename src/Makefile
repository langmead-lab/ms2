# MS application: build from project root with make -C src
CXX = g++
CXXFLAGS = -std=c++17 -I../orbit/include -I. -O3

MS_SRCS = ms.cpp tsv.cpp ms_io.cpp ms_test.cpp inspect.cpp tms_test.cpp

MS_HDRS = $(wildcard *.hpp) $(shell find ../orbit/include -name '*.hpp')

ms2: $(MS_SRCS) $(MS_HDRS)
	$(CXX) $(CXXFLAGS) -o ms2 $(MS_SRCS)

# Benchmark build: assertions off, as ms2-bench.  The test commands refuse to
# run in it, since the suites check their results with assert.
ms2-bench: $(MS_SRCS) $(MS_HDRS)
	$(CXX) $(CXXFLAGS) -DNDEBUG -o ms2-bench $(MS_SRCS)

bench: ms2-bench

# Benchmark build that also counts LF fast-forward rows and reposition walk
# lengths in ms2 batch (see MsStats), as ms2-stats.
ms2-stats: $(MS_SRCS) $(MS_HDRS)
	$(CXX) $(CXXFLAGS) -DNDEBUG -DMS_STATS -o ms2-stats $(MS_SRCS)

# Debug build: -O0 -g for breakpoints (e.g. lldb ./ms2, then run test ../data)
debug: CXXFLAGS += -O0 -g
debug: ms2

# Microbenchmark of LF and psi step chains on a tms index
tms_chain_bench: tms_chain_bench.cpp $(MS_HDRS)
	$(CXX) $(CXXFLAGS) -o tms_chain_bench tms_chain_bench.cpp

.PHONY: test all clean debug bench
test: ms2
	./ms2 test ../data

all: ms2

clean:
	rm -f ms2 ms2-bench ms2-stats tms_chain_bench
