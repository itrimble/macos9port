# OS999 storage module. Included by the top-level Makefile; also usable
# alone from the repository root: make -f storage/storage.mk test-storage
STORAGE_DIR ?= storage
STORAGE_OUT ?= build/storage
STORAGE_SRC := $(wildcard $(STORAGE_DIR)/src/*.c)
STORAGE_HDR := $(wildcard $(STORAGE_DIR)/include/*.h $(STORAGE_DIR)/src/*.h)
STORAGE_CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE
STORAGE_INC := -I$(STORAGE_DIR)/include -I$(STORAGE_DIR)/src
STORAGE_LIBS := -lz
# bzip2-compressed UDIF chunks need libbz2; detect it instead of assuming.
STORAGE_HAVE_BZ2 := $(shell printf '\043include <bzlib.h>\nint main(void){return 0;}\n' | $(CC) -x c - -o /dev/null -lbz2 2>/dev/null && echo 1)
ifeq ($(STORAGE_HAVE_BZ2),1)
STORAGE_INC += -DOS999_STORAGE_BZIP2
STORAGE_LIBS += -lbz2
endif
STORAGE_TESTS := codecs archives images volume runtime fuzz real
STORAGE_TEST_BINS := $(STORAGE_TESTS:%=$(STORAGE_OUT)/test_storage_%)
STORAGE_SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1

$(STORAGE_OUT)/libos999storage.a: $(STORAGE_SRC) $(STORAGE_HDR)
	@mkdir -p $(STORAGE_OUT)/obj
	@for f in $(STORAGE_SRC); do $(CC) $(STORAGE_CFLAGS) $(STORAGE_INC) -c $$f -o $(STORAGE_OUT)/obj/$$(basename $$f .c).o || exit 1; done
	ar rcs $@ $(STORAGE_OUT)/obj/*.o

# The runtime check links the desktop's own resource-fork reader.
$(STORAGE_OUT)/test_storage_runtime: STORAGE_EXTRA := src/mac_resource.c -Iinclude
$(STORAGE_OUT)/test_storage_%: $(STORAGE_DIR)/tests/test_storage_%.c $(STORAGE_DIR)/tests/st_test.h $(STORAGE_OUT)/libos999storage.a
	$(CC) $(STORAGE_CFLAGS) $(STORAGE_INC) -o $@ $< $(STORAGE_EXTRA) $(STORAGE_OUT)/libos999storage.a $(STORAGE_LIBS)

build/os999-unpack: $(STORAGE_DIR)/tools/os999_unpack.c $(STORAGE_OUT)/libos999storage.a
	$(CC) $(STORAGE_CFLAGS) $(STORAGE_INC) -o $@ $< $(STORAGE_OUT)/libos999storage.a $(STORAGE_LIBS)

.PHONY: test-storage storage-asan storage-tools
storage-tools: build/os999-unpack

test-storage: $(STORAGE_TEST_BINS) build/os999-unpack
	@for t in $(STORAGE_TEST_BINS); do $$t || exit 1; done

# Sanitizer build of the library, tests and fuzzer (separate output dir).
storage-asan:
	$(MAKE) -f $(STORAGE_DIR)/storage.mk STORAGE_DIR=$(STORAGE_DIR) STORAGE_OUT=build/storage-asan \
	    STORAGE_CFLAGS="-std=c11 -Wall -Wextra -D_DEFAULT_SOURCE $(STORAGE_SAN)" \
	    build/storage-asan/libos999storage.a $(STORAGE_TESTS:%=build/storage-asan/test_storage_%)
	@for t in $(STORAGE_TESTS:%=build/storage-asan/test_storage_%); do $$t || exit 1; done
