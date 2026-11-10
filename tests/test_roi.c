#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef struct {unsigned bin;oss_roi roi;unsigned op,state,reads,calls;size_t wire,saved;uint8_t *raw;} fixture;
static uint16_t pixel(unsigned y,unsigned x){return (uint16_t)((y*137u+x*31u)^0xabcdu);}
static int fake(void *u,uint8_t ep,uint8_t *b,int n,int *actual,unsigned timeout){
 fixture *f=u;f->calls++;assert(timeout);
 if(ep==8){assert(n==6&&b[0]==0xa5);f->op=b[1];
  if(b[1]==11){unsigned h=f->roi.height/f->bin+12;uint8_t p[]={0xa5,11,(f->roi.y/f->bin)>>8,(f->roi.y/f->bin)&255,h>>8,h&255};assert(!memcmp(b,p,6));}
  else if(b[1]==3){assert(!memcmp(b,(uint8_t[]){0xa5,3,6,0,100,f->bin==2?7:3},6));f->state=2;}
  else if(b[1]==5)f->state=0;
  else assert(b[1]==2||b[1]==4);
  *actual=6;return 0;
 }
 assert(ep==0x82);
 if(n==8){memset(b,0,8);b[0]=0xa5;b[2]=(uint8_t)f->op;b[4]=f->op==2?f->state:1;*actual=8;return 0;}
 assert(n==1024);size_t total=(size_t)1434*(f->roi.height/f->bin+12)*2,count=total-f->wire;if(count>1024)count=1024;assert(count);
 for(size_t k=0;k<count;k++){size_t offset=f->wire+k,word=offset/2;uint16_t v=pixel((unsigned)(word/1434)+f->roi.y,(unsigned)(word%1434));b[k]=(uint8_t)(offset&1?v>>8:v);}
 f->wire+=count;f->reads++;*actual=(int)count;return 0;
}
static int sink(void *u,const uint8_t *b,size_t n){fixture *f=u;memcpy(f->raw+f->saved,b,n);f->saved+=n;return 0;}
static void check(unsigned bin,oss_roi roi){
 fixture f={.bin=bin,.roi=roi};oss_roi_geometry g;assert(!oss_get_roi_geometry_bin(bin,&roi,&g));assert(g.raw_width==1434&&g.raw_height==roi.height/bin+12&&g.raw_bytes==(size_t)1434*g.raw_height*2);
 f.raw=malloc(g.raw_bytes);assert(f.raw);oss_camera c={0};c.profile=5;c.initialized=1;c.synchronized=1;c.phase=OSS_IDLE;c.test_transfer=fake;c.test_user=&f;
 oss_exposure e={100,bin,5};oss_capture_result result;oss_status status;
 assert(!oss_start_exposure_roi(&c,&e,&roi,NULL));assert(c.expected_bytes==g.raw_bytes);
 assert(!oss_wait_ready(&c,1000,NULL,NULL));assert(!oss_capture_raw(&c,sink,&f,1000,NULL,NULL,&result));
 assert(result.bytes==g.raw_bytes&&f.saved==g.raw_bytes&&result.wire_bytes==g.raw_bytes&&!result.read_ahead_bytes);
 assert(!oss_stop(&c,NULL));assert(!oss_get_status(&c,&status,NULL));assert(!status.state&&c.phase==OSS_IDLE);
 size_t count=(size_t)g.active_width*g.active_height;uint16_t *out=malloc(count*sizeof(*out));uint8_t *before=malloc(g.raw_bytes);assert(out&&before);memcpy(before,f.raw,g.raw_bytes);
 assert(!oss_reconstruct_roi_bin(bin,&roi,f.raw,g.raw_bytes,out,count));
 for(unsigned y=0;y<g.active_height;y++)for(unsigned x=0;x<g.active_width;x++){uint16_t v=pixel(y+roi.y+12/bin,x*bin+roi.x+(31/bin)*bin);assert(out[(size_t)y*g.active_width+x]==(bin==2?(v&0xfffeu):v));}
 assert(!memcmp(before,f.raw,g.raw_bytes));free(before);free(out);free(f.raw);
}
int main(void){
 puts("ROI: centered and odd-offset 512x512 command/capture/reconstruction; full-width raw rows preserved");
 check(1,(oss_roi){440,264,512,512});check(1,(oss_roi){73,101,512,512});check(2,(oss_roi){440,264,512,512});check(2,(oss_roi){74,102,512,512});
 fixture rejected={0};oss_camera c={0};c.profile=5;c.initialized=1;c.synchronized=1;c.phase=OSS_IDLE;c.test_transfer=fake;c.test_user=&rejected;
 oss_exposure bin2={100,2,5};oss_roi selected={73,101,512,512},full={74,102,511,512};
 assert(oss_start_exposure_roi(&c,&bin2,&selected,NULL)==OSS_EINVAL);
 assert(oss_start_exposure_roi(&c,&bin2,&full,NULL)==OSS_EINVAL);
 assert(!rejected.calls && c.phase==OSS_IDLE && !c.expected_bytes);
 puts("PASS: unaligned bin-2 origins and dimensions rejected before transfer");
 oss_roi_geometry g;oss_roi bad={1000,0,512,512};assert(oss_get_roi_geometry(&bad,&g)==OSS_EINVAL);
 puts("PASS: exact window bytes, byte budgets, all reconstructed pixels and simulated idle; no camera opened.");return 0;
}
