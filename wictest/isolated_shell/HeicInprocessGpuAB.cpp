// Research-only one-process HEVC Annex B -> libavcodec HEVC decoder with optional D3D11VA.
// No COM registration, installer, HEIC writes or changes to system codecs.
// A GPU result is only accepted when AV_PIX_FMT_D3D11 was really returned and
// av_hwframe_transfer_data succeeded (never claim a software fallback as GPU).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "psapi.lib")
using Clock=std::chrono::steady_clock;
namespace {
void Require(bool ok,const char* reason){
 if(!ok)throw std::runtime_error(reason);
}
std::wstring Env(const wchar_t* name) {
 DWORD n=GetEnvironmentVariableW(name,nullptr,0);
 if(!n||n>32000)return {};
 std::wstring value(n,L'\0');
 const DWORD count=GetEnvironmentVariableW(name,&value[0],n);
 if(!count||count>=n)return {};
 value.resize(count);
 return value;
}
std::string Ascii(const std::wstring& input){
 std::string out;
 for(wchar_t c:input){Require(c>0&&c<128,"non-ASCII disposable CI path");out.push_back(char(c));}
 return out;
}
using Bytes=std::vector<uint8_t>;
Bytes ReadBounded(const std::wstring& file) {
 FILE* f=nullptr;
 Require(_wfopen_s(&f,file.c_str(),L"rb")==0 && f,"source open failed");
 std::unique_ptr<FILE,decltype(&fclose)> close(f,&fclose);
 Require(_fseeki64(f,0,SEEK_END)==0,"source seek failed");
 const auto n=_ftelli64(f);
 Require(n>128 && n<=128*1024*1024,"source size outside 128MiB bound");
 Require(_fseeki64(f,0,SEEK_SET)==0,"source rewind failed");
 Bytes b(size_t(n),0);
 Require(fread(b.data(),1,b.size(),f)==b.size(),"source read incomplete");
 return b;
}
void SaveNew(const std::wstring& path,const Bytes& b){
 Require(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES,"refusing to overwrite output");
 HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 Require(h!=INVALID_HANDLE_VALUE,"cannot create new output");
 DWORD written=0;
 const BOOL ok=WriteFile(h,b.data(),DWORD(b.size()),&written,nullptr);
 CloseHandle(h);
 Require(ok && written==b.size(),"output write failed");
}
uint64_t ProcessCpuTicks(){
 FILETIME a{},b{},k{},u{};
 Require(GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u)!=0,"GetProcessTimes failed");
 auto ticks=[](FILETIME f){return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;};
 return ticks(k)+ticks(u);
}
uint64_t PrivateBytes(){
 PROCESS_MEMORY_COUNTERS_EX m{};
 m.cb=sizeof(m);
 Require(GetProcessMemoryInfo(GetCurrentProcess(),
   reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m))!=0,"GetProcessMemoryInfo failed");
 return m.PrivateUsage;
}
struct FrameDeleter {void operator()(AVFrame* p)const{av_frame_free(&p);}};
struct PacketDeleter {void operator()(AVPacket* p)const{av_packet_free(&p);}};
struct CodecDeleter {void operator()(AVCodecContext* p)const{avcodec_free_context(&p);}};
struct SwsDeleter {void operator()(SwsContext* p)const{sws_freeContext(p);}};
using Frame=std::unique_ptr<AVFrame,FrameDeleter>;
using Packet=std::unique_ptr<AVPacket,PacketDeleter>;
using Codec=std::unique_ptr<AVCodecContext,CodecDeleter>;
using Sws=std::unique_ptr<SwsContext,SwsDeleter>;
struct Route {
 bool requestedHardware=false;
 bool gotHardwareFrame=false;
 AVBufferRef* device=nullptr; // retained throughout all images
};
enum AVPixelFormat HwFormat(AVCodecContext* context,const enum AVPixelFormat* formats){
 auto* route=static_cast<Route*>(context->opaque);
 if(!route || !route->requestedHardware)return AV_PIX_FMT_NONE;
 for(auto f=formats;*f!=AV_PIX_FMT_NONE;++f){
  if(*f==AV_PIX_FMT_D3D11)return *f;
 }
 // Never silently fall back to a software frame and report GPU success.
 return AV_PIX_FMT_NONE;
}
struct Sample {
 Bytes bgra;
 Bytes nativeYuv10; // research diagnostic only, never used by installed WIC
 double wallMs=0, cpuMs=0;
 uint64_t privateBytes=0;
 bool hardwareFrame=false;
 int width=0,height=0;
 enum AVPixelFormat decodedFormat=AV_PIX_FMT_NONE;
};
Sample Decode(const Bytes& compressed,Route& route){
 const AVCodec* decoder=avcodec_find_decoder(AV_CODEC_ID_HEVC);
 Require(decoder!=nullptr,"HEVC decoder unavailable");
 if(route.requestedHardware){
  Require(route.device!=nullptr,"no reusable D3D11 hardware device");
  bool cfgOk=false;
  for(int i=0;;++i){
   const AVCodecHWConfig* cfg=avcodec_get_hw_config(decoder,i);
   if(!cfg)break;
   if(cfg->device_type==AV_HWDEVICE_TYPE_D3D11VA &&
      cfg->pix_fmt==AV_PIX_FMT_D3D11 &&
      (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)){cfgOk=true;break;}
  }
  Require(cfgOk,"HEVC codec has no D3D11VA hardware device configuration");
 }
 Codec context(avcodec_alloc_context3(decoder));
 Require(context!=nullptr,"AVCodecContext allocation");
 context->thread_count=route.requestedHardware?1:2;
 context->thread_type=FF_THREAD_FRAME;
 context->opaque=&route;
 if(route.requestedHardware){
  context->get_format=HwFormat;
  context->hw_device_ctx=av_buffer_ref(route.device);
  Require(context->hw_device_ctx!=nullptr,"hardware device reference failed");
 }
 Packet packet(av_packet_alloc());Frame decoded(av_frame_alloc());
 Require(packet && decoded,"AVPacket or AVFrame allocation");
 Require(compressed.size()<size_t(INT32_MAX)-AV_INPUT_BUFFER_PADDING_SIZE,"too many compressed bytes");
 Require(av_new_packet(packet.get(),int(compressed.size()))>=0,"AVPacket payload allocation");
 memcpy(packet->data,compressed.data(),compressed.size());
 const uint64_t cpuStart=ProcessCpuTicks();
 const auto start=Clock::now();
 Require(avcodec_open2(context.get(),decoder,nullptr)>=0,"HEVC decoder open failed");
 Require(avcodec_send_packet(context.get(),packet.get())>=0,"HEVC packet submission failed");
 int result=avcodec_receive_frame(context.get(),decoded.get());
 if(result==AVERROR(EAGAIN)){
  Require(avcodec_send_packet(context.get(),nullptr)>=0,"HEVC flush packet failed");
  result=avcodec_receive_frame(context.get(),decoded.get());
 }
 Require(result>=0,"HEVC 1-frame decoder produced no image");
 if(route.requestedHardware){
  Require(decoded->format==AV_PIX_FMT_D3D11,"no D3D11 texture: hardware fallback forbidden");
  route.gotHardwareFrame=true;
 }
 Frame staging(av_frame_alloc());Require(staging!=nullptr,"staging frame allocation");
 const AVFrame* source=decoded.get();
 if(route.requestedHardware){
  Require(av_hwframe_transfer_data(staging.get(),decoded.get(),0)>=0,
    "D3D11 texture CPU readback failed");
  source=staging.get();
  Require(source->format==AV_PIX_FMT_NV12 || source->format==AV_PIX_FMT_P010LE,
    "D3D11 transfer format not NV12/P010 (unsafe for current path)");
 }
 Require(source->width>0 && source->height>0 &&
   source->width<=8192 && source->height<=8192,"decoded geometry unsafe");
 Require(source->format!=AV_PIX_FMT_NONE,"unknown source pixel format");
 const int w=source->width,h=source->height;
 const int pitch=w*4;
 Sample sample;sample.width=w;sample.height=h;
 sample.decodedFormat=static_cast<AVPixelFormat>(source->format);
 sample.hardwareFrame=route.requestedHardware;
 sample.bgra.resize(size_t(pitch)*h);
 Sws convert(sws_getContext(w,h,static_cast<AVPixelFormat>(source->format),
   w,h,AV_PIX_FMT_BGRA,SWS_BICUBIC,nullptr,nullptr,nullptr));
 Require(convert!=nullptr,"swscale source pixel format unsupported");
 // sws_getContext defaults to ITU-601 even when the HEVC VUI is explicitly
 // BT.709/BT.2020. ffmpeg CLI's scale path follows the AVFrame colorspace.
 // Honor only known matrix codes; reject an unsupported matrix rather than
 // making a falsely "matching" 8-bit thumbnail from 10-bit HEIC.
 const AVColorSpace declared=decoded->colorspace;
 int matrix=SWS_CS_DEFAULT;
 switch(declared){
  case AVCOL_SPC_UNSPECIFIED:
  case AVCOL_SPC_BT470BG:
  case AVCOL_SPC_SMPTE170M:matrix=SWS_CS_ITU601;break;
  case AVCOL_SPC_BT709:matrix=SWS_CS_ITU709;break;
  case AVCOL_SPC_FCC:matrix=SWS_CS_FCC;break;
  case AVCOL_SPC_SMPTE240M:matrix=SWS_CS_SMPTE240M;break;
  case AVCOL_SPC_BT2020_NCL:matrix=SWS_CS_BT2020;break;
  default:throw std::runtime_error("unapproved HEVC YCbCr matrix: cannot guarantee color fidelity");
 }
 const AVColorRange declaredRange=decoded->color_range;
 Require(declaredRange==AVCOL_RANGE_UNSPECIFIED ||
         declaredRange==AVCOL_RANGE_MPEG ||
         declaredRange==AVCOL_RANGE_JPEG,"unknown HEVC color range");
 const int srcFull=(declaredRange==AVCOL_RANGE_JPEG)?1:0;
 const int* coefficients=sws_getCoefficients(matrix);
 Require(coefficients!=nullptr,"missing declared YCbCr conversion coefficients");
 Require(sws_setColorspaceDetails(convert.get(),coefficients,srcFull,
   sws_getCoefficients(SWS_CS_DEFAULT),1,0,1<<16,1<<16)>=0,
   "explicit YCbCr matrix and range conversion refused");
 std::printf("HEIC_INPROCESS_COLOR_MATRIX declared=%d matrix=%d range=%d src_full=%d\n",
   int(declared),matrix,int(declaredRange),srcFull);
 uint8_t* dst[4]={sample.bgra.data(),nullptr,nullptr,nullptr};
 int dstStride[4]={pitch,0,0,0};
 Require(sws_scale(convert.get(),source->data,source->linesize,0,h,dst,dstStride)==h,
   "BGRA conversion incomplete");
 sample.wallMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
 sample.cpuMs=double(ProcessCpuTicks()-cpuStart)/10000.0;
 sample.privateBytes=PrivateBytes();
 // Optional lossless decoder-stage diagnostic. Compare raw 10-bit YUV
 // before any YCbCr matrix/range conversion, so color and decode mismatches
 // are not conflated. Keep this allocation outside the benchmark timing.
 if(!route.requestedHardware && Env(L"HEIC_INPROCESS_DUMP_YUV")==L"1" &&
    source->format==AV_PIX_FMT_YUV420P10LE){
  Require(w%2==0 && h%2==0,"odd dimensions rejected by pinned 4:2:0 research corpus");
  sample.nativeYuv10.reserve(size_t(w)*h*3);
  for(int plane=0;plane<3;++plane){
   const int planeW=plane==0?w:w/2,planeH=plane==0?h:h/2;
   const size_t rowBytes=size_t(planeW)*2;
   Require(source->data[plane]!=nullptr && source->linesize[plane]>=int(rowBytes),
           "invalid raw YUV plane stride");
   for(int row=0;row<planeH;++row){
    const uint8_t* begin=source->data[plane]+size_t(row)*source->linesize[plane];
    sample.nativeYuv10.insert(sample.nativeYuv10.end(),begin,begin+rowBytes);
   }
  }
  Require(sample.nativeYuv10.size()==size_t(w)*h*3,
          "unexpected planar 10-bit source size");
 }
 return sample;
}
void PrintSample(const char* mode,const char* file,const Sample& s){
 std::printf("HEIC_INPROCESS_SAMPLE file=%s mode=%s hardware_frame=%d width=%d height=%d pixel_format=%s wall_ms=%.3f cpu_ms=%.3f private_bytes=%llu output_bytes=%llu\n",
 file,mode,int(s.hardwareFrame),s.width,s.height,
 av_get_pix_fmt_name(s.decodedFormat),s.wallMs,s.cpuMs,
 static_cast<unsigned long long>(s.privateBytes),
 static_cast<unsigned long long>(s.bgra.size()));
}
}
int wmain(int argc,wchar_t** argv){
 try{
  Require(argc==2,"usage: HeicInprocessGpuAB.exe software | gpu:<D3D11 adapter index>");
  Require(Env(L"GITHUB_ACTIONS")==L"true","CI only");
  const std::wstring temp=Env(L"RUNNER_TEMP");
  Require(!temp.empty(),"no disposable runner path");
  const std::wstring selection=argv[1];
  const bool gpu=selection.rfind(L"gpu:",0)==0;
  Require(gpu||selection==L"software","unrecognized mode");
  const std::wstring output=temp+(gpu?L"\\heic_inprocess_gpu":L"\\heic_inprocess_software");
  const std::wstring bridge=temp+L"\\heic_native_bridge";
  Require(GetFileAttributesW(bridge.c_str())!=INVALID_FILE_ATTRIBUTES,"validated native bridge directory missing");
  Require(GetFileAttributesW(output.c_str())==INVALID_FILE_ATTRIBUTES,
    "refusing to reuse output directory");
  Require(CreateDirectoryW(output.c_str(),nullptr)!=0,"new disposable output directory unavailable");
  const auto batchStart=Clock::now();
  const uint64_t batchCpuStart=ProcessCpuTicks();
  Route route;route.requestedHardware=gpu;
  int gpuIndex=-1;
  if(gpu){
    const std::wstring value=selection.substr(4);
    Require(value.size()==1 && value[0]>=L'0' && value[0]<=L'9',"GPU index must be single-digit 0-9");
    gpuIndex=value[0]-L'0';
    std::string which(1,char('0'+gpuIndex));
    const auto deviceStart=Clock::now();
    const uint64_t deviceCpuStart=ProcessCpuTicks();
    const int err=av_hwdevice_ctx_create(&route.device,AV_HWDEVICE_TYPE_D3D11VA,
      which.c_str(),nullptr,0);
    const double deviceInitMs=std::chrono::duration<double,std::milli>(Clock::now()-deviceStart).count();
    const double deviceCpuMs=double(ProcessCpuTicks()-deviceCpuStart)/10000.0;
    std::printf("HEIC_INPROCESS_GPU_DEVICE_INIT adapter=%d wall_ms=%.3f cpu_ms=%.3f av_err=%d\n",
      gpuIndex,deviceInitMs,deviceCpuMs,err);
    if(err<0 || !route.device){
     std::printf("HEIC_INPROCESS_GPU_NOT_PROVEN stage=create_device adapter=%d av_err=%d\n",gpuIndex,err);
     return 0;
    }
    std::printf("HEIC_INPROCESS_GPU_DEVICE_CREATED adapter=%d reused_across_images=1\n",gpuIndex);
  }
  struct Release {AVBufferRef** ref;~Release(){av_buffer_unref(ref);}};
  Release release{&route.device};
  const char* names[]={"04","fixture-irot3-exif6-noaux"};
  int completed=0;
  for(const char* stem:names){
    const std::wstring wide(stem,stem+strlen(stem));
    const std::wstring input=bridge+L"\\"+wide+L".h265";
    const auto ioStart=Clock::now();
    const Bytes annex=ReadBounded(input);
    const double readMs=std::chrono::duration<double,std::milli>(Clock::now()-ioStart).count();
    std::printf("HEIC_INPROCESS_IO file=%s bytes=%llu disk_read_ms=%.3f\n",
      stem,static_cast<unsigned long long>(annex.size()),readMs);
    route.gotHardwareFrame=false;
    try{
      const Sample sample=Decode(annex,route);
      const std::wstring destination=output+L"\\"+wide+L".bgra";
      SaveNew(destination,sample.bgra);
      if(!sample.nativeYuv10.empty()){
        SaveNew(output+L"\\"+wide+L".yuv420p10le",sample.nativeYuv10);
        std::printf("HEIC_INPROCESS_RAW_YUV_DUMP file=%s bytes=%llu\n",
          stem,static_cast<unsigned long long>(sample.nativeYuv10.size()));
      }
      PrintSample(gpu?"gpu":"software",stem,sample);
      Require(!gpu || route.gotHardwareFrame,"hardware frame not produced");
      ++completed;
    }catch(const std::exception& e){
      if(!gpu)throw;
      // Preserve explicit no-proof status: never produce a bogus GPU image.
      std::printf("HEIC_INPROCESS_GPU_NOT_PROVEN file=%s stage=decode_or_download message=%s\n",
        stem,e.what());
    }
  }
  const double batchWall=std::chrono::duration<double,std::milli>(Clock::now()-batchStart).count();
  const double batchCpu=double(ProcessCpuTicks()-batchCpuStart)/10000.0;
  std::printf("HEIC_INPROCESS_SUMMARY mode=%s requested=%u decoded=%d device_reused=%d batch_wall_ms=%.3f batch_cpu_ms=%.3f batch_private_bytes=%llu\n",
    gpu?"gpu":"software",unsigned(sizeof(names)/sizeof(names[0])),completed,int(gpu),
    batchWall,batchCpu,static_cast<unsigned long long>(PrivateBytes()));
  Require(gpu || completed==2,"software control must decode both pinned Main10 frames");
  std::puts("HEIC_INPROCESS_RESEARCH_ONLY_NO_REGISTER_NO_MSI_PASS");
  return 0;
 }catch(const std::exception& e){
  std::printf("HEIC_INPROCESS_FATAL %s\n",e.what());return 2;
 }
}
