LATTE_LIB_WORKSPACE?=$(CURDIR)/../../deps/latte_c

# $(CC)=gcc
# $(CXX)=g++
LATTE_CC=$(QUIET_CC)$(CC) $(FINAL_CC_CFLAGS)
LATTE_CXX=$(CXX) $(FINAL_CXX_CFLAGS)
LATTE_LD=$(QUIET_LINK)$(CC) $(FINAL_CC_LDFLAGS)
LATTE_CXX_LD=$(CXX) $(FINAL_CXX_CFLAGS)

METRIC_TEST_MAIN=metric_test
METRIC_BUILD_OBJ=metric.o
FINAL_CC_LIBS=-lm  -ldl -lz -lstdc++ -lpthread -llatte -ljemalloc

DEBUG=-g 
ifneq ($(uname_S), SunOS) 
	DEBUG+= -ggdb
endif

latte_lcov:
	lcov --capture --directory . \
		--output-file lcov.info \
		--test-name latte_lcov \
		--no-external 
latte_genhtml:
	genhtml lcov.info \
		--output-directory lcov_output \
		--title "latte LCOV" \
		--show-details \
		--legend	

%.o: %.c 
	$(LATTE_CC) $(DEBUG) -MMD -o $@ -c $< $(FINAL_CC_LIBS) -I$(LATTE_LIB_WORKSPACE)/out/include -I$(LATTE_LIB_WORKSPACE)/deps/jemalloc/include

metric_test: metric.o metric_test.o
	rm -rf lcov.info lcov_output 
	$(LATTE_CC)  -fprofile-arcs -ftest-coverage $(DEBUG) -o $(METRIC_TEST_MAIN) $(METRIC_TEST_MAIN).o $(METRIC_BUILD_OBJ) $(FINAL_CC_LIBS)  -I$(LATTE_LIB_WORKSPACE)/out/include -I$(LATTE_LIB_WORKSPACE)/deps/jemalloc/include -L$(LATTE_LIB_WORKSPACE)/out/lib 
	./$(METRIC_TEST_MAIN)
	$(MAKE) latte_lcov
	$(MAKE) latte_genhtml
