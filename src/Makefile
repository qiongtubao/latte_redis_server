
LIB_MODUELS="sds dict ae"

latte_lib:
	cd ../deps/latte_c && make build MODULES=$(LIB_MODUELS) 

%.o: %.c 
	gcc -o $@ -c $< -I../deps/latte_c/out/include 
# -lstdc++
all: latte_lib version.o main.o help.o server.o
	gcc -o main version.o main.o help.o  -I../deps/latte_c/out/include ../deps/latte_c/out/lib/liblatte.a  ../deps/latte_c/deps/jemalloc/lib/libjemalloc.a  -lm -lrt   -ldl -lz -lstdc++ -lpthread

