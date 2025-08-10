LATTE_LIB_WORKSPACE?=$(CURDIR)/../deps/latte_c

# include $(LATTE_LIB_WORKSPACE)/mks/sys.mk
# include $(LATTE_LIB_WORKSPACE)/mks/malloc.mk
REDIS_MALLOC=libc



REDIS_CC=$(QUIET_CC)$(CC) $(FINAL_CC_CFLAGS) $(FINAL_CC_LIBS)
REDIS_GCC=$(QUIET_CC)$(CC) $(FINAL_CC_CFLAGS) $(FINAL_CC_LIBS)
REDIS_LD=$(QUIET_LINK)$(CC) $(FINAL_LDFLAGS) $(FINAL_CC_LIBS)
DEBUG=-g
LIB_MODUELS="zset stream sds dict config list server io object lzf rax debug thread_single_object async_io ae utils"


#LIB_MODUELS="error sds dict ae config list server zset stream  lzf object debug"


uname_S:= $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_M:= $(shell sh -c 'uname -s 2>/dev/null || echo not')

ifeq ($(uname_S),Linux)
	REDIS_MALLOC=jemalloc
	FINAL_SHARD_LIB=-lrt -ljemalloc -luring
endif

ifdef IOURING
	LATTE_BUILD+= IOURING=1
endif

# 这边编译的时候 需要保存是否使用jemalloc 后续ld的时候使用
$(LATTE_LIB_WORKSPACE)/out/lib/liblatte.a:
	cd ../deps/latte_c && $(MAKE) build $(LATTE_BUILD) MALLOC=$(REDIS_MALLOC) MODULES=$(LIB_MODUELS) 

%.o: %.c $(LATTE_LIB_WORKSPACE)/out/lib/liblatte.a
	$(REDIS_CC) $(DEBUG) -MMD -o $@ -c $< -I$(LATTE_LIB_WORKSPACE)/out/include 

MAC_M1_CFLAGS=-fprofile-arcs -ftest-coverage

OBJS=version.o main.o help.o  env.o redis_config/config.o redis/server.o redis/crons.o redis/client.o redis/commands.o redis/client_reply.o redis/module.o redis/db.o 

#OBJS=version.o main.o help.o  env.o redis_server/config.o redis.o redis_server/crons.o redis_server/client.o

# -lstdc++
all: $(LATTE_LIB_WORKSPACE)/out/lib/liblatte.a $(OBJS)
	$(REDIS_CC) $(DEBUG) -o main $(OBJS) -I$(LATTE_LIB_WORKSPACE)/out/include -I$(LATTE_LIB_WORKSPACE)/deps/jemalloc/include -L$(LATTE_LIB_WORKSPACE)/out/lib -lm  -ldl -lz -lstdc++ -lpthread -llatte $(FINAL_SHARD_LIB) $(MAC_M1_CFLAGS)

clean:
	cd ../deps/latte_c && make clean_all 
	rm -rf *.o *.d redis/*.o redis/*.d redis_config/*.o redis_config/*.d
	rm -rf main