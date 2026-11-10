#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#define OUT_EP 0x08
#define IN_EP 0x82
#define RAW_LIMIT (1434u * 1052u * 2u)
#define BIN2_RAW_LIMIT (1434u * 532u * 2u)
static int configure_full_frame(oss_camera *, unsigned bin, oss_reply *);

static int transfer(oss_camera *c, uint8_t ep, uint8_t *data, int n,
                    int *actual, unsigned timeout) {
    int rc;
    *actual=0;
#ifdef OSS_TESTING
    if(c->test_transfer) rc=c->test_transfer(c->test_user,ep,data,n,actual,timeout);
    else
#endif
    rc=libusb_bulk_transfer(c->device,ep,data,n,actual,timeout);
    if(*actual<0 || *actual>n) { *actual=0; rc=OSS_EPROTO; }
    if(c->trace) c->trace(c->trace_user,ep,data,(size_t)n,(size_t)*actual,rc);
    return rc;
}
int oss_decode_reply(uint8_t op,const uint8_t *b,size_t n,oss_reply *out) {
    if(!b || !out) return OSS_EINVAL;
    if(n!=8 || b[0]!=0xa5 || b[2]!=op) return OSS_EPROTO;
    memcpy(out->bytes,b,8);
    return 0;
}
static int exchange(oss_camera *c,uint8_t op,const uint8_t p[4],oss_reply *reply) {
    uint8_t tx[6]={0xa5,op,0,0,0,0}, rx[8]={0};
    int n=0,rc;
    if(!c || !reply || !c->synchronized) return OSS_ESTATE;
    if(c->phase==OSS_READING) return OSS_ESTATE;
    /* A closed whitelist prevents excluded or arbitrary opcode writes. */
    switch(op) { case 1:case 2:case 3:case 4:case 5:case 7:case 8:case 9:case 11:case 12:break;
    default:return OSS_EINVAL; }
    if(p) memcpy(tx+2,p,4);
    rc=transfer(c,OUT_EP,tx,6,&n,4000);
    if(rc || n!=6) goto lost;
    rc=transfer(c,IN_EP,rx,8,&n,4000);
    if(rc) goto lost;
    rc=oss_decode_reply(op,rx,(size_t)n,reply);
    if(rc) goto lost;
    return 0;
lost:
    c->synchronized=0;
    c->phase=OSS_UNCERTAIN;
    return rc ? rc:OSS_EPROTO;
}
static int endpoints_ok(libusb_device *d) {
    struct libusb_config_descriptor *cfg=NULL;
    int rc=libusb_get_active_config_descriptor(d,&cfg),good=0;
    if(rc) return rc;
    if(cfg->bConfigurationValue==1 && cfg->bNumInterfaces==1) {
        const struct libusb_interface *i=&cfg->interface[0];
        if(i->num_altsetting==1) {
            const struct libusb_interface_descriptor *a=&i->altsetting[0];
            int in=0,out=0;
            for(int k=0;k<a->bNumEndpoints;k++) {
                const struct libusb_endpoint_descriptor *e=&a->endpoint[k];
                if((e->bmAttributes&3)!=LIBUSB_TRANSFER_TYPE_BULK || e->wMaxPacketSize!=512) continue;
                in|=e->bEndpointAddress==IN_EP;
                out|=e->bEndpointAddress==OUT_EP;
            }
            good=a->bInterfaceNumber==0 && a->bAlternateSetting==0 &&
                 a->bInterfaceClass==0xff && a->bNumEndpoints==2 && in && out;
        }
    }
    libusb_free_config_descriptor(cfg);
    return good ? 0:OSS_EPROTO;
}
int oss_open(oss_camera **out,const oss_open_options *o) {
    oss_camera *c;
    libusb_device **list=NULL,*selected=NULL;
    int matches=0,rc,config=0;
    ssize_t n;
    if(!out || (o && (!!o->bus!=!!o->address))) return OSS_EINVAL;
    *out=NULL;
    c=calloc(1,sizeof(*c));
    if(!c) return LIBUSB_ERROR_NO_MEM;
    c->synchronized=1;
    if(o) {c->trace=o->trace;c->trace_user=o->trace_user;}
    rc=libusb_init(&c->context);
    if(rc) goto fail;
    n=libusb_get_device_list(c->context,&list);
    if(n<0) {rc=(int)n;goto fail;}
    for(ssize_t k=0;k<n;k++) {
        struct libusb_device_descriptor d;
        if(libusb_get_device_descriptor(list[k],&d)) continue;
        if(d.idVendor!=0x1856 || (d.idProduct!=0x001e && d.idProduct!=0x0003)) continue;
        if(o && o->bus && (libusb_get_bus_number(list[k])!=o->bus ||
                           libusb_get_device_address(list[k])!=o->address)) continue;
        selected=list[k];matches++;
    }
    if(matches!=1) {rc=matches ? OSS_EAMBIGUOUS:LIBUSB_ERROR_NO_DEVICE;goto fail;}
    rc=endpoints_ok(selected);
    if(rc) goto fail;
    rc=libusb_open(selected,&c->device);
    if(rc) goto fail;
    rc=libusb_get_configuration(c->device,&config);
    if(rc) goto fail;
    if(config!=1) {rc=OSS_ESTATE;goto fail;}
    rc=libusb_kernel_driver_active(c->device,0);
    /* This query is unavailable on backends such as Windows. Claiming the
     * interface remains mandatory; never detach a driver or reset the camera. */
    if(rc!=0 && rc!=LIBUSB_ERROR_NOT_SUPPORTED) {rc=rc>0 ? LIBUSB_ERROR_BUSY:rc;goto fail;}
    rc=libusb_claim_interface(c->device,0);
    if(rc) goto fail;
    c->claimed=1;
    libusb_free_device_list(list,1);
    *out=c;
    return 0;
fail:
    if(list) libusb_free_device_list(list,1);
    oss_close(c);
    return rc;
}
void oss_close(oss_camera *c) {
    if(!c) return;
    if(c->claimed) libusb_release_interface(c->device,0);
    if(c->device) libusb_close(c->device);
    if(c->context) libusb_exit(c->context);
    free(c);
}
const char *oss_error(int rc) {
    switch(rc) {
    case 0:return "success";
    case OSS_EINVAL:return "invalid or unsupported parameter";
    case OSS_EPROTO:return "reply/descriptor mismatch or short transfer";
    case OSS_ESTATE:return "operation not allowed in current camera state";
    case OSS_EREJECTED:return "camera did not acknowledge the command";
    case OSS_ELIMIT:return "capture/wait safety bound reached";
    case OSS_EAMBIGUOUS:return "more than one matching camera; select bus/address";
    case OSS_ECANCELLED:return "cancelled";
    case OSS_EIO:return "evidence output failed";
    case OSS_EFRAME:return "image ended before the expected sample count";
    default:return libusb_error_name(rc);
    }
}
oss_phase oss_get_phase(const oss_camera *c) {return c ? c->phase:OSS_UNCERTAIN;}
int oss_identify(oss_camera *c,uint8_t *profile,oss_reply *reply) {
    oss_reply r;int rc;
    if(!profile) return OSS_EINVAL;
    rc=exchange(c,1,NULL,&r);
    if(!rc) {c->profile=*profile=r.bytes[4];if(reply)*reply=r;}
    return rc;
}
int oss_get_status(oss_camera *c,oss_status *s,oss_reply *reply) {
    oss_reply r;int rc;
    if(!s) return OSS_EINVAL;
    rc=exchange(c,2,NULL,&r);
    if(!rc) {
        s->state=r.bytes[4];s->auxiliary=r.bytes[6];
        if(reply)*reply=r;
        if(c->phase==OSS_EXPOSING && (s->state==0 || s->state==2)) c->phase=OSS_READY;
    }
    return rc;
}
int oss_get_capability(oss_camera *c,uint8_t *v,oss_reply *reply) {
    oss_reply r;int rc;
    if(!v) return OSS_EINVAL;
    rc=exchange(c,7,NULL,&r);
    if(!rc) {*v=r.bytes[4];if(reply)*reply=r;}
    return rc;
}
int oss_get_counter(oss_camera *c,uint16_t *v,oss_reply *reply) {
    oss_reply r;int rc;
    if(!v) return OSS_EINVAL;
    rc=exchange(c,9,NULL,&r);
    if(!rc) {*v=(uint16_t)((r.bytes[4]<<8)|r.bytes[6]);if(reply)*reply=r;}
    return rc;
}
int oss_initialize(oss_camera *c,uint8_t *cap,oss_status *s) {
    uint8_t profile;int rc;
    if(!c || !cap || !s) return OSS_EINVAL;
    rc=oss_identify(c,&profile,NULL);
    if(rc) return rc;
    if(profile!=5) return OSS_EPROTO;
    rc=oss_get_capability(c,cap,NULL);
    if(rc) return rc;
    rc=oss_get_status(c,s,NULL);
    if(rc) return rc;
    if(s->state!=0 && s->state!=2) return OSS_ESTATE;
    c->initialized=1;c->phase=OSS_IDLE;
    return 0;
}
int oss_get_image_geometry(unsigned bin,oss_image_geometry *out) {
    if(!out || (bin!=1 && bin!=2)) return OSS_EINVAL;
    *out=(oss_image_geometry){1434u,bin==1 ? 1052u:532u,
                             1392u/bin,1040u/bin,
                             bin==1 ? RAW_LIMIT:BIN2_RAW_LIMIT};
    return 0;
}
int oss_reconstruct_active(unsigned bin,const uint8_t *raw,size_t bytes,
                           uint16_t *output,size_t output_samples) {
    oss_image_geometry g;
    if(oss_get_image_geometry(bin,&g) || !raw || !output || bytes!=g.raw_bytes ||
       output_samples<(size_t)g.active_width*g.active_height) return OSS_EINVAL;
    /* Bin-2 wire rows remain full width. Its selected sample lattice has
     * 717 columns; top/left offsets are 6 rows and 15 selected columns. */
    unsigned top=12u/bin,left=31u/bin;
    for(unsigned y=0;y<g.active_height;y++) {
        for(unsigned x=0;x<g.active_width;x++) {
            size_t index=((size_t)(y+top)*g.raw_width+(x+left)*bin)*2u;
            uint16_t v=(uint16_t)(raw[index]|((uint16_t)raw[index+1]<<8));
            output[(size_t)y*g.active_width+x]=bin==2 ? (uint16_t)(v&0xfffeu):v;
        }
    }
    return 0;
}
int oss_get_roi_geometry_bin(unsigned bin,const oss_roi *roi,oss_roi_geometry *g) {
    if((bin!=1 && bin!=2) || !roi || !g || !roi->width || roi->width>1392 || !roi->height ||
       roi->height>1040 || roi->x>1392-roi->width || roi->y>1040-roi->height ||
       (bin==2 && ((roi->x|roi->y|roi->width|roi->height)&1u)))
        return OSS_EINVAL;
    *g=(oss_roi_geometry){1434,roi->height/bin+12,roi->width/bin,roi->height/bin,
                          (size_t)1434*(roi->height/bin+12)*2};
    return 0;
}
int oss_get_roi_geometry(const oss_roi *roi,oss_roi_geometry *g) {
    return oss_get_roi_geometry_bin(1,roi,g);
}
int oss_reconstruct_roi_bin(unsigned bin,const oss_roi *roi,const uint8_t *raw,size_t bytes,
                        uint16_t *out,size_t samples) {
    oss_roi_geometry g;
    if(oss_get_roi_geometry_bin(bin,roi,&g) || !raw || !out || bytes!=g.raw_bytes ||
       samples<(size_t)g.active_width*g.active_height)return OSS_EINVAL;
    for(unsigned y=0;y<g.active_height;y++)for(unsigned x=0;x<g.active_width;x++) {
        size_t i=((size_t)(y+12/bin)*1434+(31/bin)*bin+roi->x+x*bin)*2;
        uint16_t v=(uint16_t)(raw[i]|((uint16_t)raw[i+1]<<8));
        out[(size_t)y*g.active_width+x]=bin==2 ? (uint16_t)(v&0xfffeu):v;
    }
    return 0;
}
int oss_reconstruct_roi(const oss_roi *roi,const uint8_t *raw,size_t bytes,
                        uint16_t *out,size_t samples) {
    return oss_reconstruct_roi_bin(1,roi,raw,bytes,out,samples);
}
int oss_encode_exposure(const oss_exposure *e,uint8_t out[6]) {
    unsigned ticks;
    if(!e || !out || e->exposure_ms<1 || e->exposure_ms>60000 ||
       (e->bin!=1 && e->bin!=2) || e->readout_speed<1 || e->readout_speed>8) return OSS_EINVAL;
    ticks=e->exposure_ms>=10000 ? e->exposure_ms/100:e->exposure_ms;
    out[0]=0xa5;out[1]=3;
    out[2]=(uint8_t)((((0u-e->readout_speed)&7u)<<1)|(e->exposure_ms>=10000));
    out[3]=(uint8_t)(ticks>>8);out[4]=(uint8_t)ticks;out[5]=e->bin==2 ? 7:3;
    return 0;
}
int oss_start_exposure(oss_camera *c,const oss_exposure *e,oss_reply *reply) {
    uint8_t tx[6];oss_reply r;int rc=oss_encode_exposure(e,tx);
    if(rc) return rc;
    if(!c || !c->initialized || c->profile!=5 || c->phase!=OSS_IDLE) return OSS_ESTATE;
    /* The profile-5 +3 flag is set: original acquisition configures 0B
     * before 03. Do not use the generic half-height default. */
    rc=configure_full_frame(c,e->bin,NULL);
    if(rc) return rc;
    rc=exchange(c,3,tx+2,&r);
    if(rc) return rc;
    if(!r.bytes[4]) return OSS_EREJECTED;
    c->phase=OSS_EXPOSING;
    if(reply)*reply=r;
    return 0;
}
int oss_start_exposure_roi(oss_camera *c,const oss_exposure *e,const oss_roi *roi,oss_reply *reply) {
    uint8_t tx[6];oss_reply r;int rc=oss_encode_exposure(e,tx);
    if(rc) return rc;
    oss_roi_geometry geometry;
    if(oss_get_roi_geometry_bin(e->bin,roi,&geometry))return OSS_EINVAL;
    if(!c || !c->initialized || c->profile!=5 || c->phase!=OSS_IDLE) return OSS_ESTATE;
    /* The profile-5 +3 flag is set: original acquisition configures 0B
     * before 03. Do not use the generic half-height default. */
    rc=oss_set_vertical_window(c,roi->y/e->bin,roi->height/e->bin,NULL);
    if(rc) return rc;
    rc=exchange(c,3,tx+2,&r);
    if(rc) return rc;
    if(!r.bytes[4]) return OSS_EREJECTED;
    c->phase=OSS_EXPOSING;
    if(reply)*reply=r;
    return 0;
}
int oss_wait_ready(oss_camera *c,unsigned deadline,oss_cancel_fn cancel,void *user) {
    uint64_t end;
    if(!c || c->phase!=OSS_EXPOSING || deadline<1 || deadline>120000) return OSS_EINVAL;
    end=oss_monotonic_ms()+deadline;
    do {
        oss_status s;int rc;
        if(cancel && cancel(user)) return OSS_ECANCELLED;
        rc=oss_get_status(c,&s,NULL);
        if(rc) return rc;
        if(s.state==0 || s.state==2) {c->phase=OSS_READY;return 0;}
        if(s.state!=1) return OSS_EPROTO;
        oss_sleep_ms(100);
    } while(oss_monotonic_ms()<end);
    return OSS_ELIMIT;
}
int oss_capture_raw(oss_camera *c,oss_frame_fn sink,void *sink_user,unsigned deadline,
                    oss_cancel_fn cancel,void *cancel_user,oss_capture_result *res) {
    uint8_t b[1024];oss_reply r;uint64_t end;int rc=0;
    if(!c || !sink || !res || c->phase!=OSS_READY || deadline<1 || deadline>120000) return OSS_EINVAL;
    if(c->profile!=5 || c->expected_bytes<1434u*13u*2u ||
       c->expected_bytes>RAW_LIMIT || c->expected_bytes%(1434u*2u)) return OSS_ESTATE;
    memset(res,0,sizeof(*res));
    res->expected_bytes=c->expected_bytes;
    rc=exchange(c,4,NULL,&r);
    if(rc) {res->result=rc;return rc;}
    c->phase=OSS_READING;end=oss_monotonic_ms()+deadline;
    while(res->bytes<res->expected_bytes) {
        int n=0;uint64_t now=oss_monotonic_ms();unsigned timeout;
        if(cancel && cancel(cancel_user)) {rc=OSS_ECANCELLED;break;}
        if(now>=end) {rc=OSS_ELIMIT;break;}
        timeout=(unsigned)(end-now);if(timeout>4000)timeout=4000;
        rc=transfer(c,IN_EP,b,sizeof(b),&n,timeout);
        res->transfers++;
        res->wire_bytes+=(size_t)n;
        if(n) {
            size_t remaining=res->expected_bytes-res->bytes;
            size_t image=(size_t)n<remaining ? (size_t)n:remaining;
            res->bytes+=image;
            if(sink(sink_user,b,image)) {rc=OSS_EIO;break;}
            res->read_ahead_bytes=(size_t)n-image;
            if(res->read_ahead_bytes)memcpy(res->read_ahead,b+image,res->read_ahead_bytes);
        }
        if(rc) break; /* timeout partial bytes already preserved, no retry */
        if(n<(int)sizeof(b))res->end_short_packet=1;
        if(n==0) {rc=OSS_EFRAME;break;}
    }
    res->result=rc;
    /* Original code stops after the configured word budget and discards any
     * remainder of its final 1024-byte read. Retain that remainder separately.
     * This proves sample-budget completion, not device FIFO exhaustion. */
    if(!rc) c->phase=OSS_READY;
    else {c->phase=OSS_UNCERTAIN;c->synchronized=0;}
    return rc;
}
int oss_stop(oss_camera *c,oss_reply *reply) {
    oss_reply r;int rc;
    if(!c || !c->initialized) return OSS_ESTATE;
    rc=exchange(c,5,NULL,&r);
    if(rc) return rc;
    if(!r.bytes[4]) return OSS_EREJECTED;
    c->phase=OSS_IDLE;
    if(reply)*reply=r;
    return 0;
}
static int can_set(oss_camera *c) {return c && c->initialized && c->phase==OSS_IDLE;}
int oss_set_offset(oss_camera *c,int offset,oss_reply *reply) {
    uint16_t x=(uint16_t)offset;uint8_t p[4]={0x3c,(uint8_t)(x>>8),(uint8_t)x,0};oss_reply r;
    if(offset< -512 || offset>511) return OSS_EINVAL;
    if(!can_set(c)) return OSS_ESTATE;
    int rc=exchange(c,8,p,&r);if(!rc && reply)*reply=r;return rc;
}
int oss_set_cooling_flags(oss_camera *c,unsigned flags,oss_reply *reply) {
    uint8_t p[4]={(uint8_t)flags,0,0,0};oss_reply r;
    if(flags>3) return OSS_EINVAL;
    if(!can_set(c)) return OSS_ESTATE;
    int rc=exchange(c,12,p,&r);if(!rc && reply)*reply=r;return rc;
}
int oss_set_cooling(oss_camera *c,oss_cooler cooler,oss_fan fan,oss_reply *reply) {
    if((cooler!=OSS_COOLER_OFF && cooler!=OSS_COOLER_ON) ||
       (fan!=OSS_FAN_LOW && fan!=OSS_FAN_HIGH)) return OSS_EINVAL;
    return oss_set_cooling_flags(c,(cooler==OSS_COOLER_ON ? 1u:0u) |
                                  (fan==OSS_FAN_HIGH ? 2u:0u),reply);
}
static int configure_full_frame(oss_camera *c,unsigned bin,oss_reply *r) {
    oss_image_geometry g;oss_reply reply;
    if(!can_set(c) || c->profile!=5) return OSS_ESTATE;
    if(oss_get_image_geometry(bin,&g)) return OSS_EINVAL;
    uint8_t p[4]={0,0,(uint8_t)(g.raw_height>>8),(uint8_t)g.raw_height};
    int rc=exchange(c,11,p,&reply);
    if(!rc) {c->expected_bytes=g.raw_bytes;if(r)*r=reply;}
    return rc;
}
int oss_set_vertical_window(oss_camera *c,unsigned y,unsigned height,oss_reply *r) {
    oss_reply reply;
    uint8_t p[4];
    if(!can_set(c) || c->profile!=5) return OSS_ESTATE;
    if(!height || height>1040 || y>1040-height) return OSS_EINVAL;
    p[0]=(uint8_t)(y>>8);p[1]=(uint8_t)y;
    p[2]=(uint8_t)((height+12)>>8);p[3]=(uint8_t)(height+12);
    int rc=exchange(c,11,p,&reply);
    if(!rc) {c->expected_bytes=(size_t)1434*(height+12)*2;if(r)*r=reply;}
    return rc;
}
