default: all

.DEFAULT:
	cd src && $(MAKE) $@

install:
	cd src && $(MAKE) $@

modules:
	cd src/modules && $(MAKE) all

test: modules
	./runtest

.PHONY: install test modules