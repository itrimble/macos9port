# OS999 Bluetooth stack. Included by the top-level Makefile; also usable
# alone: make -f bluetooth/bluetooth.mk test-bluetooth
#
# The stack is plain C11 with no SDL and no windowing, so it builds as a
# small static library and each test is its own binary.
BT_DIR ?= bluetooth
BT_OUT ?= build/bluetooth
# The guest network stack owns IP, ARP and TCP. The Bluetooth stack needs
# only its public header to bind a personal area network to it, so the
# dependency runs one way: bluetooth knows net, net does not know bluetooth.
NET_DIR ?= net
NET_OUT ?= build/net
BT_SIM_SRC := $(BT_DIR)/src/bt_hci_sim.c
BT_SIM_HDR := $(BT_DIR)/src/bt_hci_sim.h
BT_SRC := $(filter-out $(BT_SIM_SRC),$(wildcard $(BT_DIR)/src/*.c))
BT_HDR := $(wildcard $(BT_DIR)/include/*.h $(BT_DIR)/src/*.h)
BT_CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE
BT_INC := -I$(BT_DIR)/include -I$(BT_DIR)/src -I$(BT_DIR)/tests -I$(NET_DIR)/include
BT_TESTS := hci device pair l2cap rfcomm bnep
BT_TEST_BINS := $(BT_TESTS:%=$(BT_OUT)/test_bt_%)
BT_SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1

# The guest network library is needed by the end to end test. When the top
# level Makefile already included net.mk we must not include it twice.
ifeq ($(filter $(NET_DIR)/net.mk,$(MAKEFILE_LIST)),)
include $(NET_DIR)/net.mk
endif

$(BT_OUT)/libos999bt.a: $(BT_SRC) $(BT_HDR)
	@mkdir -p $(BT_OUT)/obj
	@for f in $(BT_SRC); do $(CC) $(BT_CFLAGS) $(BT_INC) -c $$f -o $(BT_OUT)/obj/$$(basename $$f .c).o || exit 1; done
	ar rcs $@ $(BT_OUT)/obj/*.o

$(BT_OUT)/test_bt_%: $(BT_DIR)/tests/test_bt_%.c $(BT_DIR)/tests/bt_test.h $(BT_OUT)/libos999bt.a
	$(CC) $(BT_CFLAGS) $(BT_INC) -o $@ $< $(BT_OUT)/libos999bt.a

# The end to end test drives the controller simulator, which is test only.
$(BT_OUT)/test_bt_stack: $(BT_DIR)/tests/test_bt_stack.c $(BT_DIR)/tests/bt_test.h $(BT_SIM_SRC) $(BT_SIM_HDR) $(BT_OUT)/libos999bt.a
	$(CC) $(BT_CFLAGS) $(BT_INC) -o $@ $(BT_DIR)/tests/test_bt_stack.c $(BT_SIM_SRC) $(BT_OUT)/libos999bt.a

# The guest over the personal area network: a MacTCP stream crossing the
# Bluetooth link, so this one needs the guest network library as well.
$(BT_OUT)/test_bt_pan_guest: $(BT_DIR)/tests/test_bt_pan_guest.c $(BT_DIR)/tests/bt_test.h $(BT_SIM_SRC) $(BT_SIM_HDR) $(BT_OUT)/libos999bt.a $(NET_OUT)/libos999net.a
	$(CC) $(BT_CFLAGS) $(BT_INC) -o $@ $(BT_DIR)/tests/test_bt_pan_guest.c $(BT_SIM_SRC) $(BT_OUT)/libos999bt.a $(NET_OUT)/libos999net.a

.PHONY: test-bluetooth bt-asan bt-darwin
test-bluetooth: $(BT_TEST_BINS) $(BT_OUT)/test_bt_stack $(BT_OUT)/test_bt_pan_guest
	@for t in $(BT_TEST_BINS) $(BT_OUT)/test_bt_stack $(BT_OUT)/test_bt_pan_guest; do $$t || exit 1; done

# Sanitizer build of the library and the tests (separate output dir).
bt-asan:
	$(MAKE) -f $(BT_DIR)/bluetooth.mk BT_DIR=$(BT_DIR) BT_OUT=build/bluetooth-asan \
	    NET_DIR=$(NET_DIR) NET_OUT=build/net-asan \
	    NET_CFLAGS="-std=c11 -Wall -Wextra -D_DEFAULT_SOURCE $(NET_SAN)" \
	    BT_CFLAGS="-std=c11 -Wall -Wextra -D_DEFAULT_SOURCE $(BT_SAN)" \
	    $(BT_TESTS:%=build/bluetooth-asan/test_bt_%) build/bluetooth-asan/test_bt_stack build/bluetooth-asan/test_bt_pan_guest
	@for t in $(BT_TESTS:%=build/bluetooth-asan/test_bt_%) build/bluetooth-asan/test_bt_stack build/bluetooth-asan/test_bt_pan_guest; do $$t || exit 1; done

# The macOS accelerator is Objective-C against IOBluetooth. It is built
# on its own so the rest of the stack stays plain C.
bt-darwin:
	$(CC) $(BT_CFLAGS) $(BT_INC) -x objective-c -fobjc-arc -c $(BT_DIR)/src/bt_darwin.m -o $(BT_OUT)/bt_darwin.o
