# LATTE Makefile
# Copyright (C) 2009 Salvatore Sanfilippo <antirez at gmail dot com>
# This file is released under the BSD license, see the COPYING file
#
# The Makefile composes the final FINAL_CFLAGS and FINAL_LDFLAGS using
# what is needed for LATTE plus the standard CFLAGS and LDFLAGS passed.
# However when building the dependencies (Jemalloc, Lua, HiLATTE, ...)
# CFLAGS and LDFLAGS are propagated to the dependencies, so to pass
# flags only to be used when compiling / linking LATTE itself LATTE_CFLAGS
# and LATTE_LDFLAGS are used instead (this is the case of 'make gcov').
#
# Dependencies are stored in the Makefile.dep file. To rebuild this file
# Just use 'make dep', but this is only needed by developers.

# release_hdr := $(shell sh -c './mkreleasehdr.sh')
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo not')
OPTIMIZATION?=-O2
# DEPENDENCY_TARGETS=hiLATTE linenoise lua
NODEPS:=clean distclean

# Default settings
STD=-std=c99 -pedantic -DLATTE_STATIC=''
ifneq (,$(findstring clang,$(CC)))
ifneq (,$(findstring FreeBSD,$(uname_S)))
  STD+=-Wno-c11-extensions
endif
endif
WARN=-Wall -W -Wno-missing-field-initializers
OPT=$(OPTIMIZATION)

PREFIX?=/usr/local
INSTALL_BIN=$(PREFIX)/bin
INSTALL=install

# Default allocator defaults to Jemalloc if it's not an ARM
MALLOC=libc
ifneq ($(uname_M),armv6l)
ifneq ($(uname_M),armv7l)
ifeq ($(uname_S),Linux)
	MALLOC=jemalloc
endif
endif
endif

# To get ARM stack traces if LATTE crashes we need a special C flag.
ifneq (,$(filter aarch64 armv,$(uname_M)))
        CFLAGS+=-funwind-tables
else
ifneq (,$(findstring armv,$(uname_M)))
        CFLAGS+=-funwind-tables
endif
endif

# Backwards compatibility for selecting an allocator
ifeq ($(USE_TCMALLOC),yes)
	MALLOC=tcmalloc
endif

ifeq ($(USE_TCMALLOC_MINIMAL),yes)
	MALLOC=tcmalloc_minimal
endif

ifeq ($(USE_JEMALLOC),yes)
	MALLOC=jemalloc
endif

ifeq ($(USE_JEMALLOC),no)
	MALLOC=libc
endif

# Override default settings if possible
-include .make-settings

FINAL_CFLAGS=$(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS) $(LATTE_CFLAGS)
FINAL_LDFLAGS=$(LDFLAGS) $(LATTE_LDFLAGS) $(DEBUG)
FINAL_LIBS=-lm
DEBUG=-g -ggdb -pg

ifeq ($(TEST),tcl)
	FINAL_CFLAGS+= -DTCL_TEST
endif

ifeq ($(uname_S),SunOS)
	# SunOS
        ifneq ($(@@),32bit)
		CFLAGS+= -m64
		LDFLAGS+= -m64
	endif
	DEBUG=-g
	DEBUG_FLAGS=-g
	export CFLAGS LDFLAGS DEBUG DEBUG_FLAGS
	INSTALL=cp -pf
	FINAL_CFLAGS+= -D__EXTENSIONS__ -D_XPG6
	FINAL_LIBS+= -ldl -lnsl -lsocket -lresolv -lpthread -lrt
else
ifeq ($(uname_S),Darwin)
	# Darwin
	FINAL_LIBS+= -ldl
else
ifeq ($(uname_S),AIX)
        # AIX
        FINAL_LDFLAGS+= -Wl,-bexpall
        FINAL_LIBS+=-ldl -pthread -lcrypt -lbsd
