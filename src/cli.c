#define _POSIX_C_SOURCE 200809L
#include "openstarshoot.h"
#include "platform.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    FILE *trace,*tx,*rx,*image,*summary;
    size_t tx_offset,rx_offset,image_bytes;
    unsigned trace_count;
    int io_error;
    int first_error;
} output;
static volatile sig_atomic_t interrupted;
static void on_signal(int s) {(void)s;interrupted=1;}
static int cancelled(void *u) {(void)u;return interrupted!=0;}
static void trace_usb(void *user,uint8_t ep,const uint8_t *data,size_t requested,size_t actual,int rc) {
    output *o=user;FILE *f=ep&0x80 ? o->rx:o->tx;
    size_t *offset=ep&0x80 ? &o->rx_offset:&o->tx_offset;
    size_t saved=ep&0x80 ? actual:requested;
    if(fwrite(data,1,saved,f)!=saved || fflush(f))o->io_error=1;
    if(fprintf(o->trace,"%u\t%.9f\t%.9f\t%02x\t%zu\t%zu\t%d\t%zu\t%zu\n",
       ++o->trace_count,oss_realtime_seconds(),oss_monotonic_seconds(),
       ep,requested,actual,rc,*offset,saved)<0 || fflush(o->trace))o->io_error=1;
    *offset+=saved;
}
static int save_image(void *user,const uint8_t *data,size_t bytes) {
    output *o=user;
    if(fwrite(data,1,bytes,o->image)!=bytes || fflush(o->image)) {o->io_error=1;return -1;}
    o->image_bytes+=bytes;
    if(o->image_bytes/262144 != (o->image_bytes-bytes)/262144)
        printf("Readout: %zu bytes saved\n",o->image_bytes);
    return 0;
}
static FILE *new_file(const char *dir,const char *name) {
    char path[4096];
    if(snprintf(path,sizeof(path),"%s/%s",dir,name)>=(int)sizeof(path))return NULL;
    return oss_create_file(path);
}
static void report(output *o,const char *stage,int rc,const char *detail) {
    if(rc && !o->first_error)o->first_error=rc;
    printf("%s: %s%s%s\n",stage,oss_error(rc),detail&&*detail ? "; ":"",detail ? detail:"");
    if(fprintf(o->summary,"%s\t%d\t%s\n",stage,rc,detail ? detail:"")<0 || fflush(o->summary))o->io_error=1;
}
static int exit_reason(const output *o,int failed) {
    if(o->io_error)return OSS_EIO;
    if(o->first_error)return o->first_error;
    return failed ? (interrupted ? OSS_ECANCELLED:OSS_ESTATE):0;
}
static int parse_uint(const char *s,unsigned lo,unsigned hi,unsigned *out) {
    char *end;unsigned long n;
    errno=0;if(!s || !*s || *s=='-')return -1;
    n=strtoul(s,&end,10);
    if(errno || *end || n<lo || n>hi)return -1;
    *out=(unsigned)n;return 0;
}
static int parse_offset(const char *s,int *out) {
    char *end;long n;errno=0;n=strtol(s,&end,10);
    if(errno || !*s || *end || n< -512 || n>511)return -1;
    *out=(int)n;return 0;
}
static void usage(void) {
    puts("Usage: starshoot-diag info OUTPUT_DIR");
    puts("       starshoot-diag pass OUTPUT_DIR --covered --dark-ms 100");
    puts("       starshoot-diag capture OUTPUT_DIR --covered --dark-ms MILLISECONDS [--bin 1|2]");
    puts("       starshoot-diag capture OUTPUT_DIR --illuminated --exposure-ms MILLISECONDS [--bin 1|2]");
    puts("       capture also accepts --roi X Y WIDTH HEIGHT (sensor pixels; bin 2 requires even values; horizontal software crop)");
    puts("       starshoot-diag offset OUTPUT_DIR NEW_VALUE KNOWN_PRIOR_VALUE");
    puts("       starshoot-diag cooling OUTPUT_DIR NEW_MASK KNOWN_PRIOR_MASK");
    puts("       starshoot-diag cooling-set OUTPUT_DIR on|off high|low");
    puts("cooling-set writes both controls and leaves the requested settings in place.");
    puts("Output directory must not exist. pass is one routine-check and dark-frame attempt.");
    puts("Offsets are -512..511; cooling masks are 0..3. offset/cooling restore the supplied prior value.");
    puts("No reset, factory, arbitrary-command or firmware operation is available.");
}
int main(int argc,char **argv) {
    oss_camera *camera=NULL;output o={0};uint8_t cap=0;oss_status status={0};
    int rc=0,failed=0,is_pass=0,is_capture=0,is_info=0,is_offset=0,is_cooling=0,is_cooling_set=0,is_light=0;
    int offset=0,prior_offset=0;unsigned dark_ms=0,mask=0,prior_mask=0,bin=1;
    oss_cooler cooler=OSS_COOLER_OFF;oss_fan fan=OSS_FAN_LOW;
    char detail[512];oss_capture_result frame={0};oss_image_geometry geometry={0};
    oss_roi roi={0,0,1392,1040};int has_roi=0,has_bin=0;
    oss_set_console_buffering();
    if(argc<3){usage();return 2;}
    is_pass=!strcmp(argv[1],"pass");is_capture=!strcmp(argv[1],"capture");
    is_info=!strcmp(argv[1],"info");is_offset=!strcmp(argv[1],"offset");is_cooling=!strcmp(argv[1],"cooling");
    is_cooling_set=!strcmp(argv[1],"cooling-set");
    if(is_pass || is_capture) {
        if(argc<6) {usage();return 2;}
        if(is_capture && !strcmp(argv[3],"--illuminated") && !strcmp(argv[4],"--exposure-ms"))is_light=1;
        if((!is_light && (strcmp(argv[3],"--covered") || strcmp(argv[4],"--dark-ms"))) ||
           parse_uint(argv[5],1,is_pass ? 1000:60000,&dark_ms) || (is_pass && argc!=6)) {usage();return 2;}
        for(int i=6;i<argc;) {
            if(!strcmp(argv[i],"--bin") && !has_bin && i+1<argc) {
                if(parse_uint(argv[i+1],1,2,&bin)) {usage();return 2;}
                has_bin=1;i+=2;
            } else if(!strcmp(argv[i],"--roi") && !has_roi && i+4<argc) {
                if(parse_uint(argv[i+1],0,1391,&roi.x) || parse_uint(argv[i+2],0,1039,&roi.y) ||
                   parse_uint(argv[i+3],1,1392,&roi.width) || parse_uint(argv[i+4],1,1040,&roi.height)) {usage();return 2;}
                has_roi=1;i+=5;
            } else {usage();return 2;}
        }
        if(has_roi) {
            oss_roi_geometry g;
            if(oss_get_roi_geometry_bin(bin,&roi,&g)) {fputs("ROI is outside the active sensor or not aligned to binning\n",stderr);return 2;}
            geometry=(oss_image_geometry){g.raw_width,g.raw_height,g.active_width,g.active_height,g.raw_bytes};
        } else if(oss_get_image_geometry(bin,&geometry)) {usage();return 2;}
    } else if(is_offset) {
        if(argc!=5 || parse_offset(argv[3],&offset) || parse_offset(argv[4],&prior_offset)) {usage();return 2;}
    } else if(is_cooling_set) {
        if(argc!=5 || (strcmp(argv[3],"on") && strcmp(argv[3],"off")) ||
           (strcmp(argv[4],"high") && strcmp(argv[4],"low"))) {usage();return 2;}
        cooler=!strcmp(argv[3],"on") ? OSS_COOLER_ON:OSS_COOLER_OFF;
        fan=!strcmp(argv[4],"high") ? OSS_FAN_HIGH:OSS_FAN_LOW;
    } else if(is_cooling) {
        if(argc!=5 || parse_uint(argv[3],0,3,&mask) || parse_uint(argv[4],0,3,&prior_mask)) {usage();return 2;}
    } else if(!is_info || argc!=3) {usage();return 2;}
    /* mkdir is the one-pass guard. A failed/partial run is never overwritten. */
    if(oss_make_directory(argv[2])) {perror("Create new output directory");return 2;}
    o.trace=new_file(argv[2],"usb-transfers.tsv");o.tx=new_file(argv[2],"usb-out-requests.bin");
    o.rx=new_file(argv[2],"usb-in.bin");o.summary=new_file(argv[2],"stages.tsv");
    if(!o.trace||!o.tx||!o.rx||!o.summary) {perror("Open evidence files");failed=1;goto finish;}
    fprintf(o.trace,"sequence\tend_unix_seconds\tend_monotonic_seconds\tendpoint_hex\trequested\tactual\tresult\tstream_offset\tstored_bytes\n");
    if(is_pass||is_capture) {
        o.image=new_file(argv[2],is_light ? "light.raw":"dark.raw");if(!o.image){perror("Open raw image");failed=1;goto finish;}
    }

    if(is_pass||is_capture) {
        FILE *meta=new_file(argv[2],"geometry.json");
        if(!meta){perror("Open geometry metadata");failed=1;goto finish;}
        if(fprintf(meta,"{\"bin\":%u,\"roi\":[%u,%u,%u,%u],\"raw_width\":%u,\"raw_height\":%u,\"raw_bytes\":%zu}\n",bin,roi.x,roi.y,roi.width,roi.height,geometry.raw_width,geometry.raw_height,geometry.raw_bytes)<0)o.io_error=1;
        if(fclose(meta))o.io_error=1;
    }
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);
    puts("Opening the existing USB configuration and claiming interface 0");
    oss_open_options options={.trace=trace_usb,.trace_user=&o};
    rc=oss_open(&camera,&options);report(&o,"open",rc,"");
    if(rc) {failed=1;goto finish;}
    puts("Initialization: profile, capability, status; preserving offset and cooling");
    rc=oss_initialize(camera,&cap,&status);
    snprintf(detail,sizeof(detail),"profile=5 capability=%u state=%u auxiliary=%u; offset/cooling unchanged",cap,status.state,status.auxiliary);
    report(&o,"initialize",rc,rc ? "Initialization incomplete; no exposure attempted":detail);
    if(rc||o.io_error) {failed=1;goto cleanup;}
    if(is_info||is_pass) {
        uint16_t a=0,b=0;
        rc=oss_get_counter(camera,&a,NULL);report(&o,"counter-before",rc,"");
        if(!rc) {oss_sleep_ms(100);rc=oss_get_counter(camera,&b,NULL);}
        snprintf(detail,sizeof(detail),"before=%u after=%u modulo-delta=%u; nominal 100 ms interval",a,b,(uint16_t)(b-a));
        report(&o,"counter-after",rc,detail);
        if(rc||o.io_error) {failed=1;goto cleanup;}
        report(&o,"offset-test-skipped",0,"Prior value unreadable; preserved unchanged");
        report(&o,"cooling-test-skipped",0,"Prior value unreadable; preserved unchanged");
    }
    if(is_cooling_set) {
        snprintf(detail,sizeof(detail),"requested cooler=%s fan=%s; no setting readback or physical confirmation",argv[3],argv[4]);
        puts("Setting cooler and fan together; no automatic restoration");
        rc=oss_set_cooling(camera,cooler,fan,NULL);
        report(&o,"cooling-set",rc,detail);failed|=rc!=0;
        goto cleanup;
    }
    if(is_offset||is_cooling) {
        rc=is_offset ? oss_set_offset(camera,offset,NULL):oss_set_cooling_flags(camera,mask,NULL);
        report(&o,"set-known-setting",rc,"");
        failed|=rc!=0;
        int restore=is_offset ? oss_set_offset(camera,prior_offset,NULL):oss_set_cooling_flags(camera,prior_mask,NULL);
        report(&o,"restore-known-setting",restore,"Uses caller-provided prior value, not device readback");
        failed|=restore!=0;
        goto cleanup;
    }
    if(is_info) goto cleanup;
    if(interrupted||o.io_error) {failed=1;goto cleanup;}
    oss_exposure e={.exposure_ms=dark_ms,.bin=bin,.readout_speed=5};
    uint8_t encoded[6];
    rc=oss_encode_exposure(&e,encoded);
    if(rc) {report(&o,"exposure-parameters",rc,"");failed=1;goto cleanup;}
    snprintf(detail,sizeof(detail),"requested_ms=%u bin=%u readout_speed=5 flags=%u mode=%u; window=%u,%u raw rows; active=%ux%u; duration units provisional",dark_ms,bin,encoded[2],encoded[5],has_roi ? roi.y:0,geometry.raw_height,geometry.active_width,geometry.active_height);
    printf("Starting the only %s exposure: %u ms requested\n",is_light ? "illuminated":"dark",dark_ms);
    rc=has_roi ? oss_start_exposure_roi(camera,&e,&roi,NULL):oss_start_exposure(camera,&e,NULL);report(&o,"exposure-start",rc,detail);
    if(rc) {failed=1;goto cleanup;}
    puts("Waiting for acquisition state to leave 1, with a bounded deadline");
    rc=oss_wait_ready(camera,dark_ms+5000,cancelled,NULL);report(&o,"exposure-wait",rc,"");
    if(rc||o.io_error) {failed=1;goto cleanup;}
    puts("Starting the only readout; saving every received byte");
    rc=oss_capture_raw(camera,save_image,&o,30000,cancelled,NULL,&frame);
    snprintf(detail,sizeof(detail),"image_bytes=%zu expected=%zu wire_bytes=%zu read_ahead=%zu transfers=%u short_seen=%d; completion by sample count",frame.bytes,frame.expected_bytes,frame.wire_bytes,frame.read_ahead_bytes,frame.transfers,frame.end_short_packet);
    report(&o,"raw-capture",rc,detail);
    FILE *tail=new_file(argv[2],"read-ahead.bin");
    if(!tail) {o.io_error=1;perror("Open read-ahead evidence");}
    else {
        if(fwrite(frame.read_ahead,1,frame.read_ahead_bytes,tail)!=frame.read_ahead_bytes)o.io_error=1;
        if(fclose(tail))o.io_error=1;
    }
    if(rc || frame.bytes!=geometry.raw_bytes) {
        failed=1;
        if(!rc)report(&o,"frame-shape-incomplete",OSS_EFRAME,"Configured sample budget not met; raw bytes retained");
    }
