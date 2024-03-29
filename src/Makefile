# Redis Makefile
# Copyright (C) 2009 Salvatore Sanfilippo <antirez at gmail dot com>
# This file is released under the BSD license, see the COPYING file
#
# The Makefile composes the final FINAL_CFLAGS and FINAL_LDFLAGS using
# what is needed for Redis plus the standard CFLAGS and LDFLAGS passed.
# However when building the dependencies (Jemalloc, Lua, Hiredis, ...)
# CFLAGS and LDFLAGS are propagated to the dependencies, so to pass
# flags only to be used when compiling / linking Redis itself LATTE_CFLAGS
# and LATTE_LDFLAGS are used instead (this is the case of 'make gcov').
#
# Dependencies are stored in the Makefile.dep file. To rebuild this file
# Just use 'make dep', but this is only needed by developers.

release_hdr := $(shell sh -c './mkreleasehdr.sh')
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo not')
OPTIMIZATION?=-O2
DEPENDENCY_TARGETS=
#hiredis linenoise lua hdr_histogram
NODEPS:=clean distclean

# Default settings
STD=-pedantic -DLATTE_STATIC=''

# Use -Wno-c11-extensions on clang, either where explicitly used or on
# platforms we can assume it's being used.
ifneq (,$(findstring clang,$(CC)))
  STD+=-Wno-c11-extensions
else
ifneq (,$(findstring FreeBSD,$(uname_S)))
  STD+=-Wno-c11-extensions
endif
endif
WARN=-Wall -W -Wno-missing-field-initializers
OPT=$(OPTIMIZATION)

# Detect if the compiler supports C11 _Atomic
C11_ATOMIC := $(shell sh -c 'echo "\#include <stdatomic.h>" > foo.c; \
	$(CC) -std=c11 -c foo.c -o foo.o > /dev/null 2>&1; \
	if [ -f foo.o ]; then echo "yes"; rm foo.o; fi; rm foo.c')

# ifeq ($(C11_ATOMIC),yes)
# 	STD+=-std=c11
# else
# 	STD+=-std=c99
# endif

PREFIX?=/usr/local
INSTALL_BIN=$(PREFIX)/bin
INSTALL=install
PKG_CONFIG?=pkg-config

# Default allocator defaults to Jemalloc if it's not an ARM
MALLOC=libc
ifneq ($(uname_M),armv6l)
ifneq ($(uname_M),armv7l)
ifeq ($(uname_S),Linux)
	MALLOC=jemalloc
endif
endif
endif

# To get ARM stack traces if Redis crashes we need a special C flag.
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
DEBUG=-g -ggdb

# Linux ARM32 needs -latomic at linking time
ifneq (,$(findstring armv,$(uname_M)))
        FINAL_LIBS+=-latomic
endif

ifeq ($(uname_S),SunOS)
	# SunOS
	ifeq ($(findstring -m32,$(FINAL_CFLAGS)),)
		CFLAGS+=-m64
	endif
	ifeq ($(findstring -m32,$(FINAL_LDFLAGS)),)
		LDFLAGS+=-m64
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
	# Homebrew's OpenSSL is not linked to /usr/local to avoid
	# conflicts with the system's LibreSSL installation so it
	# must be referenced explicitly during build.
ifeq ($(uname_M),arm64)
	# Homebrew arm64 uses /opt/homebrew as HOMEBREW_PREFIX
	OPENSSL_PREFIX?=/opt/homebrew/opt/openssl
else
	# Homebrew x86/ppc uses /usr/local as HOMEBREW_PREFIX
	OPENSSL_PREFIX?=/usr/local/opt/openssl
endif
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
ifeq ($(uname_S),NetBSD)
	# NetBSD
	FINAL_LIBS+= -lpthread
	ifeq ($(USE_BACKTRACE),yes)
	    FINAL_CFLAGS+= -DUSE_BACKTRACE -I/usr/pkg/include
	    FINAL_LDFLAGS+= -L/usr/pkg/lib
	    FINAL_LIBS+= -lexecinfo
    	endif
