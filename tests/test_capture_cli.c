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

typedef struct {unsigned bin,opcode,state,reads;size_t wire;} fixture;
static fixture f;
static oss_camera camera;
static unsigned opens;
static int fake(void *u,uint8_t ep,uint8_t *b,int n,int *actual,unsigned timeout) {
    fixture *s=u;assert(timeout);
    if(ep==8) {
        assert(n==6 && b[0]==0xa5);s->opcode=b[1];
        if(b[1]==11)assert(!memcmp(b,s->bin==2 ? (uint8_t[]){0xa5,11,0,0,2,0x14}:(uint8_t[]){0xa5,11,0,0,4,0x1c},6));
        if(b[1]==3){assert(!memcmp(b,(uint8_t[]){0xa5,3,6,0,100,s->bin==2 ? 7:3},6));s->state=2;}
        if(b[1]==5)s->state=0;
        assert(b[1]==1 || b[1]==2 || b[1]==3 || b[1]==4 || b[1]==5 || b[1]==7 || b[1]==11);
        *actual=6;return 0;
    }
    assert(ep==0x82);
    if(n==8){memset(b,0,8);b[0]=0xa5;b[2]=(uint8_t)s->opcode;b[4]=s->opcode==1 ? 5:s->opcode==2 ? (uint8_t)s->state:s->opcode==7 ? 128:1;*actual=8;return 0;}
    assert(n==1024);size_t total=s->bin==2 ? 1525776u:3017136u;assert(s->wire<total);
    size_t count=total-s->wire;if(count>1024)count=1024;
    for(size_t k=0;k<count;k++)b[k]=(uint8_t)((s->wire+k)%251+1);
    s->wire+=count;s->reads++;*actual=(int)count;return 0;
}
int mocked_open(oss_camera **out,const oss_open_options *o){
    opens++;memset(&camera,0,sizeof(camera));camera.synchronized=1;camera.phase=OSS_IDLE;
    camera.trace=o->trace;camera.trace_user=o->trace_user;camera.test_transfer=fake;camera.test_user=&f;*out=&camera;return 0;
}
void mocked_close(oss_camera *c){if(c)assert(c==&camera && c->phase==OSS_IDLE && f.state==0);}
int main(int argc,char **arguments) {
    assert(argc==2);
    const char *base=arguments[1];
    const char *names[]={"light.raw","usb-transfers.tsv","usb-out-requests.bin","usb-in.bin","stages.tsv","read-ahead.bin","geometry.json"};
    for(unsigned bin=1;bin<=2;bin++) {
        char dir[128],path[256];snprintf(dir,sizeof(dir),"%s/bin%u",base,bin);f=(fixture){.bin=bin};
        char *legacy[]={"starshoot-diag","capture",dir,"--illuminated","--exposure-ms","100",NULL};
        char *binned[]={"starshoot-diag","capture",dir,"--illuminated","--exposure-ms","100","--bin","2",NULL};
        printf("CLI: normal %s full-frame capture through simulated transport\n",bin==1 ? "legacy bin-1":"explicit bin-2");
        assert(!cli_program_main(bin==1 ? 6:8,bin==1 ? legacy:binned));
        snprintf(path,sizeof(path),"%s/light.raw",dir);struct stat st;assert(!stat(path,&st));
        assert((size_t)st.st_size==(bin==2 ? 1525776u:3017136u));assert(f.reads==(bin==2 ? 1491u:2947u));
        for(size_t k=0;k<sizeof(names)/sizeof(names[0]);k++){assert(snprintf(path,sizeof(path),"%s/%s",dir,names[k])<(int)sizeof(path));assert(!unlink(path));}
        assert(!rmdir(dir));
    }
    for(unsigned reverse=0;reverse<2;reverse++) {
        char dir[128];snprintf(dir,sizeof(dir),"%s/unsupported%u",base,reverse);
        char *combined[]={"starshoot-diag","capture",dir,"--illuminated","--exposure-ms","100","--bin","2","--roi","73","101","512","512",NULL};
        char *reversed[]={"starshoot-diag","capture",dir,"--illuminated","--exposure-ms","100","--roi","73","101","512","512","--bin","2",NULL};
        unsigned before=opens;assert(cli_program_main(13,reverse ? reversed:combined)==2);assert(opens==before);
        struct stat st;assert(stat(dir,&st) && errno==ENOENT);
    }
    puts("PASS: unaligned bin-2 ROI rejected in either option order before output creation or USB open");
    puts("CLI binning workflow passed. All USB responses were simulated; no camera opened.");return 0;
}
