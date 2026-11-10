#define oss_open mocked_open
#define oss_close mocked_close
#define main cli_main
#include "../src/cli.c"
#undef main
#include "internal.h"
#include <assert.h>
static oss_camera c;
static unsigned op,state;
static size_t wire;
static unsigned bin=1;
static int mocked_transfer(void *u,uint8_t ep,uint8_t *b,int n,int *actual,unsigned timeout){
 (void)u;assert(timeout);
 if(ep==8){assert(n==6&&b[0]==0xa5);op=b[1];if(op==11)assert(!memcmp(b,(bin==2?(uint8_t[]){0xa5,11,0,51,1,12}:(uint8_t[]){0xa5,11,0,101,2,12}),6));if(op==3)state=2;if(op==5)state=0;*actual=6;return 0;}
 assert(ep==0x82);
 if(n==8){memset(b,0,8);b[0]=0xa5;b[2]=(uint8_t)op;b[4]=op==1?5:op==7?128:op==2?state:1;*actual=8;return 0;}
 assert(n==1024);size_t count=(bin==2?768624:1502832)-wire;if(count>1024)count=1024;assert(count);
 for(size_t i=0;i<count;i++){size_t offset=wire+i,word=offset/2;uint16_t v=(uint16_t)(((word/1434+101)*137+(word%1434)*31)^0xabcd);b[i]=(uint8_t)(offset&1?v>>8:v);}
 wire+=count;*actual=(int)count;return 0;
}
int mocked_open(oss_camera **out,const oss_open_options *o){memset(&c,0,sizeof(c));c.synchronized=1;c.test_transfer=mocked_transfer;c.trace=o->trace;c.trace_user=o->trace_user;*out=&c;return 0;}
void mocked_close(oss_camera *camera){assert(camera==&c&&c.phase==OSS_IDLE);}
int main(int argc,char **argv){assert(argc==2||argc==3);if(argc==3)bin=2;char *a[]={"starshoot-diag","capture",argv[1],"--illuminated","--exposure-ms","100","--roi",bin==2?"74":"73",bin==2?"102":"101","512","512","--bin",bin==2?"2":"1",NULL};return cli_main(13,a);}
