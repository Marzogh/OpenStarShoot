#include "platform.h"
#include "assertions.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

int main(int argc,char **argv) {
    char path[4096];
    const unsigned char bytes[]={0,10,13,26,128,255};
    unsigned char readback[sizeof(bytes)];
    assert(argc==2);
    setvbuf(stdout,NULL,_IOLBF,0);
    puts("Platform: monotonic clock, wall clock and bounded sleep");
    uint64_t before=oss_monotonic_ms();
    double seconds=oss_monotonic_seconds();
    oss_sleep_ms(20);
    assert(oss_monotonic_ms()>=before+10);
    assert(oss_monotonic_seconds()>=seconds);
    double wall=oss_realtime_seconds(),difference=wall-(double)time(NULL);
    assert(difference>-2.0 && difference<2.0);
    puts("Platform: new directory, exclusive binary file and exact byte preservation");
    assert(snprintf(path,sizeof(path),"%s/output",argv[1])<(int)sizeof(path));
    assert(!oss_make_directory(path));assert(oss_make_directory(path)!=0);
    assert(snprintf(path,sizeof(path),"%s/output/binary.dat",argv[1])<(int)sizeof(path));
    FILE *file=oss_create_file(path);assert(file);
    assert(fwrite(bytes,1,sizeof(bytes),file)==sizeof(bytes));assert(!fclose(file));
    assert(!oss_create_file(path));
    file=fopen(path,"rb");assert(file);
    assert(fread(readback,1,sizeof(readback),file)==sizeof(readback));
    assert(fgetc(file)==EOF);assert(!fclose(file));
    assert(!memcmp(bytes,readback,sizeof(bytes)));
    puts("PASS: platform timing, exclusive creation and binary output; no camera opened");
    return 0;
}
