# OS999 guest networking module (checklist N01-N08). Included by the
# top-level Makefile; also usable alone: make -f net/net.mk test-net
NET_DIR ?= net
NET_OUT ?= build/net
NET_SRC := $(wildcard $(NET_DIR)/src/*.c)
NET_HDR := $(wildcard $(NET_DIR)/include/*.h $(NET_DIR)/src/*.h)
NET_CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE
NET_INC := -I$(NET_DIR)/include -I$(NET_DIR)/src -I$(NET_DIR)/tests
GUEST_NET_LIBS := -lpthread
NET_TESTS := policy broker mactcp pan
NET_TEST_BINS := $(NET_TESTS:%=$(NET_OUT)/test_gn_%)
NET_SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1
NET_FUZZ_ITERS ?= 20000

$(NET_OUT)/libos999net.a: $(NET_SRC) $(NET_HDR)
	@mkdir -p $(NET_OUT)/obj
	@for f in $(NET_SRC); do $(CC) $(NET_CFLAGS) $(NET_INC) -c $$f -o $(NET_OUT)/obj/$$(basename $$f .c).o || exit 1; done
	ar rcs $@ $(NET_OUT)/obj/*.o

$(NET_OUT)/test_gn_%: $(NET_DIR)/tests/test_gn_%.c $(NET_DIR)/tests/gn_test.h $(NET_OUT)/libos999net.a
	$(CC) $(NET_CFLAGS) $(NET_INC) -o $@ $< $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS)

$(NET_OUT)/fuzz_gn: $(NET_DIR)/tests/fuzz_gn.c $(NET_DIR)/tests/gn_test.h $(NET_OUT)/libos999net.a
	$(CC) $(NET_CFLAGS) $(NET_INC) -o $@ $< $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS)

.PHONY: test-net net-asan
test-net: $(NET_TEST_BINS)
	@for t in $(NET_TEST_BINS); do $$t || exit 1; done

# Sanitizer build of the library, tests and fuzzer (separate output dir).
net-asan:
	$(MAKE) -f $(NET_DIR)/net.mk NET_DIR=$(NET_DIR) NET_OUT=build/net-asan \
	    NET_CFLAGS="-std=c11 -Wall -Wextra -D_DEFAULT_SOURCE $(NET_SAN)" \
	    $(NET_TESTS:%=build/net-asan/test_gn_%) build/net-asan/fuzz_gn
	@for t in $(NET_TESTS:%=build/net-asan/test_gn_%); do $$t || exit 1; done
	build/net-asan/fuzz_gn $(NET_FUZZ_ITERS)
