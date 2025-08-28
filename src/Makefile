LATTE_LIB_WORKSPACE?=$(CURDIR)/../deps/latte_c

# include $(LATTE_LIB_WORKSPACE)/mks/sys.mk
# include $(LATTE_LIB_WORKSPACE)/mks/malloc.mk
REDIS_MALLOC=libc




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


#sanitizer 
ifdef SANITIZER
ifeq ($(SANITIZER),address)
   REDIS_MALLOC=libc
   CFLAGS+=-fsanitize=address -fno-sanitize-recover=all -fno-omit-frame-pointer
   LDFLAGS+=-fsanitize=address
else
ifeq ($(SANITIZER),undefined)
   REDIS_MALLOC=libc
   CFLAGS+=-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
   LDFLAGS+=-fsanitize=undefined
else
ifeq ($(SANITIZER),thread)
   CFLAGS+=-fsanitize=thread -fno-sanitize-recover=all -fno-omit-frame-pointer
   LDFLAGS+=-fsanitize=thread
else
    $(error "unknown sanitizer=${SANITIZER}")
endif
endif
endif
endif

FINAL_CC_CFLAGS=$(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS) $(LATTE_CFLAGS)
FINAL_CXX_CFLAGS=$(CXX_STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS) $(LATTE_CFLAGS)

REDIS_CC=$(QUIET_CC)$(CC) $(FINAL_CC_CFLAGS) $(FINAL_CC_LIBS)
REDIS_GCC=$(QUIET_CC)$(CC) $(FINAL_CC_CFLAGS) $(FINAL_CC_LIBS)
REDIS_LD=$(QUIET_LINK)$(CC) $(FINAL_LDFLAGS) $(FINAL_CC_LIBS)

# 这边编译的时候 需要保存是否使用jemalloc 后续ld的时候使用
$(LATTE_LIB_WORKSPACE)/out/lib/liblatte.a:
	cd ../deps/latte_c && $(MAKE) SANITIZER=$(SANITIZER) build $(LATTE_BUILD) MALLOC=$(REDIS_MALLOC) MODULES=$(LIB_MODUELS) 

%.o: %.c $(LATTE_LIB_WORKSPACE)/out/lib/liblatte.a
	$(REDIS_CC) $(DEBUG) -MMD -o $@ -c $< -I$(LATTE_LIB_WORKSPACE)/out/include 

MAC_M1_CFLAGS=-fprofile-arcs -ftest-coverage
REDIS_OBJS=redis/server.o redis/crons.o redis/client.o redis/client_reply.o redis/module.o redis/db.o redis/slowlog.o
COMMANDS_OBJS=commands/command_manager.o commands/module.o commands/info.o
EXPERIMENT_OBJS=experiment/metric.o

OBJS=version.o main.o help.o  env.o redis_config/config.o $(REDIS_OBJS) $(COMMANDS_OBJS) $(EXPERIMENT_OBJS) shared/shared.o

#OBJS=version.o main.o help.o  env.o redis_server/config.o redis.o redis_server/crons.o redis_server/client.o

# -lstdc++
all: $(LATTE_LIB_WORKSPACE)/out/lib/liblatte.a $(OBJS)
	$(REDIS_CC) $(DEBUG) -o main $(OBJS) -I$(LATTE_LIB_WORKSPACE)/out/include -I$(LATTE_LIB_WORKSPACE)/deps/jemalloc/include -L$(LATTE_LIB_WORKSPACE)/out/lib -lm  -ldl -lz -lstdc++ -lpthread -llatte $(FINAL_SHARD_LIB) $(MAC_M1_CFLAGS)


distclean:
	cd ../deps/latte_c && make clean_all && make distclean

clean:
	rm -rf *.o *.d redis/*.o redis/*.d redis_config/*.o redis_config/*.d commands/*.o commands/*.d
	rm -rf main