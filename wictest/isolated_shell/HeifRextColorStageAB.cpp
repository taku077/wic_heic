// CI-only HEVC RExt software-decoder versus libheif RGB conversion cost.
// 10bit, fixed 2 real HEIC items, pinned CI workspace. NOT equivalent color
// output: this tests whether color conversion is large enough to optimize.
// Never registers WIC, edits source images, or creates a GPU success claim.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#define LIBHEIF_STATIC_BUILD
#define LIBDE265_STATIC_BUILD
#define KVZ_STATIC_LIB
#include <libheif/heif_cxx.h>
#pragma comment(lib,"heif.lib")
#pragma comment(lib,"libde265.lib")
#pragma comment(lib,"libkvazaar.lib")
using Clock=std::chrono::steady_clock;
namespace {
void Need(bool v,const char* m) {
 if(!v){std::printf("HEIC_REXT_COLOR_SPLIT_ERROR %s\n",m);throw std::runtime_error(m);}
}
std::wstring Env(const wchar_t* key){
 const DWORD n=GetEnvironmentVariableW(key,nullptr,0);
 if(n<2||n>32767)return {};
 std::wstring v(n,L'\0');
 const DWORD got=GetEnvironmentVariableW(key,&v[0],n);
 if(!got||got>=n)return {};
 v.resize(got);return v;
}
uint64_t CpuTicks(){
 FILETIME a{},b{},kernel{},user{};
 Need(GetProcessTimes(GetCurrentProcess(),&a,&b,&kernel,&user)!=0,"GetProcessTimes");
 auto toU64=[](FILETIME f)->uint64_t{
  return uint64_t(f.dwLowDateTime)|(uint64_t(f.dwHighDateTime)<<32);
 };
 return toU64(kernel)+toU64(user);
}
struct Output{double ms=0,cpuMs=0;uint64_t hash=0;uint64_t bytes=0;int width=0,height=0;};
uint64_t HashPlane(uint64_t state,const uint8_t* buf,int pitch,int w,int h,
                   unsigned bpp,uint64_t& totalBytes){
 Need(buf&&w>0&&h>0&&bpp>0&&bpp<=8&&
      uint64_t(w)*bpp<=uint64_t(pitch),"Truncated decoded color plane");
 const size_t rowBytes=size_t(w)*bpp;
 for(int y=0;y<h;++y){
  const uint8_t* scanline=buf+size_t(y)*pitch;
  for(size_t i=0;i<rowBytes;++i){state^=scanline[i];state*=1099511628211ull;}
 }
 totalBytes+=uint64_t(rowBytes)*h;
 return state;
}
Output Decode(const heif::ImageHandle& primary,bool rgb,int workers,
              int expectedW,int expectedH){
 heif_decoding_options* opt=heif_decoding_options_alloc();
 Need(opt!=nullptr,"Allocation of decoding options failed");
 opt->num_codec_threads=workers;
 heif_image* raw=nullptr;
 const uint64_t c0=CpuTicks();const auto t0=Clock::now();
 const heif_error status=heif_decode_image(primary.get_raw_image_handle(),
    &raw,rgb?heif_colorspace_RGB:heif_colorspace_YCbCr,
    rgb?heif_chroma_interleaved_RRGGBB_LE:heif_chroma_420,opt);
 const auto t1=Clock::now();const uint64_t c1=CpuTicks();
 heif_decoding_options_free(opt);
 Need(status.code==heif_error_Ok && raw,
      rgb?"libheif RGB16 decode failed":"libheif native YUV420 decode failed");
 Output result;
 result.ms=std::chrono::duration<double,std::milli>(t1-t0).count();
 result.cpuMs=double(c1-c0)/10000.;
 uint64_t hash=14695981039346656037ull,bytes=0;
 if(rgb){
  int pitch=0;
  const uint8_t* p=heif_image_get_plane_readonly(raw,heif_channel_interleaved,&pitch);
  const int w=heif_image_get_width(raw,heif_channel_interleaved);
  const int h=heif_image_get_height(raw,heif_channel_interleaved);
  Need(w==expectedW&&h==expectedH,"HEIF RGB16 geometry mismatch");
  hash=HashPlane(hash,p,pitch,w,h,6,bytes);
  result.width=w;result.height=h;
 }else{
  for(heif_channel c:{heif_channel_Y,heif_channel_Cb,heif_channel_Cr}){
   int pitch=0;
   const uint8_t* p=heif_image_get_plane_readonly(raw,c,&pitch);
   const int w=heif_image_get_width(raw,c);
   const int h=heif_image_get_height(raw,c);
   const bool luma=(c==heif_channel_Y);
   Need(w==(luma?expectedW:(expectedW+1)/2) &&
        h==(luma?expectedH:(expectedH+1)/2),
        "HEIF YUV420 plane geometry or chroma layout mismatch");
   hash=HashPlane(hash,p,pitch,w,h,2,bytes);
  }
  result.width=expectedW;result.height=expectedH;
 }
 result.hash=hash;result.bytes=bytes;
 heif_image_release(raw);
 return result;
}
double Median(std::vector<double> values){
 Need(values.size()==4,"Exactly four samples required");
 std::sort(values.begin(),values.end());
 return (values[1]+values[2])/2;
}
void RunOne(const std::wstring& path,const char* name,
            int expectedW,int expectedH){
 std::string utf8;
 for(wchar_t c:path){Need(c>0&&c<128,"CI path not ASCII");utf8.push_back(char(c));}
 heif::Context context;context.read_from_file(utf8);
 auto primary=context.get_primary_image_handle();
 Need(!primary.empty() && primary.get_width()==expectedW &&
      primary.get_height()==expectedH &&
      primary.get_luma_bits_per_pixel()==10 &&
      primary.get_chroma_bits_per_pixel()==10 &&
      !primary.has_alpha_channel(),
      "RExt 10bit opaque image preconditions changed");
 for(int workers:{2,4}){
  // Symmetric A-B-B-A timing order to reduce warmup/thermal/order bias:
  // 4 decoded results per route, each route checked for stable full bytes.
  constexpr bool order[]={true,false,false,true,false,true,true,false};
  std::vector<double> times[2],cpu[2];
  uint64_t refHash[2]={},refBytes[2]={};
  for(unsigned i=0;i<8;++i){
   const bool rgb=order[i];const unsigned route=rgb?1u:0u;
   const Output out=Decode(primary,rgb,workers,expectedW,expectedH);
   if(times[route].empty()){refHash[route]=out.hash;refBytes[route]=out.bytes;}
   Need(out.hash==refHash[route] && out.bytes==refBytes[route],
        "Decoded image bytes changed between identical RExt iterations");
   times[route].push_back(out.ms);cpu[route].push_back(out.cpuMs);
   std::printf("HEIC_REXT_COLOR_SAMPLE sample=%s threads=%d index=%u route=%s decode_wall_ms=%.3f decode_cpu_ms=%.3f visible_bytes=%llu bytes_hash=%016llX\n",
      name,workers,i,rgb?"rgb16":"yuv420p10",
      out.ms,out.cpuMs,static_cast<unsigned long long>(out.bytes),
      static_cast<unsigned long long>(out.hash));
  }
  const double rgbMs=Median(times[1]),yuvMs=Median(times[0]);
  // Delta is an empirical conversion-mode difference, NOT a strictly isolated
  // time for RGB alone; libheif performs potentially different extra work.
  std::printf("HEIC_REXT_COLOR_SPLIT sample=%s threads=%d repeats=4 rgb16_ms=%.3f yuv420p10_ms=%.3f mode_delta_ms=%.3f mode_delta_pct_of_rgb=%.3f rgb_cpu_ms=%.3f yuv_cpu_ms=%.3f rgb_bytes=%llu yuv_bytes=%llu each_route_stable_hash=1 equivalent_image_color_not_claimed=1 production_enabled=0\n",
     name,workers,rgbMs,yuvMs,rgbMs-yuvMs,
     rgbMs>0?100.0*(rgbMs-yuvMs)/rgbMs:0.0,Median(cpu[1]),Median(cpu[0]),
     static_cast<unsigned long long>(refBytes[1]),
     static_cast<unsigned long long>(refBytes[0]));
 }
}
}
int wmain(int argc,wchar_t** argv) {
 try{
  Need(argc==2,"Only one pinned repo corpus root permitted");
  const std::wstring workspace=Env(L"GITHUB_WORKSPACE");
  Need(Env(L"GITHUB_ACTIONS")==L"true" && !workspace.empty() &&
       argv[1]==workspace+L"\\wictest\\samples",
       "Only pinned public HEIC corpus allowed in CI");
  RunOne(workspace+L"\\wictest\\samples\\04.heic","04",2048,2048);
  RunOne(workspace+L"\\wictest\\samples\\fixture-irot3-exif6-noaux.heic",
         "fixture-irot3-exif6-noaux",1080,1920);
  std::puts("HEIC_REXT_COLOR_2FILES_2WORKERS_2FORMATS_4REPEATS_VALID_CI_ONLY_PASS");
  return 0;
 }catch(const std::exception& e){
  std::printf("HEIC_REXT_COLOR_SPLIT_FATAL reason=%s\n",e.what());return 1;
 }catch(...){std::puts("HEIC_REXT_COLOR_SPLIT_FATAL unknown");return 2;}
}
