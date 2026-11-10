#include "indiccd.h"
#include "indilogger.h"
#include "openstarshoot.h"
#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr unsigned WIDTH=1392,HEIGHT=1040;
using Clock=std::chrono::steady_clock;
struct Evidence {
    std::ofstream raw,rx,tx,index,metadata;
    size_t raw_bytes=0,rx_bytes=0,tx_bytes=0;unsigned sequence=0;bool bad=false;
    std::string directory;
    bool open(unsigned ms,unsigned bin,const oss_roi &roi,unsigned raw_height,unsigned active_width,unsigned active_height) {
        const char *custom=std::getenv("OPENSTARSHOOT_RAW_DIRECTORY");
        const char *state=std::getenv("XDG_STATE_HOME"),*home=std::getenv("HOME");
        std::filesystem::path root;
        if(custom && *custom)root=custom;
        else if(state && *state)root=std::filesystem::path(state)/"openstarshoot";
        else if(home && *home)root=std::filesystem::path(home)/".local/state/openstarshoot";
        else return false;
        std::error_code ec;std::filesystem::create_directories(root,ec);if(ec)return false;
        std::string path=(root/"frame-XXXXXX").string();std::vector<char> tmp(path.begin(),path.end());tmp.push_back(0);
        if(!mkdtemp(tmp.data()))return false;
        directory=tmp.data();
        raw.open(directory+"/frame.raw",std::ios::binary);rx.open(directory+"/usb-in.bin",std::ios::binary);
        tx.open(directory+"/usb-out-requests.bin",std::ios::binary);index.open(directory+"/usb-transfers.tsv");metadata.open(directory+"/capture.txt");
        index<<"sequence\tend_unix_seconds\tend_monotonic_seconds\tendpoint_hex\trequested\tactual\tresult\tstream_offset\tstored_bytes\n";
        metadata<<"requested_ms="<<ms<<"\nraw_width=1434\nraw_height="<<raw_height<<"\nactive_width="<<active_width<<"\nactive_height="<<active_height<<"\nroi_x="<<roi.x<<"\nroi_y="<<roi.y<<"\nroi_width="<<roi.width<<"\nroi_height="<<roi.height<<"\nbin="<<bin<<"\nreadout_speed=5\n";
        return good();
    }
    bool good(){return !bad && raw.good() && rx.good() && tx.good() && index.good() && metadata.good();}
    static void trace(void *user,uint8_t ep,const uint8_t *data,size_t requested,size_t actual,int rc) {
        auto *e=static_cast<Evidence *>(user);if(!e->raw.is_open())return;
        size_t saved=(ep&0x80)?actual:requested;auto &stream=(ep&0x80)?e->rx:e->tx;auto &offset=(ep&0x80)?e->rx_bytes:e->tx_bytes;
        stream.write(reinterpret_cast<const char *>(data),saved);
        e->index<<++e->sequence<<'\t'<<std::fixed<<std::setprecision(9)<<std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()<<'\t'<<std::chrono::duration<double>(Clock::now().time_since_epoch()).count()<<'\t'<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(ep)<<std::dec<<'\t'<<requested<<'\t'<<actual<<'\t'<<rc<<'\t'<<offset<<'\t'<<saved<<'\n';offset+=saved;
        if(!stream || !e->index)e->bad=true;
    }
    static int image(void *user,const uint8_t *data,size_t bytes) {
        auto *e=static_cast<Evidence *>(user);e->raw.write(reinterpret_cast<const char *>(data),bytes);e->raw_bytes+=bytes;
        if(!e->raw){e->bad=true;return -1;}return 0;
    }
    bool close(int result,int final_state) {
        if(!raw.is_open())return true;
        metadata<<"result="<<result<<"\nfinal_state="<<final_state<<"\nraw_bytes="<<raw_bytes<<'\n';
        for(auto *s:{&raw,&rx,&tx,&index,&metadata}){s->flush();if(!*s)bad=true;s->close();if(s->fail())bad=true;}
        return !bad;
    }
    void reset(){raw_bytes=rx_bytes=tx_bytes=0;sequence=0;bad=false;raw.clear();rx.clear();tx.clear();index.clear();metadata.clear();}
};
}

