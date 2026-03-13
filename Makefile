default: all

.DEFAULT:
	cd src && $(MAKE) $@

install:
	cd src && $(MAKE) $@

modules:
	cd src/modules && $(MAKE) all

test: modules
	-rm -rf tests/tmp && mkdir -p tests/tmp
	./runtest

.PHONY: install test modules