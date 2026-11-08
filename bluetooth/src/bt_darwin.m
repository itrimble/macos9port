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
@property(nonatomic, assign) BtDarwinDoneFn done;
@property(nonatomic, assign) void *ctx;
@end

@implementation BtDarwinInquiry
- (void)deviceInquiryComplete:(IOBluetoothDeviceInquiry *)sender error:(IOReturn)error
                     aborted:(BOOL)aborted {
    (void)sender;
    BtDarwinDoneFn done = self.done;
    self.done = NULL;
    if (done) done(self.ctx, (error == kIOReturnSuccess && !aborted) ? BT_OK : BT_EIO);
}
- (void)deviceInquiryDeviceFound:(IOBluetoothDeviceInquiry *)sender device:(IOBluetoothDevice *)device {
    (void)sender;
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
@property(nonatomic, assign) BtDarwin *owner;
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
    /* Synchronous open reports exactly once after the pipe is initialized. */
    (void)channel; (void)error;
}

- (const BtDarwinPipe *)pipe {
    return &_pipe;
}

- (void)l2capChannelClosed:(IOBluetoothL2CAPChannel *)channel {
    (void)channel;
    BtDarwinCloseFn notify = self.on_close;
    void *context = self.ctx;
    self.on_close = NULL;
    self.on_data = NULL;
    BtDarwin *owner = self.owner;
    self.owner = NULL;
    if (owner) {
        owner->channel = nil;
        owner->channel_delegate = nil;
    }
    if (notify) notify(context);
}
@end

static long darwin_write(void *ctx, const void *p, size_t n) {
    IOBluetoothL2CAPChannel *channel = (__bridge IOBluetoothL2CAPChannel *)ctx;
    if (!channel || !n) return 0;
    if (!p || n > UINT16_MAX || n > [channel outgoingMTU]) return -1;
    IOReturn rc = [channel writeSync:(void *)p length:(UInt16)n];
    return rc == kIOReturnSuccess ? (long)n : -1;
}

static long darwin_read(void *ctx, void *p, size_t cap) {
    /* The channel delegate pushes the data, so there is nothing to pull. */
    (void)ctx; (void)p; (void)cap;
    return 0;
}

int bt_darwin_available(void) {
    IOBluetoothHostController *host = [IOBluetoothHostController defaultController];
    return host && host.powerState == kBluetoothHCIPowerStateON;
}

int bt_darwin_local_address(BtDarwin *d, BtAddress *out) {
    if (!d || !out) return BT_EINVAL;
    NSString *address = [[IOBluetoothHostController defaultController] addressAsString];
    if (!address) return BT_ENOENT;
    return bt_addr_parse(address.UTF8String, out);
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
    return bt_darwin_discover_with_done(d, seconds, fn, NULL, ctx);
}

int bt_darwin_discover_with_done(BtDarwin *d, uint32_t seconds, BtDarwinDeviceFn fn,
                                BtDarwinDoneFn done, void *ctx) {
    if (!d || seconds > 60) return BT_EINVAL;
    bt_darwin_stop_discover(d);
    BtDarwinInquiry *delegate = [[BtDarwinInquiry alloc] init];
    delegate.found = fn;
    delegate.done = done;
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
    BtDarwinInquiry *delegate = d->inquiry_delegate;
    delegate.found = NULL;
    delegate.done = NULL;
    delegate.ctx = NULL;
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
    /* Install callbacks only once synchronous open and pipe setup succeed. */
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
    delegate.owner = d;
    delegate.on_data = on_data;
    delegate.on_close = on_close;
    if (on_open) on_open(ctx, delegate.handle, pipe);
    return BT_OK;
}

int bt_darwin_close_l2cap(BtDarwin *d) {
    if (!d || !d->channel) return BT_OK;
    BtDarwinChannel *delegate = d->channel_delegate;
    delegate.owner = NULL;
    delegate.on_open = NULL;
    delegate.on_data = NULL;
    delegate.on_close = NULL;
    delegate.ctx = NULL;
    [d->channel closeChannel];
    d->channel = nil;
    d->channel_delegate = nil;
    return BT_OK;
}