class OpenStarShootCCD final:public INDI::CCD {
public:
    OpenStarShootCCD(){setVersion(0,1);}
    ~OpenStarShootCCD() override {evidence.close(OSS_ECANCELLED,-1);oss_close(camera);}
    const char *getDefaultName() override {return "OpenStarShoot Mono III";}
    bool initProperties() override {
        INDI::CCD::initProperties();SetCCDCapability(CCD_CAN_BIN | CCD_CAN_SUBFRAME);
        PrimaryCCD.setMinMaxStep("CCD_BINNING","HOR_BIN",1,2,1,false);
        PrimaryCCD.setMinMaxStep("CCD_BINNING","VER_BIN",1,2,1,false);
        FastExposureToggleSP.p=IP_RO;FastExposureCountNP.p=IP_RO;
        setDefaultPollingPeriod(100);setPollingPeriodRange(100,100);
        addCaptureFormat({"MONO16","Mono 16",16,true,true});
        PrimaryCCD.setMinMaxStep("CCD_EXPOSURE","CCD_EXPOSURE_VALUE",0.001,60,0.001,false);
        return true;
    }
    bool ISNewSwitch(const char *dev,const char *name,ISState *states,char *names[],int n) override {
        if(!std::strcmp(name,"CCD_FAST_TOGGLE")){LOG_WARN("Fast exposure sequencing is not supported.");return false;}
        return INDI::CCD::ISNewSwitch(dev,name,states,names,n);
    }
    bool ISNewNumber(const char *dev,const char *name,double values[],char *names[],int n) override {
        if(!std::strcmp(name,"CCD_FAST_COUNT"))return false;
        return INDI::CCD::ISNewNumber(dev,name,values,names,n);
    }
    bool Connect() override {
        if(camera)return false;
        oss_open_options options={};options.trace=Evidence::trace;options.trace_user=&evidence;
        int rc=oss_open(&camera,&options);uint8_t capability=0;oss_status status={};
        if(!rc)rc=oss_initialize(camera,&capability,&status);
        if(rc || status.state!=0){LOGF_ERROR("Camera initialization failed: %s",oss_error(rc?rc:OSS_ESTATE));oss_close(camera);camera=nullptr;return false;}
        SetCCDParams(WIDTH,HEIGHT,16,6.45f,6.45f);PrimaryCCD.setFrameBufferSize(WIDTH*HEIGHT*2);
        PrimaryCCD.setBin(1,1);PrimaryCCD.setFrame(0,0,WIDTH,HEIGHT);
        fault=false;LOG_INFO("Connected in full-frame bin-1 mode; offset and cooling unchanged.");return true;
    }
    bool Disconnect() override {
        if(InExposure || uploading){LOG_WARN("Wait for acquisition completion before disconnecting; active abort is unverified.");return false;}
        if(timer_id>=0){RemoveTimer(timer_id);timer_id=-1;}
        oss_close(camera);camera=nullptr;return true;
    }
    bool updateProperties() override {
        INDI::CCD::updateProperties();if(isConnected() && timer_id<0)timer_id=SetTimer(100);return true;
    }
    bool UpdateCCDFrame(int x,int y,int w,int h) override {
        if(InExposure || uploading || x<0 || y<0 || w<=0 || h<=0)return false;
        oss_roi roi={unsigned(x),unsigned(y),unsigned(w),unsigned(h)};oss_roi_geometry geometry={};
        if(oss_get_roi_geometry_bin(PrimaryCCD.getBinX(),&roi,&geometry))return false;
        PrimaryCCD.setFrame(x,y,w,h);return true;
    }
    bool UpdateCCDBin(int x,int y) override {
        if(InExposure || uploading || x!=y || (x!=1 && x!=2))return false;
        oss_roi selected=frame();oss_roi_geometry geometry={};
        if(oss_get_roi_geometry_bin(unsigned(x),&selected,&geometry))return false;
        PrimaryCCD.setBin(x,y);return true;
    }
    void UploadComplete(INDI::CCDChip *) override {uploading=false;}
    bool AbortExposure() override {LOG_WARN("Active abort is not validated and is not advertised.");return false;}
    bool StartExposure(float duration) override {
        if(!camera || InExposure || uploading || fault || !std::isfinite(duration) || duration<0.001f || duration>60.0f)return false;
        unsigned ms=unsigned(std::lround(double(duration)*1000));if(ms>=10000)ms=(ms/100)*100;
        capture_roi=frame();capture_bin=PrimaryCCD.getBinX();
        if(capture_bin!=unsigned(PrimaryCCD.getBinY()) || (capture_bin!=1 && capture_bin!=2))return false;
        if(fullFrame(capture_roi)){
            oss_image_geometry g={};if(oss_get_image_geometry(capture_bin,&g))return false;
            raw_bytes=g.raw_bytes;raw_height=g.raw_height;active_width=g.active_width;active_height=g.active_height;
        }else{
            oss_roi_geometry g={};if(oss_get_roi_geometry_bin(capture_bin,&capture_roi,&g))return false;
            raw_bytes=g.raw_bytes;raw_height=g.raw_height;active_width=g.active_width;active_height=g.active_height;
        }
        evidence.reset();if(!evidence.open(ms,capture_bin,capture_roi,raw_height,active_width,active_height)){LOG_ERROR("Cannot create raw acquisition evidence; no exposure started.");evidence.close(OSS_EIO,-1);return false;}
        oss_exposure exposure={ms,capture_bin,5};int rc=fullFrame(capture_roi)?oss_start_exposure(camera,&exposure,nullptr):oss_start_exposure_roi(camera,&exposure,&capture_roi,nullptr);
        if(rc || !evidence.good()){LOGF_ERROR("Exposure start failed: %s",oss_error(rc?rc:OSS_EIO));evidence.close(rc?rc:OSS_EIO,-1);fault=true;return false;}
        PrimaryCCD.setExposureDuration(double(ms)/1000);InExposure=true;
        deadline=Clock::now()+std::chrono::milliseconds(ms+5000);
        LOGF_INFO("Exposure started; raw evidence: %s",evidence.directory.c_str());return true;
    }
    void TimerHit() override {
        timer_id=-1;
        if(!isConnected())return;
        if(InExposure){
            oss_status status={};int rc=oss_get_status(camera,&status,nullptr);
            if(rc || !evidence.good() || (status.state!=0 && status.state!=1 && status.state!=2))fail(rc?rc:OSS_EPROTO);
            else if(status.state==0 || status.state==2)readImage();
            else if(Clock::now()>=deadline)fail(OSS_ELIMIT);
            else PrimaryCCD.setExposureLeft(std::max(0.0,std::chrono::duration<double>(deadline-Clock::now()).count()-5));
        }
        timer_id=SetTimer(100);
    }
private:
    static bool fullFrame(const oss_roi &r){return r.x==0 && r.y==0 && r.width==WIDTH && r.height==HEIGHT;}
    oss_roi frame() const {return {unsigned(PrimaryCCD.getSubX()),unsigned(PrimaryCCD.getSubY()),unsigned(PrimaryCCD.getSubW()),unsigned(PrimaryCCD.getSubH())};}
    oss_roi capture_roi={0,0,WIDTH,HEIGHT};unsigned capture_bin=1,raw_height=1052,active_width=WIDTH,active_height=HEIGHT;size_t raw_bytes=1434u*1052u*2u;
    oss_camera *camera=nullptr;Evidence evidence;bool fault=false;std::atomic<bool> uploading{false};int timer_id=-1;Clock::time_point deadline;
    void fail(int rc){LOGF_ERROR("Acquisition failed: %s; partial raw evidence retained. No retry or reset.",oss_error(rc));evidence.close(rc,-1);InExposure=false;fault=true;PrimaryCCD.setExposureFailed();}
    void readImage(){
        oss_capture_result result={};int rc=oss_capture_raw(camera,Evidence::image,&evidence,30000,nullptr,nullptr,&result);
        if(rc || result.bytes!=raw_bytes || result.read_ahead_bytes || !evidence.good()){fail(rc?rc:OSS_EFRAME);return;}
        rc=oss_stop(camera,nullptr);oss_status status={};if(!rc)rc=oss_get_status(camera,&status,nullptr);
        if(rc || status.state!=0){fail(rc?rc:OSS_ESTATE);return;}
        if(!evidence.close(0,0)){fail(OSS_EIO);return;}
        std::ifstream input(evidence.directory+"/frame.raw",std::ios::binary);std::vector<uint8_t> raw(raw_bytes);
        input.read(reinterpret_cast<char *>(raw.data()),raw.size());if(!input || input.gcount()!=std::streamsize(raw.size())){fail(OSS_EIO);return;}
        auto *active=reinterpret_cast<uint16_t *>(PrimaryCCD.getFrameBuffer());
        rc=fullFrame(capture_roi)?oss_reconstruct_active(capture_bin,raw.data(),raw.size(),active,size_t(active_width)*active_height):oss_reconstruct_roi_bin(capture_bin,&capture_roi,raw.data(),raw.size(),active,size_t(active_width)*active_height);
        if(rc){fail(rc);return;}
        PrimaryCCD.setExposureLeft(0);InExposure=false;
        uploading=true;
        if(!ExposureComplete(&PrimaryCCD)){uploading=false;LOG_ERROR("INDI image delivery failed; original raw evidence remains available.");PrimaryCCD.setExposureFailed();}
    }
};
static std::unique_ptr<OpenStarShootCCD> camera_driver(new OpenStarShootCCD());
