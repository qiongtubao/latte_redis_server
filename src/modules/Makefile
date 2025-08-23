

# find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

# Compile flags for linux / osx
ifeq ($(uname_S),Linux)
	SHOBJ_CFLAGS ?= -W -Wall -fno-common -g -ggdb -std=c99 -O2
	SHOBJ_LDFLAGS ?= -shared
else
	SHOBJ_CFLAGS ?= -W -Wall -dynamic -fno-common -g -ggdb -std=c99 -O2
	SHOBJ_LDFLAGS ?= -bundle -undefined dynamic_lookup
endif

.SUFFIXES: .c .so .xo .o

all: latte.so 

# .c.xo:
# 	$(CC) -I. $(CFLAGS) $(SHOBJ_CFLAGS) -fPIC -c $< -o $@

.c.xo:
	$(CC) $(CFLAGS) $(SHOBJ_CFLAGS) -fPIC -c $< -o $@

latte.xo: ../redis/redis_module.h 


string.xo: ../redis/redis_module.h 


latte.so: string.xo latte.xo 
	$(CC) -o $@ $^ $(SHOBJ_LDFLAGS) $(LIBS) -lc -lpthread

clean:
	rm -rf *.xo *.so