else
ifeq ($(uname_S),FreeBSD)
	# FreeBSD
	FINAL_LIBS+= -lpthread -lexecinfo
else
ifeq ($(uname_S),DragonFly)
	# DragonFly
	FINAL_LIBS+= -lpthread -lexecinfo
else
ifeq ($(uname_S),OpenBSD)
	# OpenBSD
	FINAL_LIBS+= -lpthread -lexecinfo
else
ifeq ($(uname_S),NetBSD)
	# NetBSD
	FINAL_LIBS+= -lpthread -lexecinfo
else
ifeq ($(uname_S),Haiku)
	# Haiku
	FINAL_CFLAGS+= -DBSD_SOURCE
	FINAL_LDFLAGS+= -lbsd -lnetwork
	FINAL_LIBS+= -lpthread
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
endif
endif
endif
endif

ifdef OPENSSL_PREFIX
	OPENSSL_CFLAGS=-I$(OPENSSL_PREFIX)/include
	OPENSSL_LDFLAGS=-L$(OPENSSL_PREFIX)/lib
	# Also export OPENSSL_PREFIX so it ends up in deps sub-Makefiles
	export OPENSSL_PREFIX
endif

# Include paths to dependencies
# FINAL_CFLAGS+= -I../deps/hiredis -I../deps/linenoise -I../deps/lua/src -I../deps/hdr_histogram
FINAL_CFLAGS+= -I./libs
# Determine systemd support and/or build preference (defaulting to auto-detection)
BUILD_WITH_SYSTEMD=no
LIBSYSTEMD_LIBS=-lsystemd

# If 'USE_SYSTEMD' in the environment is neither "no" nor "yes", try to
# auto-detect libsystemd's presence and link accordingly.
ifneq ($(USE_SYSTEMD),no)
	LIBSYSTEMD_PKGCONFIG := $(shell $(PKG_CONFIG) --exists libsystemd && echo $$?)
# If libsystemd cannot be detected, continue building without support for it
# (unless a later check tells us otherwise)
ifeq ($(LIBSYSTEMD_PKGCONFIG),0)
	BUILD_WITH_SYSTEMD=yes
	LIBSYSTEMD_LIBS=$(shell $(PKG_CONFIG) --libs libsystemd)
endif
endif

# If 'USE_SYSTEMD' is set to "yes" use pkg-config if available or fall back to
# default -lsystemd.
ifeq ($(USE_SYSTEMD),yes)
	BUILD_WITH_SYSTEMD=yes
endif

ifeq ($(BUILD_WITH_SYSTEMD),yes)
	FINAL_LIBS+=$(LIBSYSTEMD_LIBS)
	FINAL_CFLAGS+= -DHAVE_LIBSYSTEMD
endif

ifeq ($(MALLOC),tcmalloc)
	FINAL_CFLAGS+= -DUSE_TCMALLOC
	FINAL_LIBS+= -ltcmalloc
endif

ifeq ($(MALLOC),tcmalloc_minimal)
	FINAL_CFLAGS+= -DUSE_TCMALLOC
	FINAL_LIBS+= -ltcmalloc_minimal
endif

ifeq ($(MALLOC),jemalloc)
	DEPENDENCY_TARGETS+= jemalloc
	FINAL_CFLAGS+= -DUSE_JEMALLOC -I../deps/jemalloc/include
	FINAL_LIBS := ../deps/jemalloc/lib/libjemalloc.a $(FINAL_LIBS)
endif

ifeq ($(BUILD_TLS),yes)
	FINAL_CFLAGS+=-DUSE_OPENSSL $(OPENSSL_CFLAGS)
	FINAL_LDFLAGS+=$(OPENSSL_LDFLAGS)
	LIBSSL_PKGCONFIG := $(shell $(PKG_CONFIG) --exists libssl && echo $$?)
ifeq ($(LIBSSL_PKGCONFIG),0)
	LIBSSL_LIBS=$(shell $(PKG_CONFIG) --libs libssl)