else
ifeq ($(uname_S),OpenBSD)
	# OpenBSD
	FINAL_LIBS+= -lpthread
	ifeq ($(USE_BACKTRACE),yes)
	    FINAL_CFLAGS+= -DUSE_BACKTRACE -I/usr/local/include
	    FINAL_LDFLAGS+= -L/usr/local/lib
	    FINAL_LIBS+= -lexecinfo
    	endif

else
ifeq ($(uname_S),FreeBSD)
	# FreeBSD
	FINAL_LIBS+= -lpthread -lexecinfo
else
ifeq ($(uname_S),DragonFly)
	# FreeBSD
	FINAL_LIBS+= -lpthread -lexecinfo
else
	# All the other OSes (notably Linux)
	FINAL_LDFLAGS+= -rdynamic
	FINAL_LIBS+=-ldl -pthread -lrt
endif
endif
endif
endif
endif
endif
# Include paths to dependencies
# FINAL_CFLAGS+= -I./deps/hiLATTE -I./deps/linenoise -I./deps/lua/src
FINAL_CFLAGS+= -I./libs
ifeq ($(MALLOC),tcmalloc)

	FINAL_CFLAGS+= -DUSE_TCMALLOC
	FINAL_LIBS+= -ltcmalloc

endif

ifeq ($(MALLOC),tcmalloc_minimal)
	FINAL_CFLAGS+= -DUSE_TCMALLOC -I../deps/gperftools/src
	# FINAL_CFLAGS+= -DUSE_TCMALLOC
	# FINAL_LIBS+= -ltcmalloc_minimal
	FINAL_LIBS += ../deps/gperftools/build/libs/libtcmalloc_minimal.a $(FINAL_LIBS)
endif

ifeq ($(MALLOC),jemalloc)
	# DEPENDENCY_TARGETS+= jemalloc
	# FINAL_CFLAGS+= -DUSE_JEMALLOC -I../deps/jemalloc/include
	# FINAL_LIBS := ../deps/jemalloc/lib/libjemalloc.a $(FINAL_LIBS)
	DEPENDENCY_TARGETS+= jemalloc
	FINAL_CFLAGS+= -DUSE_JEMALLOC -I../deps/jemalloc/include
	FINAL_LIBS := ../deps/jemalloc/lib/libjemalloc.a $(FINAL_LIBS)
endif

LATTE_CC=$(QUIET_CC)$(CC) $(FINAL_CFLAGS)
LATTE_LD=$(QUIET_LINK)$(CC) $(FINAL_LDFLAGS)
LATTE_INSTALL=$(QUIET_INSTALL)$(INSTALL)

CCCOLOR="\033[34m"
LINKCOLOR="\033[34;1m"
SRCCOLOR="\033[33m"
BINCOLOR="\033[37;1m"
MAKECOLOR="\033[32;1m"
ENDCOLOR="\033[0m"

ifndef V
QUIET_CC = @printf '    %b %b\n' $(CCCOLOR)CC$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_LINK = @printf '    %b %b\n' $(LINKCOLOR)LINK$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_INSTALL = @printf '    %b %b\n' $(LINKCOLOR)INSTALL$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR) 1>&2;
endif

