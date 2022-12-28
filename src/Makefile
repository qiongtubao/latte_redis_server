
LIB_MODUELS="sds dict ae config"

uname_S:= $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_M:= $(shell sh -c 'uname -s 2>/dev/null || echo not')

ifeq ($(uname_S),Linux)
	FINAL_SHARD_LIB=-lrt 
endif

latte_lib:
	cd ../deps/latte_c && make build MODULES=$(LIB_MODUELS) 

%.o: %.c 
	gcc -o $@ -c $< -I../deps/latte_c/out/include 

MAC_M1_CFLAGS=-fprofile-arcs -ftest-coverage
OBJS=version.o main.o help.o server.o env.o server_config.o
# -lstdc++
all: latte_lib $(OBJS)
	gcc -o main $(OBJS) -I../deps/latte_c/out/include ../deps/latte_c/out/lib/liblatte.a  ../deps/latte_c/deps/jemalloc/lib/libjemalloc.a $(FINAL_SHARD_LIB) -lm  -ldl -lz -lstdc++ -lpthread $(MAC_M1_CFLAGS)

clean:
	cd ../deps/latte_c && make clean_all 
	rm -rf *.o 
	rm -rf main