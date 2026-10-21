// Portable research-only D3D11VA HEVC Main10 *hardware frame* proof.
// The only inputs are two pinned sample Annex B files next to this EXE.
// No original HEIC files, registry changes, COM install, codecs, network,
// MSI, background activity, or output image writes. Stdout diagnostics only.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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
struct Route {bool requestedGpu=false;bool gotD3D11=false;AVBufferRef* device=nullptr;};
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
 double decoderMs=0,readbackMs=0,colorMs=0,totalMs=0;
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
 if(route.requestedGpu){
  Need(decoded->format==AV_PIX_FMT_D3D11,
       "AVFrame pixel format is not D3D11 hardware texture");
  route.gotD3D11=true;
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
 std::printf("HEIC_GPU_PORTABLE_FRAME sample=%s mode=%s true_hw_d3d11=%u coded_width=%d coded_height=%d cpu_format=%s decoder_ms=%.3f readback_ms=%.3f bgra_ms=%.3f total_ms=%.3f\n",
   name,mode,unsigned(m.hardware),m.w,m.h,av_get_pix_fmt_name(m.format),
   m.decoderMs,m.readbackMs,m.colorMs,m.totalMs);
}
void Audit(const char* name,const Measurement& sw,const Measurement& gpu){
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
 std::printf("HEIC_GPU_PORTABLE_COMPARE sample=%s hardware_frame=1 exact=%d mae_rgba=%.6f psnr_db=%.3f max_error=%u\n",
   name,int(absTotal==0),mae,psnr,peak);
}
}
int wmain(int argc,wchar_t** argv){
 try {
  Need(argc==2,"usage: HeicD3D11PortableProbe.exe adapter-index [0..9]");
  const std::wstring arg=argv[1];
  Need(arg.size()==1&&arg[0]>=L'0'&&arg[0]<=L'9',"Only a single digit DXGI adapter index 0..9 is accepted");
  const int adapter=arg[0]-L'0';
  const auto dir=ExecutableFolder();
  const auto fixtureDir=dir+L"\\samples\\";
  const char* names[]={"04","fixture-irot3-exif6-noaux"};
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
  Route hardware;hardware.requestedGpu=true;hardware.device=gpu.ptr;
  Route software;software.requestedGpu=false;
  int proven=0;
  for(const char* name:names){
   const std::wstring stem(name,name+strlen(name));
   const Bytes compressed=ReadSafe(fixtureDir+stem+L".h265");
   try{
    const Measurement sw=OneFrame(compressed,software);
    Print(name,"software",sw);
    hardware.gotD3D11=false;
    const Measurement hw=OneFrame(compressed,hardware);
    Need(hardware.gotD3D11&&hw.hardware,
       "Hardware decode was replaced by software unexpectedly");
    Print(name,"gpu",hw);
    Audit(name,sw,hw);
    ++proven;
   }catch(const std::exception& err){
    std::printf("HEIC_GPU_PORTABLE_NOT_PROVEN sample=%s reason=%s\n",
      name,err.what());
   }
  }
  std::printf("HEIC_GPU_PORTABLE_SUMMARY proven=%d total=2 device_reused=1 product_enabled=0\n",proven);
  if(proven==2)std::puts("HEIC_GPU_PORTABLE_MAIN10_HARDWARE_DECODE_AND_CPU_READBACK_PROVEN");
  else std::puts("HEIC_GPU_PORTABLE_GPU_PATH_NOT_PROVEN_KEEP_SOFTWARE_FALLBACK");
  std::puts("HEIC_GPU_PORTABLE_READONLY_NO_SYSTEM_CHANGES_NO_MSI");
  return 0;
 }catch(const std::exception& error){
  std::printf("HEIC_GPU_PORTABLE_FATAL %s\n",error.what());return 2;
 }
}
