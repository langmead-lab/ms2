# MS application: build from project root with make -C src
CXX = g++
CXXFLAGS = -std=c++17 -I../orbit/include -I. -O3

MS_SRCS = ms.cpp tsv.cpp ms_io.cpp ms_test.cpp inspect.cpp tms_test.cpp

MS_HDRS = $(wildcard *.hpp) $(shell find ../orbit/include -name '*.hpp')

ms2: $(MS_SRCS) $(MS_HDRS)
	$(CXX) $(CXXFLAGS) -o ms2 $(MS_SRCS)

# Debug build: -O0 -g for breakpoints (e.g. lldb ./ms2, then run test ../data)
debug: CXXFLAGS += -O0 -g
debug: ms2

.PHONY: test all clean debug
test: ms2
	./ms2 test ../data

all: ms2

clean:
	rm -f ms