LATTE_SERVER_NAME=latte-redis-server
LATTE_SERVER_OBJ=latte_redis_server.o
LATTE_LIBS=libs/sds.o libs/zmalloc.o libs/sha256.o libs/util.o libs/list.o libs/assert.o  libs/rax.o libs/log.o libs/siphash.o libs/mt19937-64.o libs/dict.o libs/stream.o libs/lzf_c.o libs/lzf_d.o libs/quicklist.o libs/listpack.o libs/ziplist.o libs/crcspeed.o libs/crc64.o
ROBJ_LIBS=robj/robj.o robj/module.o robj/zset.o robj/stream.o robj/list.o robj/set.o robj/string.o robj/hash.o
LATTE_SERVER_LIBS_OBJ=$(ROBJ_LIBS) $(LATTE_LIBS) replication.o
#LATTE_SERVER_LIBS_OBJ=libs/log.o libs/zmalloc.o libs/sds.o libs/utils.o libs/dict.o libs/latteassert.o libs/siphash.o libs/connection.o libs/ae.o libs/anet.o libs/syncio.o libs/latteServer.o libs/rax.o libs/crcspeed.o libs/crc64.o  ./config.o ./setproctitle.o ./libs/sha256.o ./module.o ./libs/dict_plus.o ./libs/adlist.o
# LATTE_SENTINEL_NAME=LATTE-sentinel
# LATTE_SERVER_OBJ=adlist.o quicklist.o ae.o anet.o dict.o server.o sds.o zmalloc.o lzf_c.o lzf_d.o pqsort.o zipmap.o sha1.o ziplist.o release.o networking.o util.o object.o db.o replication.o rdb.o t_string.o t_list.o t_set.o t_zset.o t_hash.o config.o aof.o pubsub.o multi.o debug.o sort.o intset.o syncio.o cluster.o crc16.o endianconv.o slowlog.o scripting.o bio.o rio.o rand.o memtest.o crc64.o bitops.o sentinel.o notify.o setproctitle.o blocked.o hyperloglog.o latency.o sparkline.o LATTE-check-rdb.o LATTE-check-aof.o geo.o lazyfree.o module.o evict.o expire.o geohash.o geohash_helper.o childinfo.o defrag.o siphash.o rax.o t_stream.o listpack.o localtime.o lolwut.o lolwut5.o
# LATTE_CLI_NAME=LATTE-cli
# LATTE_CLI_OBJ=anet.o adlist.o dict.o LATTE-cli.o zmalloc.o release.o anet.o ae.o crc64.o siphash.o crc16.o
# LATTE_BENCHMARK_NAME=LATTE-benchmark
# LATTE_BENCHMARK_OBJ=ae.o anet.o LATTE-benchmark.o adlist.o zmalloc.o LATTE-benchmark.o
# LATTE_CHECK_RDB_NAME=LATTE-check-rdb
# LATTE_CHECK_AOF_NAME=LATTE-check-aof

all: $(LATTE_SERVER_NAME)
	@echo ""
	@echo "Hint: It's a good idea to run 'make test' ;)"
	@echo ""

Makefile.dep:
	-$(LATTE_CC) -MM *.c > Makefile.dep 2> /dev/null || true

ifeq (0, $(words $(findstring $(MAKECMDGOALS), $(NODEPS))))
-include Makefile.dep
endif

.PHONY: all

persist-settings: distclean
	echo STD=$(STD) >> .make-settings
	echo WARN=$(WARN) >> .make-settings
	echo OPT=$(OPT) >> .make-settings
	echo MALLOC=$(MALLOC) >> .make-settings
	echo CFLAGS=$(CFLAGS) >> .make-settings
	echo LDFLAGS=$(LDFLAGS) >> .make-settings
	echo LATTE_CFLAGS=$(LATTE_CFLAGS) >> .make-settings
	echo LATTE_LDFLAGS=$(LATTE_LDFLAGS) >> .make-settings
	echo PREV_FINAL_CFLAGS=$(FINAL_CFLAGS) >> .make-settings
	echo PREV_FINAL_LDFLAGS=$(FINAL_LDFLAGS) >> .make-settings
	-(cd ./deps && $(MAKE) $(DEPENDENCY_TARGETS))

.PHONY: persist-settings

# Prerequisites target
.make-prerequisites:
	@touch $@

# Clean everything, persist settings and build dependencies if anything changed
ifneq ($(strip $(PREV_FINAL_CFLAGS)), $(strip $(FINAL_CFLAGS)))
.make-prerequisites: persist-settings
endif

ifneq ($(strip $(PREV_FINAL_LDFLAGS)), $(strip $(FINAL_LDFLAGS)))
.make-prerequisites: persist-settings
endif

$(LATTE_SERVER_NAME): $(LATTE_SERVER_OBJ) $(LATTE_SERVER_LIBS_OBJ)
	# LATTE-server
	cd libs && $(MAKE) all && cd ../
	$(LATTE_LD) -o $@ $^ $(FINAL_LIBS)

