# ms2: matching statistics on Orbit move structures.  The source and its own
# Makefile are in src/; check out the submodules first with
# git submodule update --init.

.PHONY: all ms2 test debug clean help

help:
	@echo "Usage: make [target]"
	@echo "Targets:"
	@echo "  all: build src/ms2"
	@echo "  test: build src/ms2 and run its tests against data/"
	@echo "  debug: build src/ms2 with -O0 -g"
	@echo "  clean: remove build outputs"

all: ms2

ms2:
	$(MAKE) -C src

test: ms2
	./src/ms2 test data
	@echo "==============================================="
	@echo "     All ms2 tests passed"
	@echo "==============================================="

debug:
	$(MAKE) -C src debug

clean:
	$(MAKE) -C src clean
