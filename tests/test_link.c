#include "openstarshoot.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const oss_exposure exposure={100,1,5};
    const unsigned char expected[]={0xa5,3,6,0,100,3};
    unsigned char request[6];
    oss_image_geometry geometry;
    if(oss_encode_exposure(&exposure,request) || memcmp(request,expected,sizeof(request)) ||
       oss_get_image_geometry(1,&geometry) || geometry.raw_bytes!=3017136 ||
       oss_get_phase(NULL)!=OSS_UNCERTAIN || strcmp(oss_error(OSS_OK),"success"))return 1;
    puts("PASS: public header and linked library; no camera opened");
    return 0;
}