test:
	$(MAKE) clean
	$(MAKE) TEST="tcl"

# Because the jemalloc.h header is generated as a part of the jemalloc build,
# building it should complete before building any other object. Instead of
# depending on a single artifact, build all dependencies first.
libs/%.o:
	-(cd ./libs && $(MAKE) )

./%.o: %.c .make-prerequisites
	$(LATTE_CC) -c $<

$(LATTE_SERVER_OBJ): latte_redis_server.c .make-prerequisites
	$(LATTE_CC) -c $<


clean:
	rm -rf $(LATTE_SERVER_NAME) $(LATTE_SENTINEL_NAME) $(LATTE_CLI_NAME) $(LATTE_BENCHMARK_NAME) $(LATTE_CHECK_RDB_NAME) $(LATTE_CHECK_AOF_NAME) *.o *.gcda *.gcno *.gcov LATTE.info lcov-html Makefile.dep dict-benchmark

.PHONY: clean

distclean: clean
	-(cd ./deps && $(MAKE) distclean)
	-(rm -f .make-*)

.PHONY: distclean

# test: $(LATTE_SERVER_NAME) $(LATTE_CHECK_AOF_NAME)
# 	@(cd ..; ./runtest)

check: test

lcov:
	$(MAKE) gcov
	@(set -e; cd ..; ./runtest --clients 1)
	@geninfo -o LATTE.info .
	@genhtml --legend -o lcov-html LATTE.info



.PHONY: lcov

bench: $(LATTE_BENCHMARK_NAME)
	./$(LATTE_BENCHMARK_NAME)

32bit:
	@echo ""
	@echo "WARNING: if it fails under Linux you probably need to install libc6-dev-i386"
	@echo ""
	$(MAKE) CFLAGS="-m32" LDFLAGS="-m32"

gcov:
	$(MAKE) LATTE_CFLAGS="-fprofile-arcs -ftest-coverage -DCOVERAGE_TEST" LATTE_LDFLAGS="-fprofile-arcs -ftest-coverage"

noopt:
	$(MAKE) OPTIMIZATION="-O0"

valgrind:
	$(MAKE) OPTIMIZATION="-O0" MALLOC="libc"

helgrind:
	$(MAKE) OPTIMIZATION="-O0" MALLOC="libc" CFLAGS="-D__ATOMIC_VAR_FORCE_SYNC_MACROS"

src/help.h:
	@../utils/generate-command-help.rb > help.h

install: all
	@mkdir -p $(INSTALL_BIN)
	$(LATTE_INSTALL) $(LATTE_SERVER_NAME) $(INSTALL_BIN)
	$(LATTE_INSTALL) $(LATTE_BENCHMARK_NAME) $(INSTALL_BIN)
	$(LATTE_INSTALL) $(LATTE_CLI_NAME) $(INSTALL_BIN)
	$(LATTE_INSTALL) $(LATTE_CHECK_RDB_NAME) $(INSTALL_BIN)
	$(LATTE_INSTALL) $(LATTE_CHECK_AOF_NAME) $(INSTALL_BIN)
	@ln -sf $(LATTE_SERVER_NAME) $(INSTALL_BIN)/$(LATTE_SENTINEL_NAME)

uninstall:
	rm -f $(INSTALL_BIN)/{$(LATTE_SERVER_NAME),$(LATTE_BENCHMARK_NAME),$(LATTE_CLI_NAME),$(LATTE_CHECK_RDB_NAME),$(LATTE_CHECK_AOF_NAME),$(LATTE_SENTINEL_NAME)}


test-parse: ./tests/latte_parse.c ./tests/latte_parse.h ./libs/zmalloc.o  ./libs/sds.o 
	$(LATTE_CC) ./tests/latte_parse.c ./libs/zmalloc.c ./libs/sds.c ./libs/utils.c -DPARSE_TEST_MAIN $(FINAL_LIBS) -o /tmp/parse_test
	/tmp/parse_test