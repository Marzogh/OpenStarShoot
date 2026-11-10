#include "internal.h"
#include "assertions.h"
#include <stdio.h>
#include <string.h>

typedef struct {int calls,mode;uint8_t opcode;size_t saved,partial;unsigned image_reads;} fixture;
static int fake(void *u,uint8_t ep,uint8_t *b,int n,int *actual,unsigned timeout) {
    fixture *f=u;f->calls++;assert(timeout>0);
    if(ep==8) {
        assert(n==6 && b[0]==0xa5 && b[1]!=6 && b[1]!=10);
        if(b[1]==11)assert(!memcmp(b,(uint8_t[]){0xa5,11,0,0,4,0x1c},6));
        f->opcode=b[1];*actual=6;
        if(f->mode==1)*actual=3;
        return 0;
    }
    assert(ep==0x82);
    if(n==8) {
        memset(b,0,8);b[0]=0xa5;b[2]=f->opcode;b[4]=1;
        if(f->opcode==1)b[4]=5;
        if(f->opcode==2)b[4]=2;
        if(f->opcode==9){b[4]=0xab;b[6]=0xcd;}
        if(f->opcode==3 && f->mode==4)b[4]=0;
        if(f->mode==2)b[2]=0x7f;
        *actual=8;return 0;
    }
    assert(n==1024);
    memset(b,0x5a,1024);
    f->image_reads++;
    *actual=f->mode==3 ? 512:(f->mode==5 && f->image_reads==1 ? 512:(f->mode==6 ? 0:1024));
    return f->mode==3 ? LIBUSB_ERROR_TIMEOUT:0;
}
static oss_camera camera(fixture *f) {
    oss_camera c={0};c.synchronized=1;c.profile=5;c.initialized=1;c.phase=OSS_IDLE;
    c.expected_bytes=1434u*1052u*2u;
    c.test_transfer=fake;c.test_user=f;return c;
}
static int sink(void *u,const uint8_t *b,size_t n) {
    fixture *f=u;assert(b[0]==0x5a);f->saved+=n;return 0;
}
int main(void) {
    uint8_t tx[6];oss_reply r;oss_exposure e={100,1,5};
    puts("Protocol: exposure boundaries and excluded input validation");
    assert(!oss_encode_exposure(&e,tx));assert(!memcmp(tx,(uint8_t[]){0xa5,3,6,0,100,3},6));
    e.exposure_ms=9999;assert(!oss_encode_exposure(&e,tx));assert(tx[2]==6 && tx[3]==0x27 && tx[4]==0x0f);
    e.exposure_ms=10000;e.readout_speed=5;
    assert(!oss_encode_exposure(&e,tx));assert(!memcmp(tx,(uint8_t[]){0xa5,3,7,0,100,3},6));
    e.exposure_ms=0;assert(oss_encode_exposure(&e,tx)==OSS_EINVAL);
    e.exposure_ms=60001;assert(oss_encode_exposure(&e,tx)==OSS_EINVAL);
    e.exposure_ms=100;e.bin=3;assert(oss_encode_exposure(&e,tx)==OSS_EINVAL);
    puts("Protocol: reply length/echo checks; ignored bytes are not assumed zero");
    uint8_t b[]={0xa5,0x99,1,0x77,5,0x88,0,0x66};
    assert(!oss_decode_reply(1,b,8,&r));assert(r.bytes[4]==5);
    assert(oss_decode_reply(2,b,8,&r)==OSS_EPROTO);
    assert(oss_decode_reply(1,b,7,&r)==OSS_EPROTO);
    puts("Transport: initialize and counter decoding through simulated bulk I/O");
    fixture f={0};oss_camera c=camera(&f);uint8_t cap;oss_status status;uint16_t counter;
    assert(!oss_initialize(&c,&cap,&status));assert(f.calls==6);
    assert(!oss_get_counter(&c,&counter,NULL));assert(counter==0xabcd);
    puts("Acquisition: start, readiness, stop; rejected starts do not advance phase");
    e=(oss_exposure){100,1,5};
    assert(!oss_start_exposure(&c,&e,NULL));assert(c.phase==OSS_EXPOSING);
    assert(!oss_wait_ready(&c,1000,NULL,NULL));assert(c.phase==OSS_READY);
    assert(!oss_stop(&c,NULL));assert(c.phase==OSS_IDLE);
    f.mode=4;assert(oss_start_exposure(&c,&e,NULL)==OSS_EREJECTED);
    assert(c.phase==OSS_IDLE);f.mode=0;
    puts("Guards: no USB writes for invalid settings or unsupported window");
    int before=f.calls;
    assert(oss_set_offset(&c,512,NULL)==OSS_EINVAL);
    assert(oss_set_cooling_flags(&c,4,NULL)==OSS_EINVAL);
    assert(oss_set_vertical_window(&c,1,1040,NULL)==OSS_EINVAL);
    assert(f.calls==before);
    puts("Transport: short OUT and stale reply poison the session without retries");
    f=(fixture){.mode=1};c=camera(&f);
    assert(oss_get_counter(&c,&counter,NULL)==OSS_EPROTO);assert(f.calls==1);
    assert(oss_get_counter(&c,&counter,NULL)==OSS_ESTATE);assert(f.calls==1);
    f=(fixture){.mode=2};c=camera(&f);
    assert(oss_get_counter(&c,&counter,NULL)==OSS_EPROTO);assert(f.calls==2);
    puts("Capture: full 1024-byte transfers stop at exact image length with retained read-ahead");
    f=(fixture){0};c=camera(&f);c.phase=OSS_READY;oss_capture_result result;
    assert(!oss_capture_raw(&c,sink,&f,1000,NULL,NULL,&result));
    assert(result.bytes==3017136 && f.saved==3017136 && !result.end_short_packet);
    assert(result.transfers==2947 && result.wire_bytes==3017728 && result.read_ahead_bytes==592);
    assert(result.read_ahead[0]==0x5a);
    assert(!oss_stop(&c,NULL));assert(c.phase==OSS_IDLE);
    puts("Capture: short reads are accumulated, not mistaken for end of frame");
    f=(fixture){.mode=5};c=camera(&f);c.phase=OSS_READY;
    assert(!oss_capture_raw(&c,sink,&f,1000,NULL,NULL,&result));
    assert(result.bytes==3017136 && result.end_short_packet && result.read_ahead_bytes==80);
    puts("Capture: zero read before completion is incomplete, not a successful frame");
    f=(fixture){.mode=6};c=camera(&f);c.phase=OSS_READY;
    assert(oss_capture_raw(&c,sink,&f,1000,NULL,NULL,&result)==OSS_EFRAME);
    assert(result.bytes==0 && c.phase==OSS_UNCERTAIN);
    puts("Capture: timeout partial bytes preserved, no speculative cleanup after failure");
    f=(fixture){.mode=3};c=camera(&f);c.phase=OSS_READY;
    assert(oss_capture_raw(&c,sink,&f,1000,NULL,NULL,&result)==LIBUSB_ERROR_TIMEOUT);
    assert(result.bytes==512 && f.saved==512 && c.phase==OSS_UNCERTAIN);
    before=f.calls;assert(oss_stop(&c,NULL)==OSS_ESTATE);assert(f.calls==before);
    puts("All offline protocol/transport checks passed. No camera opened.");
    return 0;
}