else
	LIBSSL_LIBS=-lssl
endif
	LIBCRYPTO_PKGCONFIG := $(shell $(PKG_CONFIG) --exists libcrypto && echo $$?)
ifeq ($(LIBCRYPTO_PKGCONFIG),0)
	LIBCRYPTO_LIBS=$(shell $(PKG_CONFIG) --libs libcrypto)
else
	LIBCRYPTO_LIBS=-lcrypto
endif
	FINAL_LIBS += ../deps/hiredis/libhiLATTE_ssl.a $(LIBSSL_LIBS) $(LIBCRYPTO_LIBS)
endif

ifndef V
    define MAKE_INSTALL
        @printf '    %b %b\n' $(LINKCOLOR)INSTALL$(ENDCOLOR) $(BINCOLOR)$(1)$(ENDCOLOR) 1>&2
        @$(INSTALL) $(1) $(2)
    endef
else
    define MAKE_INSTALL
        $(INSTALL) $(1) $(2)
    endef
endif


LATTE_CC=$(QUIET_CC)$(CC) $(FINAL_CFLAGS)
LATTE_GCC=$(QUIET_CC)$(CC) $(FINAL_CFLAGS)
LATTE_LD=$(QUIET_LINK)$(CC) $(FINAL_LDFLAGS)
LATTE_INSTALL=$(QUIET_INSTALL)$(INSTALL)

CCCOLOR="\033[34m"
LINKCOLOR="\033[34;1m"
SRCCOLOR="\033[33m"
BINCOLOR="\033[37;1m"
MAKECOLOR="\033[32;1m"
ENDCOLOR="\033[0m"

# ifndef V
# QUIET_CC = @printf '    %b %b\n' $(CCCOLOR)CC$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;
# QUIET_LINK = @printf '    %b %b\n' $(LINKCOLOR)LINK$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR) 1>&2;
# QUIET_INSTALL = @printf '    %b %b\n' $(LINKCOLOR)INSTALL$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR) 1>&2;
# endif

# LATTE_SERVER_NAME=latte-redis-server$(PROG_SUFFIX)
# LATTE_SENTINEL_NAME=latte-redis-sentinel$(PROG_SUFFIX)
# LATTE_SERVER_OBJ=adlist.o quicklist.o ae.o anet.o dict.o server.o sds.o zmalloc.o lzf_c.o lzf_d.o pqsort.o zipmap.o sha1.o ziplist.o release.o networking.o util.o object.o db.o replication.o rdb.o t_string.o t_list.o t_set.o t_zset.o t_hash.o config.o aof.o pubsub.o multi.o debug.o sort.o intset.o syncio.o cluster.o crc16.o endianconv.o slowlog.o scripting.o bio.o rio.o rand.o memtest.o crcspeed.o crc64.o bitops.o sentinel.o notify.o setproctitle.o blocked.o hyperloglog.o latency.o sparkline.o redis-check-rdb.o redis-check-aof.o geo.o lazyfree.o module.o evict.o expire.o geohash.o geohash_helper.o childinfo.o defrag.o siphash.o rax.o t_stream.o listpack.o localtime.o lolwut.o lolwut5.o lolwut6.o acl.o tracking.o connection.o tls.o sha256.o timeout.o setcpuaffinity.o monotonic.o mt19937-64.o resp_parser.o call_reply.o
# LATTE_CLI_NAME=latte-redis-cli$(PROG_SUFFIX)
# LATTE_CLI_OBJ=anet.o adlist.o dict.o redis-cli.o zmalloc.o release.o ae.o redisassert.o crcspeed.o crc64.o siphash.o crc16.o monotonic.o cli_common.o mt19937-64.o
# LATTE_BENCHMARK_NAME=redis-benchmark$(PROG_SUFFIX)
# LATTE_BENCHMARK_OBJ=ae.o anet.o redis-benchmark.o adlist.o dict.o zmalloc.o redisassert.o release.o crcspeed.o crc64.o siphash.o crc16.o monotonic.o cli_common.o mt19937-64.o
# LATTE_CHECK_RDB_NAME=redis-check-rdb$(PROG_SUFFIX)
# LATTE_CHECK_AOF_NAME=redis-check-aof$(PROG_SUFFIX)

