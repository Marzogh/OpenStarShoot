#include "internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RAW_BYTES (1434u*532u*2u)
typedef struct {unsigned opcode,state,reads;size_t wire,saved;uint8_t *raw;} fixture;
static uint16_t pixel(unsigned y,unsigned x) {
    const uint16_t values[]={0,32767,32768,65535};
    if(y==6 && x>=30 && x<=36 && !(x&1))return values[(x-30)/2];
    return (uint16_t)((y*137u+x*31u)^0xabcdu);
}
static int fake(void *u,uint8_t ep,uint8_t *b,int n,int *actual,unsigned timeout) {
    fixture *f=u;assert(timeout>0);
    if(ep==8) {
        assert(n==6 && b[0]==0xa5);f->opcode=b[1];
        if(b[1]==11)assert(!memcmp(b,(uint8_t[]){0xa5,11,0,0,2,0x14},6));
        else if(b[1]==3){assert(!memcmp(b,(uint8_t[]){0xa5,3,6,0,100,7},6));f->state=2;}
        else if(b[1]==5)f->state=0;
        else assert(b[1]==2 || b[1]==4);
        *actual=6;return 0;
    }
    assert(ep==0x82);
    if(n==8) {
        memset(b,0,8);b[0]=0xa5;b[2]=(uint8_t)f->opcode;
        b[4]=f->opcode==2 ? (uint8_t)f->state:1;*actual=8;return 0;
    }
    assert(n==1024 && f->wire<RAW_BYTES);
    size_t count=RAW_BYTES-f->wire;if(count>1024)count=1024;
    for(size_t k=0;k<count;k++) {
        size_t offset=f->wire+k,word=offset/2u;
        uint16_t v=pixel((unsigned)(word/1434u),(unsigned)(word%1434u));
        b[k]=(uint8_t)(offset&1u ? v>>8:v);
    }
    f->wire+=count;f->reads++;*actual=(int)count;return 0;
}
static int sink(void *u,const uint8_t *b,size_t n) {
    fixture *f=u;assert(f->saved+n<=RAW_BYTES);
    memcpy(f->raw+f->saved,b,n);f->saved+=n;return 0;
}
int main(void) {
    puts("Binning: one nominal full-frame 2x2 command, capture and every-pixel active reconstruction check");
    oss_image_geometry g;assert(!oss_get_image_geometry(2,&g));
    assert(g.raw_width==1434 && g.raw_height==532 && g.raw_bytes==RAW_BYTES);
    assert(g.active_width==696 && g.active_height==520);
    fixture f={0};f.raw=malloc(RAW_BYTES);assert(f.raw);
    oss_camera c={0};c.profile=5;c.initialized=1;c.synchronized=1;c.phase=OSS_IDLE;
    c.test_transfer=fake;c.test_user=&f;
    oss_exposure e={100,2,5};oss_capture_result result;oss_status status;
    assert(!oss_start_exposure(&c,&e,NULL));assert(c.expected_bytes==RAW_BYTES);
    assert(!oss_wait_ready(&c,1000,NULL,NULL));assert(c.phase==OSS_READY);
    assert(!oss_capture_raw(&c,sink,&f,1000,NULL,NULL,&result));
    assert(result.bytes==RAW_BYTES && f.saved==RAW_BYTES && result.wire_bytes==RAW_BYTES);
    assert(result.transfers==1491 && f.reads==1491 && !result.read_ahead_bytes && result.end_short_packet);
    assert(!oss_stop(&c,NULL));assert(!oss_get_status(&c,&status,NULL));assert(status.state==0 && c.phase==OSS_IDLE);
    size_t count=(size_t)g.active_width*g.active_height;
    uint16_t *out=malloc(count*sizeof(*out));uint8_t *before=malloc(RAW_BYTES);assert(out&&before);
    memcpy(before,f.raw,RAW_BYTES);
    assert(!oss_reconstruct_active(2,f.raw,RAW_BYTES,out,count));
    for(unsigned y=0;y<520;y++)for(unsigned x=0;x<696;x++)assert(out[(size_t)y*696u+x]==(pixel(y+6u,(x+15u)*2u)&0xfffeu));
    assert(!memcmp(before,f.raw,RAW_BYTES));free(before);free(out);free(f.raw);
    puts("PASS: mode7/window532, exact 1434x532 raw stream, 696x520 active pixels and idle cleanup; no camera opened.");
    return 0;
}