cleanup:
    /* One command-05 check doubles as normal acquisition cleanup. Do not
     * transmit after a desynchronized/partial image transport failure. */
    if(camera && (is_pass||is_capture) && oss_get_phase(camera)!=OSS_UNCERTAIN) {
        rc=oss_stop(camera,NULL);report(&o,"stop-05",rc,"One stop command; no retry");failed|=rc!=0;
        if(!rc) {
            rc=oss_get_status(camera,&status,NULL);
            if(!rc && status.state!=0 && status.state!=2)rc=OSS_ESTATE;
            snprintf(detail,sizeof(detail),"state=%u auxiliary=%u",status.state,status.auxiliary);
            report(&o,"final-status",rc,detail);failed|=rc!=0 || status.state==1;
        }
    } else if(camera && oss_get_phase(camera)==OSS_UNCERTAIN) {
        report(&o,"cleanup-stop-skipped",OSS_ESTATE,"Transport uncertain; no further command, no reset; camera state not verified");
        failed=1;
    }
finish:
    oss_close(camera);
    int final_rc=exit_reason(&o,failed);
    if(o.summary)report(&o,"finished",final_rc,
        is_cooling_set ? "Both cooling controls requested; no automatic restoration; see stages":
        is_offset||is_cooling ? "Explicit setting check and restore attempted; see stages":
        "Offset/cooling unchanged; no reset/factory/firmware commands");
    FILE *files[]={o.image,o.trace,o.tx,o.rx,o.summary};
    for(size_t k=0;k<sizeof(files)/sizeof(files[0]);k++)if(files[k] && fclose(files[k]))o.io_error=1;
    printf("Evidence saved in %s; result: %s\n",argv[2],failed||o.io_error ? "failure or incomplete":"completed");
    return failed||o.io_error ? 1:0;
}
