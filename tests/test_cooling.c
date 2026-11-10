#define oss_open mocked_open
#define oss_close mocked_close
#define main cli_program_main
#include "../src/cli.c"
#undef main
#undef oss_open
#undef oss_close
#include "internal.h"
#include "assertions.h"
#include "test_paths.h"

typedef struct {unsigned calls,opcode,mask,writes;int fault;uint8_t result;} fixture;
static fixture f;
static oss_camera camera;
static unsigned opens;
static int fake(void *u,uint8_t ep,uint8_t *b,int n,int *actual,unsigned timeout) {
    fixture *s=u;s->calls++;assert(timeout==4000);
    if(ep==8) {
        assert(n==6 && b[0]==0xa5);s->opcode=b[1];
        assert(b[1]==1 || b[1]==2 || b[1]==7 || b[1]==12);
        if(b[1]==12) {
            assert(!memcmp(b,(uint8_t[]){0xa5,12,(uint8_t)s->mask,0,0,0},6));s->writes++;
        }
        *actual=s->fault==1 ? 3:6;return 0;
    }
    assert(ep==0x82 && n==8);
    memcpy(b,(uint8_t[]){0xa5,0x99,(uint8_t)s->opcode,0x77,s->result,0x88,0x55,0x66},8);
    if(s->opcode==1)b[4]=5;
    if(s->opcode==2)b[4]=0;
    if(s->fault==2)b[2]=0x7f;
    if(s->fault==3)b[0]=0;
    *actual=s->fault==4 ? 7:8;
    return s->fault==5 ? LIBUSB_ERROR_TIMEOUT:0;
}
static oss_camera fresh(void) {
    oss_camera c={0};c.synchronized=1;c.initialized=1;c.profile=5;c.phase=OSS_IDLE;
    c.test_transfer=fake;c.test_user=&f;return c;
}
int mocked_open(oss_camera **out,const oss_open_options *o) {
    opens++;camera=fresh();camera.initialized=0;
    camera.trace=o->trace;camera.trace_user=o->trace_user;*out=&camera;return 0;
}
void mocked_close(oss_camera *c) {(void)c;}
int main(int argc,char **arguments) {
    assert(argc==2);
    const char *base=arguments[1];
    puts("Cooling encoding: all cooler/fan pairs, including result bytes with no defined success meaning");
    for(unsigned mask=0;mask<4;mask++)for(unsigned k=0;k<3;k++) {
        f=(fixture){.mask=mask,.result=(uint8_t[]){0,1,255}[k]};oss_camera c=fresh();oss_reply r;
        assert(!oss_set_cooling(&c,mask&1 ? OSS_COOLER_ON:OSS_COOLER_OFF,mask&2 ? OSS_FAN_HIGH:OSS_FAN_LOW,&r));
        assert(f.calls==2 && f.writes==1 && r.bytes[4]==f.result && c.phase==OSS_IDLE);
    }
    puts("Cooling guards: invalid enum/mask and non-idle/uninitialized state issue no USB transfer");
    f=(fixture){0};oss_camera c=fresh();
    assert(oss_set_cooling(&c,(oss_cooler)2,OSS_FAN_HIGH,NULL)==OSS_EINVAL);
    assert(oss_set_cooling(&c,OSS_COOLER_OFF,(oss_fan)-1,NULL)==OSS_EINVAL);
    assert(oss_set_cooling_flags(&c,4,NULL)==OSS_EINVAL);
    for(int phase=OSS_EXPOSING;phase<=OSS_UNCERTAIN;phase++) {
        c.phase=(oss_phase)phase;assert(oss_set_cooling(&c,OSS_COOLER_OFF,OSS_FAN_HIGH,NULL)==OSS_ESTATE);
    }
    c.phase=OSS_IDLE;c.initialized=0;assert(oss_set_cooling(&c,OSS_COOLER_ON,OSS_FAN_LOW,NULL)==OSS_ESTATE);
    assert(f.calls==0);
    puts("Cooling replies: short OUT, wrong opcode/marker, short IN and timeout stop without retry");
    for(int fault=1;fault<=5;fault++) {
        f=(fixture){.fault=fault};c=fresh();oss_reply r;memset(&r,0x5a,sizeof(r));
        assert(oss_set_cooling(&c,OSS_COOLER_OFF,OSS_FAN_LOW,&r)==(fault==5 ? LIBUSB_ERROR_TIMEOUT:OSS_EPROTO));
        assert(c.phase==OSS_UNCERTAIN && !c.synchronized && r.bytes[0]==0x5a);
        unsigned before=f.calls;assert(oss_set_cooling(&c,OSS_COOLER_OFF,OSS_FAN_LOW,NULL)==OSS_ESTATE);assert(f.calls==before);
    }
    puts("Cooling CLI: named pairs write once and leave the requested state; no exposure, stop or implicit restore");
    const char *files[]={"usb-transfers.tsv","usb-out-requests.bin","usb-in.bin","stages.tsv"};
    for(unsigned mask=0;mask<4;mask++) {
        char dir[256],path[512];snprintf(dir,sizeof(dir),"%s/state%u",base,mask);
        f=(fixture){.mask=mask};
        char *argv[]={"starshoot-diag","cooling-set",dir,mask&1 ? "on":"off",mask&2 ? "high":"low",NULL};
        assert(!cli_program_main(5,argv));assert(f.writes==1 && f.calls==8);
        for(unsigned k=0;k<4;k++){snprintf(path,sizeof(path),"%s/%s",dir,files[k]);assert(!unlink(path));}
        assert(!rmdir(dir));
    }
    char dir[256];snprintf(dir,sizeof(dir),"%s/invalid",base);
    char *bad[]={"starshoot-diag","cooling-set",dir,"on","off",NULL};
    unsigned before=opens;assert(cli_program_main(5,bad)==2);assert(opens==before);
    struct stat st;assert(stat(dir,&st) && errno==ENOENT);
    puts("PASS: cooling library and normal CLI checks; all USB responses simulated, no camera opened.");return 0;
}
