/* macOS Bluetooth accelerator: device inquiry and L2CAP channels through
 * IOBluetooth. See bt_darwin.h for what this does and does not cover. */
#import <Foundation/Foundation.h>
#import <IOBluetooth/IOBluetooth.h>

#include "bt_darwin.h"

#include <stdlib.h>
#include <string.h>

struct BtDarwin {
    IOBluetoothDeviceInquiry *inquiry;
    id inquiry_delegate;
    IOBluetoothL2CAPChannel *channel;
    id channel_delegate;
    BtDarwinOpenFn on_open;
    BtDarwinDataFn on_data;
    BtDarwinCloseFn on_close;
    void *ctx;
    uint16_t next_handle;
};

/* The inquiry delegate has to be an object, so the C callback is kept
 * on the delegate itself. */
@interface BtDarwinInquiry : NSObject <IOBluetoothDeviceInquiryDelegate>
@property(nonatomic, assign) BtDarwinDeviceFn found;
@property(nonatomic, assign) void *ctx;
@end

@implementation BtDarwinInquiry
- (void)deviceInquiryDeviceFound:(IOBluetoothDeviceInquiry *)sender device:(IOBluetoothDevice *)device {
    if (!self.found || !device) return;
    const char *text = [[device addressString] UTF8String];
    BtAddress addr;
    if (!text || bt_addr_parse(text, &addr) != BT_OK) return;
    const char *name = [[device name] UTF8String];
    self.found(self.ctx, &addr, name ? name : "", (uint32_t)[device classOfDevice]);
}
@end

/* The channel delegate carries the pipe callbacks. */
@interface BtDarwinChannel : NSObject <IOBluetoothL2CAPChannelDelegate>
@property(nonatomic, assign) BtDarwinOpenFn on_open;
@property(nonatomic, assign) BtDarwinDataFn on_data;
@property(nonatomic, assign) BtDarwinCloseFn on_close;
@property(nonatomic, assign) void *ctx;
@property(nonatomic, assign) uint16_t handle;
- (const BtDarwinPipe *)pipe;
@end

@implementation BtDarwinChannel {
    BtDarwinPipe _pipe;
}
- (void)l2capChannelData:(IOBluetoothL2CAPChannel *)channel data:(void *)bytes length:(size_t)length {
    (void)channel;
    if (self.on_data && bytes && length) self.on_data(self.ctx, (const uint8_t *)bytes, length);
}

- (void)l2capChannelOpenComplete:(IOBluetoothL2CAPChannel *)channel status:(IOReturn)error {
    (void)channel;
    if (error != kIOReturnSuccess) {
        if (self.on_close) self.on_close(self.ctx);
        return;
    }
    if (self.on_open) self.on_open(self.ctx, self.handle, &_pipe);
}

- (const BtDarwinPipe *)pipe {
    return &_pipe;
}

- (void)l2capChannelClosed:(IOBluetoothL2CAPChannel *)channel {
    (void)channel;
    if (self.on_close) self.on_close(self.ctx);
}
@end

static long darwin_write(void *ctx, const void *p, size_t n) {
    IOBluetoothL2CAPChannel *channel = (__bridge IOBluetoothL2CAPChannel *)ctx;
    if (!channel || !n) return 0;
    IOReturn rc = [channel writeSync:(void *)p length:(UInt16)n];
    return rc == kIOReturnSuccess ? (long)n : -1;
}

static long darwin_read(void *ctx, void *p, size_t cap) {
    /* The channel delegate pushes the data, so there is nothing to pull. */
    (void)ctx; (void)p; (void)cap;
    return 0;
}

int bt_darwin_available(void) {
    return 1;
}

BtDarwin *bt_darwin_new(void) {
    return calloc(1, sizeof(BtDarwin));
}

void bt_darwin_free(BtDarwin *d) {
    if (!d) return;
    bt_darwin_stop_discover(d);
    bt_darwin_close_l2cap(d);
    free(d);
}

int bt_darwin_discover(BtDarwin *d, uint32_t seconds, BtDarwinDeviceFn fn, void *ctx) {
    if (!d) return BT_EINVAL;
    bt_darwin_stop_discover(d);
    BtDarwinInquiry *delegate = [[BtDarwinInquiry alloc] init];
    delegate.found = fn;
    delegate.ctx = ctx;
    IOBluetoothDeviceInquiry *inquiry = [[IOBluetoothDeviceInquiry alloc] initWithDelegate:delegate];
    if (!inquiry) {
        return BT_ENOMEM;
    }
    [inquiry setSearchCriteria:kBluetoothServiceClassMajorAny
               majorDeviceClass:kBluetoothDeviceClassMajorAny
                minorDeviceClass:kBluetoothDeviceClassMinorAny];
    inquiry.inquiryLength = (uint8_t)(seconds ? seconds : 10);
    IOReturn rc = [inquiry start];
    if (rc != kIOReturnSuccess) {
        d->inquiry = nil;
        d->inquiry_delegate = nil;
        return BT_EIO;
    }
    d->inquiry = inquiry;
    d->inquiry_delegate = delegate;
    return BT_OK;
}

int bt_darwin_stop_discover(BtDarwin *d) {
    if (!d || !d->inquiry) return BT_OK;
    [d->inquiry stop];
    d->inquiry = nil;
    d->inquiry_delegate = nil;
    return BT_OK;
}

int bt_darwin_open_l2cap(BtDarwin *d, const BtAddress *peer, uint16_t psm,
                         BtDarwinOpenFn on_open, BtDarwinDataFn on_data, BtDarwinCloseFn on_close,
                         void *ctx) {
    if (!d || !peer) return BT_EINVAL;
    if (d->channel) return BT_EBUSY;
    char text[24];
    bt_addr_format(peer, text, sizeof text);
    IOBluetoothDevice *device =
        [IOBluetoothDevice deviceWithAddressString:[NSString stringWithUTF8String:text]];
    if (!device) return BT_ENOENT;
    if ([device isConnected] == NO && [device openConnection] != kIOReturnSuccess) return BT_EIO;

    BtDarwinChannel *delegate = [[BtDarwinChannel alloc] init];
    delegate.on_open = on_open;
    delegate.on_data = on_data;
    delegate.on_close = on_close;
    delegate.ctx = ctx;
    delegate.handle = d->next_handle++;

    IOBluetoothL2CAPChannel *channel = nil;
    IOReturn rc = [device openL2CAPChannelSync:&channel withPSM:(BluetoothL2CAPPSM)psm
                                     delegate:delegate];
    if (rc != kIOReturnSuccess || !channel) return BT_EIO;
    BtDarwinPipe *pipe = (BtDarwinPipe *)delegate.pipe;
    pipe->write = darwin_write;
    pipe->read = darwin_read;
    pipe->ctx = (__bridge void *)channel;
    d->channel = channel;
    d->channel_delegate = delegate;
    return BT_OK;
}

int bt_darwin_close_l2cap(BtDarwin *d) {
    if (!d || !d->channel) return BT_OK;
    [d->channel closeChannel];
    d->channel = nil;
    d->channel_delegate = nil;
    return BT_OK;
}
