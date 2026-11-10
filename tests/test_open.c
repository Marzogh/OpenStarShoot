/* Exercise the real open/close path with synthetic libusb enumeration only. */
#define libusb_init mock_init
#define libusb_exit mock_exit
#define libusb_get_device_list mock_get_device_list
#define libusb_free_device_list mock_free_device_list
#define libusb_get_device_descriptor mock_get_device_descriptor
#define libusb_get_active_config_descriptor mock_get_active_config_descriptor
#define libusb_free_config_descriptor mock_free_config_descriptor
#define libusb_open mock_open
#define libusb_close mock_close
#define libusb_get_configuration mock_get_configuration
#define libusb_kernel_driver_active mock_kernel_driver_active
#define libusb_claim_interface mock_claim_interface
#define libusb_release_interface mock_release_interface
#include "../src/openstarshoot.c"
#include "assertions.h"
#include <stdio.h>

static int kernel_result,claims,releases,closes,exits;
static char context_token,device_token,handle_token;
static libusb_device *devices[]={(libusb_device *)&device_token,NULL};
static const struct libusb_endpoint_descriptor endpoints[]={
    {.bEndpointAddress=0x08,.bmAttributes=LIBUSB_TRANSFER_TYPE_BULK,.wMaxPacketSize=512},
    {.bEndpointAddress=0x82,.bmAttributes=LIBUSB_TRANSFER_TYPE_BULK,.wMaxPacketSize=512}
};
static const struct libusb_interface_descriptor alternate={
    .bInterfaceNumber=0,.bAlternateSetting=0,.bNumEndpoints=2,
    .bInterfaceClass=0xff,.endpoint=endpoints
};
static const struct libusb_interface interface={.altsetting=&alternate,.num_altsetting=1};
static struct libusb_config_descriptor config={.bNumInterfaces=1,.bConfigurationValue=1,.interface=&interface};

int LIBUSB_CALL mock_init(libusb_context **out) {*out=(libusb_context *)&context_token;return 0;}
void LIBUSB_CALL mock_exit(libusb_context *ctx) {assert(ctx==(libusb_context *)&context_token);exits++;}
ssize_t LIBUSB_CALL mock_get_device_list(libusb_context *ctx,libusb_device ***out) {(void)ctx;*out=devices;return 1;}
void LIBUSB_CALL mock_free_device_list(libusb_device **list,int unref) {assert(list==devices && unref==1);}
int LIBUSB_CALL mock_get_device_descriptor(libusb_device *dev,struct libusb_device_descriptor *out) {
    assert(dev==devices[0]);memset(out,0,sizeof(*out));out->idVendor=0x1856;out->idProduct=0x001e;return 0;
}
int LIBUSB_CALL mock_get_active_config_descriptor(libusb_device *dev,struct libusb_config_descriptor **out) {assert(dev==devices[0]);*out=&config;return 0;}
void LIBUSB_CALL mock_free_config_descriptor(struct libusb_config_descriptor *cfg) {assert(cfg==&config);}
int LIBUSB_CALL mock_open(libusb_device *dev,libusb_device_handle **out) {assert(dev==devices[0]);*out=(libusb_device_handle *)&handle_token;return 0;}
void LIBUSB_CALL mock_close(libusb_device_handle *dev) {assert(dev==(libusb_device_handle *)&handle_token);closes++;}
int LIBUSB_CALL mock_get_configuration(libusb_device_handle *dev,int *out) {(void)dev;*out=1;return 0;}
int LIBUSB_CALL mock_kernel_driver_active(libusb_device_handle *dev,int index) {(void)dev;assert(index==0);return kernel_result;}
int LIBUSB_CALL mock_claim_interface(libusb_device_handle *dev,int index) {(void)dev;assert(index==0);claims++;return 0;}
int LIBUSB_CALL mock_release_interface(libusb_device_handle *dev,int index) {(void)dev;assert(index==0);releases++;return 0;}
int main(void) {
    const int results[]={0,LIBUSB_ERROR_NOT_SUPPORTED,1,LIBUSB_ERROR_IO};
    const int expected[]={0,0,LIBUSB_ERROR_BUSY,LIBUSB_ERROR_IO};
    puts("USB open: absent/unsupported kernel-driver query, busy driver and real query error; all USB calls simulated");
    for(unsigned i=0;i<sizeof(results)/sizeof(results[0]);i++) {
        oss_camera *camera=NULL;
        kernel_result=results[i];claims=releases=closes=exits=0;
        assert(oss_open(&camera,NULL)==expected[i]);
        if(!expected[i]) {assert(camera && claims==1);oss_close(camera);assert(releases==1);}
        else {assert(!camera && claims==0 && releases==0);}
        assert(closes==1 && exits==1);
    }
    puts("PASS: USB open/close portability guards; no camera opened");
    return 0;
}
