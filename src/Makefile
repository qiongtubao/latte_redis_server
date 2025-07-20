LATTE_LIB_WORKSPACE?=$(CURDIR)/../deps/latte_c

REDIS_CC=$(QUIET_CC)$(CC) $(FINAL_CFLAGS)
REDIS_GCC=$(QUIET_CC)$(CC) $(FINAL_CFLAGS)
REDIS_LD=$(QUIET_LINK)$(CC) $(FINAL_LDFLAGS)

LIB_MODUELS="zset stream sds dict ae config list server io object lzf rax debug"


#LIB_MODUELS="error sds dict ae config list server zset stream  lzf object debug"


uname_S:= $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_M:= $(shell sh -c 'uname -s 2>/dev/null || echo not')

ifeq ($(uname_S),Linux)
	FINAL_SHARD_LIB=-lrt 
endif

$(LATTE_LIB_WORKSPACE)/out/lib/liblatte.a:
	cd ../deps/latte_c && make build  MODULES=$(LIB_MODUELS) 

%.o: %.c $(LATTE_LIB_WORKSPACE)/out/lib/liblatte.a
	$(REDIS_CC) $(DEBUG) -MMD -o $@ -c $< -I$(LATTE_LIB_WORKSPACE)/out/include 

MAC_M1_CFLAGS=-fprofile-arcs -ftest-coverage

OBJS=version.o main.o help.o  env.o redis/config.o redis/server.o redis/crons.o redis/client.o redis/commands.o redis/client_reply.o redis/module.o redis/db.o 

#OBJS=version.o main.o help.o  env.o redis_server/config.o redis.o redis_server/crons.o redis_server/client.o

# -lstdc++
all: $(LATTE_LIB_WORKSPACE)/out/lib/liblatte.a $(OBJS)
	$(REDIS_CC) $(DEBUG) -o main $(OBJS) -I$(LATTE_LIB_WORKSPACE)/out/include -L$(LATTE_LIB_WORKSPACE)/out/lib $(FINAL_SHARD_LIB) -lm  -ldl -lz -lstdc++ -lpthread -llatte $(MAC_M1_CFLAGS)

clean:
	cd ../deps/latte_c && make clean_all 
	rm -rf *.o *.d redis/*.o redis/*.d
	rm -rf main