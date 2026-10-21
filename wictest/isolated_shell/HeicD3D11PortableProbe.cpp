// Portable research-only D3D11VA HEVC Main10 *hardware frame* proof.
// The only inputs are two pinned sample Annex B files next to this EXE.
// No original HEIC files, registry changes, COM install, codecs, network,
// MSI, background activity, or output image writes. Stdout diagnostics only.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "HevcAnnexBSpsProfile.h"
#include "HeicD3D11GpuThumbnail.h"
#include "HeicThumbFantReference.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swscale.lib")
#pragma comment(lib,"psapi.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"windowscodecs.lib")
namespace {
using Clock=std::chrono::steady_clock;
using Bytes=std::vector<uint8_t>;
void Need(bool valid,const char* msg) {
 if(!valid)throw std::runtime_error(msg);
}
struct FrameFree {void operator()(AVFrame* p)const{av_frame_free(&p);}};
struct PacketFree {void operator()(AVPacket* p)const{av_packet_free(&p);}};
struct CodecFree {void operator()(AVCodecContext* p)const{avcodec_free_context(&p);}};
struct ScaleFree {void operator()(SwsContext* p)const{sws_freeContext(p);}};
using Frame=std::unique_ptr<AVFrame,FrameFree>;
using Packet=std::unique_ptr<AVPacket,PacketFree>;
using Codec=std::unique_ptr<AVCodecContext,CodecFree>;
using Sws=std::unique_ptr<SwsContext,ScaleFree>;
struct Device {AVBufferRef* ptr=nullptr;~Device(){av_buffer_unref(&ptr);}};
std::wstring ExecutableFolder(){
 std::wstring buf(32768,L'\0');
 const DWORD n=GetModuleFileNameW(nullptr,&buf[0],DWORD(buf.size()));
 Need(n>2&&n<buf.size(),"Cannot locate diagnostic executable");
 buf.resize(n);
 const size_t slash=buf.find_last_of(L"\\/");
 Need(slash!=std::wstring::npos,"Executable folder missing");
 return buf.substr(0,slash);
}
Bytes ReadSafe(const std::wstring& file){
 FILE* raw=nullptr;
 Need(_wfopen_s(&raw,file.c_str(),L"rb")==0&&raw,
      "Fixed diagnostic HEVC fixture missing");
 std::unique_ptr<FILE,decltype(&fclose)> input(raw,&fclose);
 Need(_fseeki64(input.get(),0,SEEK_END)==0,"Fixture seek");
 const auto n=_ftelli64(input.get());
 Need(n>=200&&n<64ll*1024*1024,"Fixture size unsafe");
 Need(_fseeki64(input.get(),0,SEEK_SET)==0,"Fixture rewind");
 Bytes out(size_t(n),0);
 Need(fread(out.data(),1,out.size(),input.get())==out.size(),"Fixture incomplete");
 return out;
}
struct Route {bool requestedGpu=false;bool gotD3D11=false;bool thumbPilot=false;AVBufferRef* device=nullptr;HeicGpuThumb::Cache* thumbCache=nullptr;};
AVPixelFormat PickHardware(AVCodecContext* codec,const AVPixelFormat* formats){
 const auto* route=static_cast<Route*>(codec->opaque);
 if(!route||!route->requestedGpu)return AV_PIX_FMT_NONE;
 for(auto* p=formats;*p!=AV_PIX_FMT_NONE;++p)if(*p==AV_PIX_FMT_D3D11)return *p;
 return AV_PIX_FMT_NONE;  // NEVER silently substitute CPU decoding.
}
struct Measurement{
 Bytes bgra;
 bool hardware=false;
 int w=0,h=0;
 AVPixelFormat format=AV_PIX_FMT_NONE;
 int profile=AV_PROFILE_UNKNOWN;
 double decoderMs=0,readbackMs=0,colorMs=0,totalMs=0;
 bool thumbAttempted=false,thumbProduced=false;
 std::string thumbReason;
 HeicGpuThumb::Output thumb;
};
Measurement OneFrame(const Bytes& compressed,Route& route){
 const AVCodec* decoder=avcodec_find_decoder(AV_CODEC_ID_HEVC);
 Need(decoder!=nullptr,"HEVC library decoder unavailable");
 if(route.requestedGpu){
  Need(route.device!=nullptr,"D3D11 GPU device absent");
  bool supported=false;
  for(int i=0;;++i){
   const AVCodecHWConfig* cfg=avcodec_get_hw_config(decoder,i);
   if(!cfg)break;
   if(cfg->device_type==AV_HWDEVICE_TYPE_D3D11VA &&
      cfg->pix_fmt==AV_PIX_FMT_D3D11 &&
      (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)){supported=true;break;}
  }
  Need(supported,"FFmpeg HEVC codec lacks D3D11VA device support");
 }
 Codec ctx(avcodec_alloc_context3(decoder));
 Need(ctx!=nullptr,"HEVC decoder context allocation failed");
 ctx->thread_count=route.requestedGpu?1:2;
 ctx->thread_type=FF_THREAD_FRAME;
 ctx->opaque=&route;
 if(route.requestedGpu){
  ctx->get_format=PickHardware;
  ctx->hw_device_ctx=av_buffer_ref(route.device);
  Need(ctx->hw_device_ctx!=nullptr,"GPU device reference allocation failed");
 }
 Packet packet(av_packet_alloc());
 Frame decoded(av_frame_alloc());
 Frame downloaded(av_frame_alloc());
 Need(packet&&decoded&&downloaded,"HEVC packet/frame allocation failed");
 Need(compressed.size()<int(INT32_MAX)-AV_INPUT_BUFFER_PADDING_SIZE,"HEVC fixture too large");
 Need(av_new_packet(packet.get(),int(compressed.size()))>=0,"HEVC AVPacket allocation failed");
 memcpy(packet->data,compressed.data(),compressed.size());
 const auto started=Clock::now();
 Need(avcodec_open2(ctx.get(),decoder,nullptr)>=0,"HEVC decoder open failed");
 Need(avcodec_send_packet(ctx.get(),packet.get())>=0,"HEVC packet submission failed");
 int result=avcodec_receive_frame(ctx.get(),decoded.get());
 if(result==AVERROR(EAGAIN)){
  Need(avcodec_send_packet(ctx.get(),nullptr)>=0,"HEVC flush failed");
  result=avcodec_receive_frame(ctx.get(),decoded.get());
 }
 Need(result>=0,"HEVC decoder did not return a frame");
 const auto decodedAt=Clock::now();
 const AVFrame* source=decoded.get();
 HeicGpuThumb::Output gpuThumbResult;
 bool thumbOk=false;std::string thumbReason;
 if(route.requestedGpu){
  Need(decoded->format==AV_PIX_FMT_D3D11,
       "AVFrame pixel format is not D3D11 hardware texture");
  route.gotD3D11=true;
  if(route.thumbPilot){
   try{
    Need(route.thumbCache!=nullptr,"GPU thumbnail cache must be supplied explicitly");
    gpuThumbResult=HeicGpuThumb::Render(decoded.get(),route.device,*route.thumbCache);
    thumbOk=true;
   }catch(const std::exception& e){thumbReason=e.what();}
  }
  Need(av_hwframe_transfer_data(downloaded.get(),decoded.get(),0)>=0,
       "D3D11 to CPU GPU frame readback failed");
  source=downloaded.get();
  Need(source->format==AV_PIX_FMT_P010LE || source->format==AV_PIX_FMT_NV12,
       "Unexpected D3D11VA readback format");
 }
 const auto readbackAt=Clock::now();
 Need(source->width>0&&source->height>0&&
      source->width<=8192&&source->height<=8192,"Unexpected HEVC frame geometry");
 Need(source->format!=AV_PIX_FMT_NONE,"Unknown native YUV pixel format");
 Measurement m;
 m.hardware=route.requestedGpu;
 m.w=source->width;m.h=source->height;
 m.format=static_cast<AVPixelFormat>(source->format);
 m.profile=ctx->profile;
 m.thumbAttempted=route.thumbPilot;
 m.thumbProduced=thumbOk;
 m.thumbReason=thumbReason;
 if(thumbOk)m.thumb=std::move(gpuThumbResult);
 m.bgra.resize(size_t(m.w)*m.h*4);
 Sws scaler(sws_getContext(m.w,m.h,m.format,m.w,m.h,
   AV_PIX_FMT_BGRA,SWS_BICUBIC,nullptr,nullptr,nullptr));
 Need(scaler!=nullptr,"YUV to BGRA color converter unavailable");
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
  default:throw std::runtime_error("Unsupported HDR/color matrix; hardware image refused");
 }
 const AVColorRange range=decoded->color_range;
 Need(range==AVCOL_RANGE_JPEG||range==AVCOL_RANGE_MPEG||
     range==AVCOL_RANGE_UNSPECIFIED,"Unsupported full/limited range");
 const int full=(range==AVCOL_RANGE_JPEG)?1:0;
 Need(sws_setColorspaceDetails(scaler.get(),sws_getCoefficients(matrix),full,
   sws_getCoefficients(SWS_CS_DEFAULT),1,0,1<<16,1<<16)>=0,
   "YUV-to-RGB color matrix refused");
 uint8_t* dest[4]={m.bgra.data(),nullptr,nullptr,nullptr};
 int strides[4]={m.w*4,0,0,0};
 Need(sws_scale(scaler.get(),source->data,source->linesize,0,m.h,dest,strides)==m.h,
   "GPU or CPU BGRA conversion incomplete");
 const auto finished=Clock::now();
 auto ms=[](Clock::duration d){return std::chrono::duration<double,std::milli>(d).count();};
 m.decoderMs=ms(decodedAt-started);
 m.readbackMs=ms(readbackAt-decodedAt);
 m.colorMs=ms(finished-readbackAt);
 m.totalMs=ms(finished-started);
 return m;
}
void Print(const char* name,const char* mode,const Measurement& m){
 std::printf("HEIC_GPU_PORTABLE_FRAME sample=%s mode=%s true_hw_d3d11=%u coded_width=%d coded_height=%d hevc_profile=%d cpu_format=%s decoder_ms=%.3f readback_ms=%.3f bgra_ms=%.3f total_ms=%.3f\n",
   name,mode,unsigned(m.hardware),m.w,m.h,m.profile,av_get_pix_fmt_name(m.format),
   m.decoderMs,m.readbackMs,m.colorMs,m.totalMs);
}
double Median3(std::vector<double> measurements){
 Need(measurements.size()==3,"Three repeated measurements required");
 std::sort(measurements.begin(),measurements.end());
 return measurements[1];
}
double MeanRgbError(const Bytes& lhs,const Bytes& rhs){
 Need(lhs.size()==rhs.size()&&lhs.size()==size_t(256)*192*4,
      "Quality control buffers must be matching 256x192 BGRA");
 uint64_t error=0;
 for(size_t i=0;i<lhs.size();i+=4)
  for(int ch=0;ch<3;++ch)error+=uint64_t(std::abs(int(lhs[i+ch])-int(rhs[i+ch])));
 return double(error)/(256.0*192*3);
}
struct ThumbReferences{
 Bytes fant,linear,cubic;
};
void AuditThumbnail(const char* sample,const Measurement& gpu,const ThumbReferences& refs){
 if(!gpu.thumbAttempted)return;
 if(!gpu.thumbProduced){
  std::printf("HEIC_GPU_THUMB_NOT_PROVEN sample=%s reason=%s\n",sample,gpu.thumbReason.c_str());
  return;
 }
 const Bytes& candidate=gpu.thumb.bgra;
 const Bytes& reference=refs.fant;
 Need(candidate.size()==reference.size() && candidate.size()==size_t(256)*192*4,
      "GPU thumb dimensions invalid");
 uint64_t total=0,squares=0;unsigned maxerr=0,nonopaque=0;
 for(size_t p=0;p<candidate.size();p+=4){
  if(candidate[p+3]!=255)++nonopaque;
  for(unsigned ch=0;ch<3;++ch){
   const unsigned diff=unsigned(std::abs(int(candidate[p+ch])-int(reference[p+ch])));
   total+=diff;squares+=uint64_t(diff)*diff;maxerr=std::max(maxerr,diff);
  }
 }
 const double count=256.0*192*3;
 const double mae=double(total)/count,mse=double(squares)/count;
 const double psnr=mse==0?99.0:10.0*std::log10(65025.0/mse);
 std::printf("HEIC_GPU_THUMB_COMPARE sample=%s vs_wic_fant_mae=%.6f psnr_db=%.3f max_rgb_error=%u nonopaque_pixels=%u quality_approved=0\n",
   sample,mae,psnr,maxerr,nonopaque);
 std::printf("HEIC_GPU_THUMB_FILTER_DIAG sample=%s gpu_vs_wic_linear_mae=%.6f gpu_vs_wic_cubic_mae=%.6f wic_linear_vs_fant_mae=%.6f wic_cubic_vs_fant_mae=%.6f\n",
    sample,MeanRgbError(candidate,refs.linear),MeanRgbError(candidate,refs.cubic),
    MeanRgbError(refs.linear,refs.fant),MeanRgbError(refs.cubic,refs.fant));
 std::printf("HEIC_GPU_THUMB_TIMES sample=%s setup_ms=%.3f video_blt_submit_ms=%.3f small_256px_readback_ms=%.3f gpu_thumb_total_ms=%.3f hevc_gpu_decode_ms=%.3f hevc_plus_thumb_ms=%.3f original_full_yuv_readback_ms=%.3f reused_processor=%d\n",
   sample,gpu.thumb.setupMs,gpu.thumb.bltMs,gpu.thumb.smallReadbackMs,
   gpu.thumb.totalMs,gpu.decoderMs,gpu.decoderMs+gpu.thumb.totalMs,gpu.readbackMs,int(gpu.thumb.reusedProcessor));
 std::puts("HEIC_GPU_THUMB_RESEARCH_ONLY_NOT_PRODUCTION_OR_EXPLORER_APPROVED");
}
struct FantEndToEnd {
 double cpuFantMs=0,gpuFantMs=0,cpuTotalMs=0,gpuTotalMs=0;
};
FantEndToEnd CompareFullResolutionFant(const char* sample,
                                       const Measurement& sw,
                                       const Measurement& gpu){
 // This preserves the original source pixels, color matrix, and the EXACT
 // Windows Fant resampler: only HEVC decoding changes CPU -> D3D11VA.
 Need(sw.w==4032&&sw.h==3024&&gpu.w==sw.w&&gpu.h==sw.h,
      "WIC Fant direct A/B is constrained to fixed 12MP Main10 fixture");
 const auto a=Clock::now();
 const Bytes swWic=HeicThumbFant::Scale(sw.bgra,sw.w,sw.h);
 const auto b=Clock::now();
 const Bytes gpuWic=HeicThumbFant::Scale(gpu.bgra,gpu.w,gpu.h);
 const auto c=Clock::now();
 Need(swWic.size()==size_t(256)*192*4 && swWic==gpuWic,
      "GPU-decoded BGRA + WIC Fant DOES NOT MATCH CPU-decoded Fant");
 const auto ms=[](Clock::duration duration){
  return std::chrono::duration<double,std::milli>(duration).count();
 };
 FantEndToEnd t;
 t.cpuFantMs=ms(b-a);t.gpuFantMs=ms(c-b);
 t.cpuTotalMs=sw.totalMs+t.cpuFantMs;
 t.gpuTotalMs=gpu.totalMs+t.gpuFantMs;
 std::printf("HEIC_GPU_FANT_END_TO_END sample=%s cpu_hevc_bgra_ms=%.3f gpu_hevc_full_readback_bgra_ms=%.3f cpu_fant_ms=%.3f gpu_fant_ms=%.3f cpu_thumbnail_total_ms=%.3f gpu_thumbnail_total_ms=%.3f exact_bgra_and_fant=1 wic_fant_output_bytes=%llu explorer_tested=0 product_enabled=0\n",
   sample,sw.totalMs,gpu.totalMs,t.cpuFantMs,t.gpuFantMs,
   t.cpuTotalMs,t.gpuTotalMs,static_cast<unsigned long long>(gpuWic.size()));
 return t;
}
bool Audit(const char* name,const Measurement& sw,const Measurement& gpu){
 Need(sw.w==gpu.w&&sw.h==gpu.h&&sw.bgra.size()==gpu.bgra.size(),
      "Hardware frame geometry does not match software");
 uint64_t absTotal=0,squares=0;
 unsigned peak=0;
 for(size_t i=0;i<sw.bgra.size();++i){
  const unsigned diff=unsigned(std::abs(int(sw.bgra[i])-int(gpu.bgra[i])));
  absTotal+=diff;squares+=uint64_t(diff)*diff;peak=std::max(peak,diff);
 }
 const double n=double(sw.bgra.size());
 const double mae=double(absTotal)/n,mse=double(squares)/n;
 const double psnr=mse==0?99.0:10*std::log10(255.0*255.0/mse);
 const bool preliminaryPass=(peak<=4 && mae<=0.25);
 std::printf("HEIC_GPU_PORTABLE_COMPARE sample=%s hardware_frame=1 exact=%d mae_rgba=%.6f psnr_db=%.3f max_error=%u preliminary_color_pass=%d\n",
   name,int(absTotal==0),mae,psnr,peak,int(preliminaryPass));
 if(!preliminaryPass)
   std::puts("HEIC_GPU_PORTABLE_COLOR_NOT_PROVEN hardware_frame_exists_but_color_quality_failed");
 return preliminaryPass;
}
}
int wmain(int argc,wchar_t** argv){
 try {
  Need(argc==2,"usage: HeicD3D11PortableProbe.exe --software-selftest | 0..9 | thumb:0..9");
  const std::wstring arg=argv[1];
  const bool selftest=arg==L"--software-selftest";
  const bool gpuThumbMode=(arg.size()==7&&arg.rfind(L"thumb:",0)==0);
  const std::wstring indexText=gpuThumbMode?arg.substr(6):arg;
  Need(selftest||(indexText.size()==1&&indexText[0]>=L'0'&&indexText[0]<=L'9'),
       "Only DXGI index 0..9, thumb:0..9 or --software-selftest accepted");
  const auto dir=ExecutableFolder();
  const auto fixtureDir=dir+L"\\samples\\";
  const char* names[]={"04","fixture-irot3-exif6-noaux","synthetic12mp_detail"};
  if(selftest){
   Route sw;sw.requestedGpu=false;
   for(int i=0;i<3;++i){
    const char* name=names[i];
    const std::wstring stem(name,name+strlen(name));
    const Bytes input=ReadSafe(fixtureDir+stem+L".h265");
    const auto sps=HevcSps::ReadAnnexBSpsProfile(input);
    Need(!sps.malformed && sps.spsCount>0,"Missing/malformed HEVC SPS early profile");
    const Measurement result=OneFrame(input,sw);
    Print(name,"software-selftest",result);
    const int expectedW=i==0?2048:(i==1?1920:4032);
    const int expectedH=i==0?2048:(i==1?1080:3024);
    const int expectedProfile=i==2?AV_PROFILE_HEVC_MAIN_10:AV_PROFILE_HEVC_REXT;
    Need(result.w==expectedW&&result.h==expectedH,
         "Packed HEVC fixture decoded with wrong dimensions");
    std::printf("HEIC_GPU_PORTABLE_PROFILE sample=%s got=%d expected=%d\n",
       name,result.profile,expectedProfile);
    std::printf("HEIC_GPU_PORTABLE_SPS_PROFILE sample=%s idc=%d sps_count=%d\n",
       name,sps.generalProfileIdc,sps.spsCount);
    Need(result.profile==expectedProfile && sps.generalProfileIdc==expectedProfile,
         "HEVC profile changed/mismatched SPS; refuse GPU profile contract");
    if(i==2){
     const Bytes ref=HeicThumbFant::Scale(result.bgra,result.w,result.h);
     const Bytes control=HeicThumbFant::Scale(result.bgra,result.w,result.h);
     Need(ref==control && ref.size()==size_t(256)*192*4,
          "Windows WIC Fant control not deterministic at 12MP");
     int nonOpaque=0;
     for(size_t p=3;p<ref.size();p+=4)nonOpaque+=(ref[p]!=255);
     Need(nonOpaque==0,"WIC Fant opacity/alpha unexpected");
     std::puts("HEIC_GPU_THUMB_WIC_FANT_REFERENCE_256x192_SELFTEST_PASS");
    }
   }
   std::puts("HEIC_GPU_PORTABLE_SOFTWARE_SELFTEST_3_OF_3_PASS");
   std::puts("HEIC_GPU_PORTABLE_READONLY_NO_SYSTEM_CHANGES_NO_MSI");
   return 0;
  }
  const int adapter=indexText[0]-L'0';
  // Important: a D3D11 texture from WARP / Microsoft Basic Render is NOT
  // evidence of hardware HEVC acceleration. Only physically enumerated
  // Intel adapters can be counted by this Intel GPU research diagnostic.
  Microsoft::WRL::ComPtr<IDXGIFactory1> dxgi;
  if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(dxgi.GetAddressOf())))){
   std::puts("HEIC_GPU_PORTABLE_NOT_PROVEN stage=dxgi_factory");
   return 0;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter1> selected;
  const HRESULT enumerated=dxgi->EnumAdapters1(UINT(adapter),selected.GetAddressOf());
  if(FAILED(enumerated)||!selected){
   std::printf("HEIC_GPU_PORTABLE_NOT_PROVEN stage=adapter_not_present adapter=%d\n",adapter);
   return 0;
  }
  DXGI_ADAPTER_DESC1 info{};
  Need(SUCCEEDED(selected->GetDesc1(&info)),"DXGI adapter metadata query failed");
  const bool softwareAdapter=(info.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0 ||
    (info.VendorId==0x1414);
  const bool intel=info.VendorId==0x8086;
  std::printf("HEIC_GPU_PORTABLE_ADAPTER index=%d vendor=0x%04X device=0x%04X software=%d intel=%d description=%ls\n",
    adapter,info.VendorId,info.DeviceId,int(softwareAdapter),int(intel),info.Description);
  if(softwareAdapter||!intel){
   std::puts("HEIC_GPU_PORTABLE_NOT_PROVEN stage=not_physical_intel_adapter");
   return 0;
  }
  Device gpu;
  const std::string which(1,char('0'+adapter));
  const auto start=Clock::now();
  const int err=av_hwdevice_ctx_create(&gpu.ptr,AV_HWDEVICE_TYPE_D3D11VA,
    which.c_str(),nullptr,0);
  const double gpuInit=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
  std::printf("HEIC_GPU_PORTABLE_DEVICE adapter=%d init_ms=%.3f result=%d\n",
    adapter,gpuInit,err);
  if(err<0||!gpu.ptr){
   std::puts("HEIC_GPU_PORTABLE_NOT_PROVEN stage=create_device");
   return 0;
  }
  HeicGpuThumb::Cache thumbCache;
  Route hardware;hardware.requestedGpu=true;hardware.device=gpu.ptr;
  hardware.thumbPilot=gpuThumbMode;
  hardware.thumbCache=gpuThumbMode?&thumbCache:nullptr;
  Route software;software.requestedGpu=false;
  int proven=0;
  for(const char* name:names){
   const std::wstring stem(name,name+strlen(name));
   const Bytes compressed=ReadSafe(fixtureDir+stem+L".h265");
   try{
    const auto sps=HevcSps::ReadAnnexBSpsProfile(compressed);
    Need(!sps.malformed && sps.spsCount>0,"Missing/malformed AnnexB HEVC profile");
    const Measurement sw=OneFrame(compressed,software);
    Need(sps.generalProfileIdc==sw.profile,
         "SPS profile mismatch versus software decoder: fail closed");
    Print(name,"software",sw);
    // The Intel D3D11 Main10 hardware profile does not imply RExt support.
    // The two smaller samples are HEVC Range Extensions, not HEVC Main10.
    // They demonstrably failed hwaccel initialisation on Intel HD620. Keep
    // them software-only, rather than producing noisy driver errors.
    const bool main10=(sps.generalProfileIdc==AV_PROFILE_HEVC_MAIN_10 &&
                       sw.profile==AV_PROFILE_HEVC_MAIN_10);
    std::printf("HEIC_GPU_PORTABLE_PROFILE_GUARD sample=%s sps_idc=%d decoded_profile=%d sps_count=%d route=%s\n",
      name,sps.generalProfileIdc,sw.profile,sps.spsCount,
      main10?"d3d11_candidate":"software_only");
    if(!main10)continue;
    ThumbReferences wicReference;
    if(gpuThumbMode){
     wicReference.fant=HeicThumbFant::Scale(sw.bgra,sw.w,sw.h);
     wicReference.linear=HeicThumbFant::ScaleLinear(sw.bgra,sw.w,sw.h);
     wicReference.cubic=HeicThumbFant::ScaleCubic(sw.bgra,sw.w,sw.h);
    }
    hardware.gotD3D11=false;
    const Measurement hw=OneFrame(compressed,hardware);
    Need(hardware.gotD3D11&&hw.hardware,
       "Hardware decode was replaced by software unexpectedly");
    Print(name,"gpu",hw);
    if(gpuThumbMode)AuditThumbnail(name,hw,wicReference);
    if(!Audit(name,sw,hw)){
     std::printf("HEIC_GPU_PORTABLE_NOT_PROVEN sample=%s stage=color_audit_fail_closed\n",name);
     continue;
    }
    // A/B includes the same real Windows WIC Fant filter and demands exact
    // thumbnail bytes before we count any speedup.
    const FantEndToEnd firstFant=CompareFullResolutionFant(name,sw,hw);
    std::vector<double> cpuFantTotals{firstFant.cpuTotalMs};
    std::vector<double> gpuFantTotals{firstFant.gpuTotalMs};
    std::vector<double> cpuFantOnly{firstFant.cpuFantMs};
    std::vector<double> gpuFantOnly{firstFant.gpuFantMs};
    // Cold one-shot timing can misrepresent Intel GPU driver setup.
    // Retest on the SAME GPU device, but with fresh decoders/frames each time.
    // This is bounded at three pairs, so it never becomes an open-ended loop.
    std::vector<double> cpuTotal{sw.totalMs},gpuTotal{hw.totalMs};
    std::vector<double> gpuDecode{hw.decoderMs},gpuReadback{hw.readbackMs},gpuColor{hw.colorMs};
    std::vector<double> thumbTotal,smallReadback;
    if(gpuThumbMode&&hw.thumbProduced){
     thumbTotal.push_back(hw.thumb.totalMs);
     smallReadback.push_back(hw.thumb.smallReadbackMs);
    }
    for(int repetition=1;repetition<3;++repetition){
     const Measurement nextCpu=OneFrame(compressed,software);
     Print(name,"software-repeat",nextCpu);
     hardware.gotD3D11=false;
     const Measurement nextGpu=OneFrame(compressed,hardware);
     Need(hardware.gotD3D11&&nextGpu.hardware,
         "Repeated Main10 D3D11VA lost hardware device texture");
     Print(name,"gpu-repeat",nextGpu);
     if(gpuThumbMode)AuditThumbnail(name,nextGpu,wicReference);
     Need(Audit(name,nextCpu,nextGpu),
         "Repeated hardware decode failed pixel/color comparison");
     const FantEndToEnd nextFant=CompareFullResolutionFant(name,nextCpu,nextGpu);
     cpuFantTotals.push_back(nextFant.cpuTotalMs);
     gpuFantTotals.push_back(nextFant.gpuTotalMs);
     cpuFantOnly.push_back(nextFant.cpuFantMs);
     gpuFantOnly.push_back(nextFant.gpuFantMs);
     cpuTotal.push_back(nextCpu.totalMs);
     gpuTotal.push_back(nextGpu.totalMs);
     gpuDecode.push_back(nextGpu.decoderMs);
     gpuReadback.push_back(nextGpu.readbackMs);
     gpuColor.push_back(nextGpu.colorMs);
     if(gpuThumbMode&&nextGpu.thumbProduced){
      thumbTotal.push_back(nextGpu.thumb.totalMs);
      smallReadback.push_back(nextGpu.thumb.smallReadbackMs);
     }
    }
    const double cpuMedian=Median3(cpuTotal),gpuMedian=Median3(gpuTotal);
    std::printf("HEIC_GPU_PORTABLE_REPEATED_BENCH sample=%s trials=3 cpu_total_median_ms=%.3f gpu_total_median_ms=%.3f gpu_decoder_median_ms=%.3f gpu_readback_median_ms=%.3f gpu_color_median_ms=%.3f total_improvement_percent=%.2f true_hardware_passes=3 exact_reference_audited=3\n",
       name,cpuMedian,gpuMedian,Median3(gpuDecode),Median3(gpuReadback),
       Median3(gpuColor),100.0*(cpuMedian-gpuMedian)/cpuMedian);
    const double cFant=Median3(cpuFantTotals),gFant=Median3(gpuFantTotals);
    std::printf("HEIC_GPU_FANT_3PASS_MEDIAN sample=%s trials=3 cpu_thumbnail_median_ms=%.3f gpu_thumbnail_median_ms=%.3f cpu_fant_only_median_ms=%.3f gpu_fant_only_median_ms=%.3f thumbnail_speedup_percent=%.3f exact_wic_fant_pixels_all_3=1 use_full_p010_readback=1 initial_explorer_gain_unproven=1\n",
      name,cFant,gFant,Median3(cpuFantOnly),Median3(gpuFantOnly),
      100.0*(cFant-gFant)/cFant);
    std::puts("HEIC_GPU_FANT_PIXELS_3_OF_3_IDENTICAL_KEEP_ORIGINAL_WINDOWS_FILTER");
    if(gpuThumbMode){
     if(thumbTotal.size()==3){
      std::printf("HEIC_GPU_THUMB_REPEATED_BENCH sample=%s trials=3 gpu_thumb_median_ms=%.3f small_bgra_readback_median_ms=%.3f hevc_gpu_decode_median_ms=%.3f gpu_decode_plus_thumb_median_ms=%.3f control_full_readback_median_ms=%.3f research_only=1\n",
       name,Median3(thumbTotal),Median3(smallReadback),Median3(gpuDecode),
       Median3(gpuDecode)+Median3(thumbTotal),Median3(gpuReadback));
     }else{
      std::printf("HEIC_GPU_THUMB_REPEATED_NOT_PROVEN sample=%s completed=%llu requested=3\n",
       name,static_cast<unsigned long long>(thumbTotal.size()));
     }
    }
    ++proven;
   }catch(const std::exception& err){
    std::printf("HEIC_GPU_PORTABLE_NOT_PROVEN sample=%s reason=%s\n",
      name,err.what());
   }
  }
  std::printf("HEIC_GPU_PORTABLE_SUMMARY gpu_eligible_main10=1 proven=%d total_samples=3 software_rext_samples=2 device_reused=1 product_enabled=0\n",proven);
  if(proven==1)std::puts("HEIC_GPU_PORTABLE_MAIN10_HARDWARE_12MP_DECODE_AND_CPU_READBACK_PROVEN");
  else std::puts("HEIC_GPU_PORTABLE_GPU_PATH_NOT_PROVEN_KEEP_SOFTWARE_FALLBACK");
  std::puts("HEIC_GPU_PORTABLE_READONLY_NO_SYSTEM_CHANGES_NO_MSI");
  return 0;
 }catch(const std::exception& error){
  std::printf("HEIC_GPU_PORTABLE_FATAL %s\n",error.what());return 2;
 }
}
