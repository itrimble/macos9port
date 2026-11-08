# Production Bluetooth backend, kept outside CORE/headless desktop linkage.
BT_OUT ?= build/bluetooth
NET_OUT ?= build/net
DESKTOP_BT_APP_SRC := src/desktop_bluetooth.c src/desktop_guestnet.c
DESKTOP_BT_APP_HDR := include/desktop_bluetooth.h include/desktop_guestnet.h
DESKTOP_BT_APP_INC := -Ibluetooth/include -Ibluetooth/src -Inet/include
DESKTOP_BT_APP_ARCHIVES := $(BT_OUT)/libos999bt.a $(NET_OUT)/libos999net.a
ifeq ($(shell uname -s),Darwin)
DESKTOP_BT_APP_SRC += src/desktop_bt_darwin.c
DESKTOP_BT_APP_HOST := build/desktop_bt_transport.o
DESKTOP_BT_APP_LIBS := -framework Foundation -framework IOBluetooth
build/desktop_bt_transport.o: bluetooth/src/bt_darwin.m bluetooth/src/bt_darwin.h bluetooth/include/os999_bluetooth.h
	mkdir -p build
	$(CC) $(CFLAGS) $(DESKTOP_BT_APP_INC) -fobjc-arc -c $< -o $@
else
DESKTOP_BT_APP_SRC += src/desktop_bt_none.c
endif
