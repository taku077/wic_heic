// Isolated CI-only comparison of *decode to YCbCr* and *decode to RGB*.
// This is a timing decomposition, NOT a replacement WIC thumbnail provider:
// thumbnails still need correct RGB and color-management conversion.
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
std::wstring Env(const wchar_t* name){
 const DWORD len=GetEnvironmentVariableW(name,nullptr,0);
 if(len<2 || len>32767)return {};
 std::wstring s(len,L'\0');
 const DWORD n=GetEnvironmentVariableW(name,&s[0],len);
 if(n==0||n>=len)return {};
 s.resize(n);return s;
}
void Require(bool condition,const char* msg){
 if(!condition){std::printf("HEIC_COLOR_AB_FAILED %s\n",msg);throw std::runtime_error(msg);}
}
uint64_t CpuTicks(){
 FILETIME a{},b{},kernel{},user{};
 Require(GetProcessTimes(GetCurrentProcess(),&a,&b,&kernel,&user)!=0,"CPU ticks");
 auto f=[](FILETIME v)->uint64_t{return uint64_t(v.dwLowDateTime)|(uint64_t(v.dwHighDateTime)<<32u);};
 return f(kernel)+f(user);
}
struct Sample{double elapsedMs=0,cpuMs=0;int format=0,threads=0;uint32_t checksum=0;};
Sample Decode(heif::ImageHandle& primary,int format,int workers) {
 heif_decoding_options* opts=heif_decoding_options_alloc();
 Require(opts!=nullptr,"allocate libheif decoding options");
 opts->num_codec_threads=workers;
 const auto cs=(format==0)?heif_colorspace_RGB:heif_colorspace_YCbCr;
 const auto chroma=(format==0)?heif_chroma_interleaved_RGB:heif_chroma_420;
 heif_image* image=nullptr;
 const uint64_t cpuStart=CpuTicks();
 const auto start=Clock::now();
 const heif_error err=heif_decode_image(primary.get_raw_image_handle(),
     &image,cs,chroma,opts);
 const auto end=Clock::now();
 const uint64_t cpuEnd=CpuTicks();
 heif_decoding_options_free(opts);
 if(err.code!=heif_error_Ok || !image){
   if(image)heif_image_release(image);
   std::printf("HEIC_COLOR_AB_LIBHEIF_ERROR code=%d message=%s\n",int(err.code),err.message?err.message:"?");
   throw std::runtime_error("libheif decode failed");
 }
 int stride=0;
 const auto channel=(format==0)?heif_channel_interleaved:heif_channel_Y;
 const auto pixels=heif_image_get_plane_readonly(image,channel,&stride);
 const auto w=heif_image_get_width(image,channel);
 const auto h=heif_image_get_height(image,channel);
 Require(pixels && stride>0 && w==4032 && h==3024,"unexpected decoded geometry");
 uint32_t checksum=2166136261u;
 // Small deterministic sample verifies the decoded picture was materialized.
 for(int y=0;y<3024;y+=173){
   const uint8_t* row=pixels+size_t(y)*size_t(stride);
   for(int x=0;x<4032;x+=109){checksum=(checksum^row[x])*16777619u;}
 }
 heif_image_release(image);
 Sample s;
 s.elapsedMs=std::chrono::duration<double,std::milli>(end-start).count();
 s.cpuMs=double(cpuEnd-cpuStart)/10000.0;
 s.format=format;s.threads=workers;s.checksum=checksum;
 return s;
}
double Median(std::vector<double> v){
 std::sort(v.begin(),v.end());return (v[v.size()/2-1]+v[v.size()/2])/2.0;
}
}
int wmain(int argc,wchar_t** argv) {
 try {
  Require(argc==2,"expected artificial HEIC in CI");
  const auto path=std::wstring(argv[1]);
  const auto expected=Env(L"RUNNER_TEMP")+L"\\wicfast_synthetic_4032x3024.heic";
  Require(Env(L"GITHUB_ACTIONS")==L"true" && path==expected,
          "only temporary generated image permitted");
  Require(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES,"synthetic input not found");
  std::string ascii;
  for(wchar_t ch:path){Require(ch>0&&ch<128,"non-ASCII CI path");ascii.push_back(char(ch));}
  heif::Context context;
  context.read_from_file(ascii);
  heif::ImageHandle primary=context.get_primary_image_handle();
  Require(primary.get_width()==4032&&primary.get_height()==3024,"unexpected input size");
  for(int nthreads:{2,4}){
   const int order[]={0,1,1,0,0,1,1,0};
   std::vector<double> durations[2],cpu[2];
   uint32_t hashes[2]={0,0};
   for(int i=0;i<8;++i){
     const int format=order[i];
     const auto s=Decode(primary,format,nthreads);
     if(durations[format].empty())hashes[format]=s.checksum;
     Require(hashes[format]==s.checksum,"unstable decoded result across repeats");
     durations[format].push_back(s.elapsedMs);
     cpu[format].push_back(s.cpuMs);
     std::printf("HEIC_COLOR_AB_SAMPLE threads=%d mode=%s index=%d wall_ms=%.2f cpu_ms=%.2f sample_checksum=%08X\n",
       nthreads,format?"ycbcr420":"rgb24",i,s.elapsedMs,s.cpuMs,s.checksum);
   }
   const double rgb=Median(durations[0]),yuv=Median(durations[1]);
   std::printf("HEIC_COLOR_AB_MEDIAN threads=%d rgb_ms=%.2f ycbcr420_ms=%.2f rgb_minus_yuv_ms=%.2f rgb_cpu_ms=%.2f yuv_cpu_ms=%.2f\n",
      nthreads,rgb,yuv,rgb-yuv,Median(cpu[0]),Median(cpu[1]));
   std::puts("HEIC_COLOR_AB_RESULTS_NOT_PIXEL_PARITY_DIFFERENT_COLOR_SPACES");
  }
  std::puts("HEIC_COLOR_AB_CI_ONLY_SYNTHETIC_READONLY_PASSED");
  return 0;
 }catch(const std::exception& e) {
  std::printf("HEIC_COLOR_AB_FATAL stage=%s\n",e.what());return 1;
 }catch(...) { std::puts("HEIC_COLOR_AB_FATAL unknown");return 2;}
}
