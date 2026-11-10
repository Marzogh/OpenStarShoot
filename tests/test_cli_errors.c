#define main cli_program_main
#include "../src/cli.c"
#undef main
#include "assertions.h"
int main(void) {
    output o={0};o.summary=tmpfile();assert(o.summary);
    puts("CLI errors: capture limit survives successful cleanup");
    report(&o,"raw-capture",OSS_ELIMIT,"simulated limit, not a file error");
    report(&o,"stop-05",0,"");
    assert(exit_reason(&o,1)==OSS_ELIMIT);
    puts("CLI errors: transport failure retains its libusb code");
    o.first_error=0;report(&o,"exposure-start",-7,"simulated USB timeout");
    assert(exit_reason(&o,1)==-7);
    puts("CLI errors: only actual evidence I/O failure is reported as OSS_EIO");
    o.io_error=1;assert(exit_reason(&o,1)==OSS_EIO);
    o.io_error=0;o.first_error=0;assert(exit_reason(&o,0)==0);
    fclose(o.summary);puts("All CLI error-classification checks passed. No camera opened.");
    return 0;
}
