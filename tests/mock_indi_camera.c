/* Synthetic INDI-test backend. This file has no USB implementation. */
#include "openstarshoot.h"
#include <stdlib.h>
#include <string.h>
struct oss_camera {oss_phase phase;oss_open_options options;unsigned polls;size_t raw_bytes;};
static void command(oss_camera *c,unsigned opcode,const unsigned char p[4],unsigned value){
 uint8_t tx[6]={0xa5,(uint8_t)opcode,0,0,0,0},rx[8]={0xa5,0,(uint8_t)opcode,0,(uint8_t)value,0,0,0};
 if(p)memcpy(tx+2,p,4);
 if(c->options.trace){c->options.trace(c->options.trace_user,8,tx,6,6,0);c->options.trace(c->options.trace_user,0x82,rx,8,8,0);}
}
int oss_open(oss_camera **out,const oss_open_options *options){*out=calloc(1,sizeof(**out));if(!*out)return OSS_EIO;(*out)->options=*options;(*out)->phase=OSS_IDLE;return 0;}
void oss_close(oss_camera *c){free(c);}
const char *oss_error(int e){return e?"synthetic failure":"success";}
oss_phase oss_get_phase(const oss_camera *c){return c->phase;}
int oss_initialize(oss_camera *c,uint8_t *cap,oss_status *s){*cap=128;s->state=0;s->auxiliary=0;command(c,1,0,5);command(c,7,0,128);command(c,2,0,0);return 0;}
static int start(oss_camera *c,const oss_exposure *e,const oss_roi *roi){
 if(c->phase!=OSS_IDLE||e->readout_speed!=5)return OSS_ESTATE;
 oss_image_geometry g;oss_roi_geometry rg;unsigned height,y=0;
 if(roi){if(oss_get_roi_geometry_bin(e->bin,roi,&rg))return OSS_EINVAL;c->raw_bytes=rg.raw_bytes;height=rg.raw_height;y=roi->y/e->bin;}
 else {if(oss_get_image_geometry(e->bin,&g))return OSS_EINVAL;c->raw_bytes=g.raw_bytes;height=g.raw_height;}
 unsigned n=e->exposure_ms>=10000?e->exposure_ms/100:e->exposure_ms;
 const uint8_t window[4]={(uint8_t)(y>>8),(uint8_t)y,(uint8_t)(height>>8),(uint8_t)height};
 uint8_t p[4]={(uint8_t)(6|(e->exposure_ms>=10000)),(uint8_t)(n>>8),(uint8_t)n,(uint8_t)(e->bin==2?7:3)};
 command(c,11,window,1);command(c,3,p,1);c->phase=OSS_EXPOSING;c->polls=0;return 0;
}
int oss_start_exposure(oss_camera *c,const oss_exposure *e,oss_reply *reply){(void)reply;return start(c,e,NULL);}
int oss_start_exposure_roi(oss_camera *c,const oss_exposure *e,const oss_roi *roi,oss_reply *reply){(void)reply;return start(c,e,roi);}
int oss_get_status(oss_camera *c,oss_status *s,oss_reply *reply){
 (void)reply;s->auxiliary=0;s->state=c->phase==OSS_EXPOSING && ++c->polls==1?1:c->phase==OSS_IDLE?0:2;
 if(s->state==2){c->phase=OSS_READY;}
 command(c,2,0,s->state);return 0;
}
int oss_stop(oss_camera *c,oss_reply *reply){(void)reply;command(c,5,0,1);c->phase=OSS_IDLE;return 0;}
int oss_capture_raw(oss_camera *c,oss_frame_fn sink,void *user,unsigned deadline,oss_cancel_fn cancel,void *cancel_user,oss_capture_result *r){
 (void)deadline;(void)cancel;(void)cancel_user;if(c->phase!=OSS_READY)return OSS_ESTATE;
 memset(r,0,sizeof(*r));r->expected_bytes=c->raw_bytes;command(c,4,0,1);c->phase=OSS_READING;
 while(r->bytes<r->expected_bytes){uint8_t b[1024];size_t n=r->expected_bytes-r->bytes;if(n>1024)n=1024;
  for(size_t k=0;k<n;k++){size_t offset=r->bytes+k,word=offset/2;uint16_t p=(uint16_t)(((word/1434)*137+(word%1434)*31)^0xabcd);b[k]=(uint8_t)(offset&1?p>>8:p);}
  if(c->options.trace)c->options.trace(c->options.trace_user,0x82,b,1024,n,0);
  if(sink(user,b,n)){return OSS_EIO;}
  r->bytes+=n;r->wire_bytes+=n;r->transfers++;if(n<1024)r->end_short_packet=1;
 }c->phase=OSS_READY;return 0;
}