LATTE_SERVER_NAME=latte-redis-server
LATTE_SERVER_OBJ=server.o
LATTE_LIBS=libs/sds.o libs/zmalloc.o libs/sha256.o libs/util.o libs/list.o libs/assert.o  libs/rax.o libs/log.o libs/siphash.o libs/mt19937-64.o libs/dict.o libs/stream.o libs/lzf_c.o libs/lzf_d.o libs/quicklist.o libs/listpack.o libs/ziplist.o libs/crcspeed.o libs/crc64.o
ROBJ_LIBS=robj/robj.o robj/module.o robj/zset.o robj/stream.o robj/list.o robj/set.o robj/string.o robj/hash.o
LATTE_SERVER_LIBS_OBJ=$(ROBJ_LIBS) $(LATTE_LIBS) 

all: $(LATTE_SERVER_NAME) $(LATTE_SENTINEL_NAME) $(LATTE_CLI_NAME) $(LATTE_BENCHMARK_NAME) $(LATTE_CHECK_RDB_NAME) $(LATTE_CHECK_AOF_NAME)
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
	echo BUILD_TLS=$(BUILD_TLS) >> .make-settings
	echo USE_SYSTEMD=$(USE_SYSTEMD) >> .make-settings
	echo CFLAGS=$(CFLAGS) >> .make-settings
	echo LDFLAGS=$(LDFLAGS) >> .make-settings
	echo LATTE_CFLAGS=$(LATTE_CFLAGS) >> .make-settings
	echo LATTE_LDFLAGS=$(LATTE_LDFLAGS) >> .make-settings
	echo PREV_FINAL_CFLAGS=$(FINAL_CFLAGS) >> .make-settings
	echo PREV_FINAL_LDFLAGS=$(FINAL_LDFLAGS) >> .make-settings
	-(cd ../deps && $(MAKE) $(DEPENDENCY_TARGETS))

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



# latte-redis-server
# $(LATTE_SERVER_NAME):  $(LATTE_SERVER_LIBS_OBJ)
# 	$(LATTE_LD) -o $@ $^ $(FINAL_LIBS)  -I../libs -c latte_redis_server.c

$(LATTE_SERVER_NAME): $(LATTE_SERVER_OBJ) $(LATTE_SERVER_LIBS_OBJ)
	$(LATTE_CC) -o $@ $^ $(FINAL_LIBS)

# redis-sentinel
# $(LATTE_SENTINEL_NAME): $(LATTE_SERVER_NAME)
# 	$(LATTE_INSTALL) $(LATTE_SERVER_NAME) $(LATTE_SENTINEL_NAME)

# redis-check-rdb
# $(LATTE_CHECK_RDB_NAME): $(LATTE_SERVER_NAME)
# 	$(LATTE_INSTALL) $(LATTE_SERVER_NAME) $(LATTE_CHECK_RDB_NAME)

# redis-check-aof
# $(LATTE_CHECK_AOF_NAME): $(LATTE_SERVER_NAME)
# 	$(LATTE_INSTALL) $(LATTE_SERVER_NAME) $(LATTE_CHECK_AOF_NAME)

# redis-cli
# $(LATTE_CLI_NAME): $(LATTE_CLI_OBJ)
# 	$(LATTE_LD) -o $@ $^ ../deps/hiredis/libhiredis.a ../deps/linenoise/linenoise.o $(FINAL_LIBS)

# redis-benchmark
# $(LATTE_BENCHMARK_NAME): $(LATTE_BENCHMARK_OBJ)
# 	$(LATTE_LD) -o $@ $^ ../deps/hiredis/libhiredis.a ../deps/hdr_histogram/hdr_histogram.o $(FINAL_LIBS)

# DEP = $(LATTE_SERVER_OBJ:%.o=%.d) $(LATTE_CLI_OBJ:%.o=%.d) $(LATTE_BENCHMARK_OBJ:%.o=%.d)
# -include $(DEP)

DEP = $(LATTE_SERVER_OBJ:%.o=%.d)
-include $(DEP)



libs/%.o: 
	-(cd ./libs && $(MAKE) all && ../)

robj/%.o: 
	-(cd ./robj && $(MAKE) all && ../)

# Because the jemalloc.h header is generated as a part of the jemalloc build,
# building it should complete before building any other object. Instead of
# depending on a single artifact, build all dependencies first.
%.o: %.c .make-prerequisites
	$(LATTE_CC) -MMD -o $@ -c $< $(FINAL_LIBS)

clean:
	rm -rf $(LATTE_SERVER_NAME) $(LATTE_SENTINEL_NAME) $(LATTE_CLI_NAME) $(LATTE_BENCHMARK_NAME) $(LATTE_CHECK_RDB_NAME) $(LATTE_CHECK_AOF_NAME) *.o *.gcda *.gcno *.gcov redis.info lcov-html Makefile.dep
	rm -f $(DEP)

.PHONY: clean

distclean: clean
	-(cd ../deps && $(MAKE) distclean)
#	-(cd modules && $(MAKE) clean)
#   -(cd ../tests/modules && $(MAKE) clean)
	-(rm -f .make-*)

.PHONY: distclean

test1: test.c 
	@echo "==============================================="
	@echo $(C11_ATOMIC)
	@echo $(STD)
	@echo "==============================================="
	$(LATTE_LD) -o $@ $^ $(FINAL_LIBS)

test: $(LATTE_SERVER_NAME) $(LATTE_CHECK_AOF_NAME) $(LATTE_CLI_NAME) $(LATTE_BENCHMARK_NAME)
	@(cd ..; ./runtest)

test-modules: $(LATTE_SERVER_NAME)
	@(cd ..; ./runtest-moduleapi)

test-sentinel: $(LATTE_SENTINEL_NAME) $(LATTE_CLI_NAME)
	@(cd ..; ./runtest-sentinel)

test-cluster: $(LATTE_SERVER_NAME) $(LATTE_CLI_NAME)
	@(cd ..; ./runtest-cluster)

check: test

lcov:
	$(MAKE) gcov
	@(set -e; cd ..; ./runtest --clients 1)
	@geninfo -o redis.info .
	@genhtml --legend -o lcov-html redis.info

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
	$(MAKE) OPTIMIZATION="-O0" MALLOC="libc" CFLAGS="-D__ATOMIC_VAR_FORCE_SYNC_MACROS" LATTE_CFLAGS="-I/usr/local/include" LATTE_LDFLAGS="-L/usr/local/lib"

src/help.h:
	@../utils/generate-command-help.rb > help.h

install: all
	@mkdir -p $(INSTALL_BIN)
	$(call MAKE_INSTALL,$(LATTE_SERVER_NAME),$(INSTALL_BIN))
	$(call MAKE_INSTALL,$(LATTE_BENCHMARK_NAME),$(INSTALL_BIN))
	$(call MAKE_INSTALL,$(LATTE_CLI_NAME),$(INSTALL_BIN))
	@ln -sf $(LATTE_SERVER_NAME) $(INSTALL_BIN)/$(LATTE_CHECK_RDB_NAME)
	@ln -sf $(LATTE_SERVER_NAME) $(INSTALL_BIN)/$(LATTE_CHECK_AOF_NAME)
	@ln -sf $(LATTE_SERVER_NAME) $(INSTALL_BIN)/$(LATTE_SENTINEL_NAME)

uninstall:
	rm -f $(INSTALL_BIN)/{$(LATTE_SERVER_NAME),$(LATTE_BENCHMARK_NAME),$(LATTE_CLI_NAME),$(LATTE_CHECK_RDB_NAME),$(LATTE_CHECK_AOF_NAME),$(LATTE_SENTINEL_NAME)}
