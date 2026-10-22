// Read-only libheif/libde265 RExt 10-bit decoding thread-count A/B.
// Two pinned opaque HEIC images: 04.heic (2048x2048) and the rotated
// fixture without auxiliary alpha (coded 1920x1080). No system registration,
// originals, GPU, or production WIC decoder modifications.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
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
void Need(bool good,const char* msg){
 if(!good){std::printf("HEIC_REXT_THREADS_ERROR reason=%s\n",msg);throw std::runtime_error(msg);}
}
std::wstring Env(const wchar_t* name){
 const DWORD n=GetEnvironmentVariableW(name,nullptr,0);
 if(!n||n>32000)return {};
 std::wstring value(n,L'\0');
 const DWORD got=GetEnvironmentVariableW(name,&value[0],n);
 if(!got||got>=n)return {};
 value.resize(got);return value;
}
uint64_t CpuTime(){
 FILETIME created{},exited{},kernel{},user{};
 Need(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,
      "GetProcessTimes failed");
 auto ticks=[](FILETIME x)->uint64_t{
  return uint64_t(x.dwLowDateTime)|(uint64_t(x.dwHighDateTime)<<32);
 };
 return ticks(kernel)+ticks(user);
}
struct Sample {double wallMs=0,cpuMs=0;uint64_t pixelHash=0;size_t pixelBytes=0;};
Sample Decode(const heif::ImageHandle& image,int workers,int expectedW,int expectedH){
 heif_decoding_options* opts=heif_decoding_options_alloc();
 Need(opts!=nullptr,"libheif decoder options allocation failed");
 opts->num_codec_threads=workers;
 heif_image* decoded=nullptr;
 const auto cpuStart=CpuTime();
 const auto start=Clock::now();
 const heif_error error=heif_decode_image(image.get_raw_image_handle(),
       &decoded,heif_colorspace_RGB,heif_chroma_interleaved_RGB,opts);
 const auto end=Clock::now();
 const auto cpuFinish=CpuTime();
 heif_decoding_options_free(opts);
 Need(error.code==heif_error_Ok&&decoded!=nullptr,
      "RExt 10bit RGB decode failed in selected thread mode");
 int stride=0;
 const auto source=heif_image_get_plane_readonly(decoded,heif_channel_interleaved,&stride);
 const int w=heif_image_get_width(decoded,heif_channel_interleaved);
 const int h=heif_image_get_height(decoded,heif_channel_interleaved);
 Need(source&&stride>=w*3 && w==expectedW && h==expectedH,
      "RExt output RGB raster geometry changed unexpectedly");
 uint64_t hash=14695981039346656037ull;
 for(int row=0;row<h;++row) {
  const auto* pixels=source+size_t(row)*stride;
  for(int x=0;x<w*3;++x){hash^=pixels[x];hash*=1099511628211ull;}
 }
 heif_image_release(decoded);
 Sample result;
 result.pixelHash=hash;result.pixelBytes=size_t(w)*h*3;
 result.wallMs=std::chrono::duration<double,std::milli>(end-start).count();
 result.cpuMs=double(cpuFinish-cpuStart)/10000.;
 return result;
}
double Median(std::vector<double> values) {
 Need(values.size()==4,"Expected four ABBA repeats");
 std::sort(values.begin(),values.end());
 return (values[1]+values[2])/2;
}
void Test(const std::wstring& source,const char* name,int expectedW,int expectedH) {
 std::string ascii;
 for(wchar_t c:source){Need(c>0&&c<127,"Non-ASCII CI sample path");ascii.push_back(char(c));}
 heif::Context context;
 context.read_from_file(ascii);
 auto image=context.get_primary_image_handle();
 Need(!image.empty() && image.get_width()==expectedW &&
      image.get_height()==expectedH &&
      image.get_luma_bits_per_pixel()==10 &&
      image.get_chroma_bits_per_pixel()==10 &&
      !image.has_alpha_channel(),"Unsafe primary HEIC thread-test scope");
 const int sequence[12]={1,2,4,4,2,1,1,4,2,2,4,1};
 std::vector<double> wall[5],cpu[5];
 uint64_t reference=0;size_t referenceBytes=0;
 for(int i=0;i<12;++i){
  const int n=sequence[i];
  Sample s=Decode(image,n,expectedW,expectedH);
  if(i==0){reference=s.pixelHash;referenceBytes=s.pixelBytes;}
  Need(s.pixelHash==reference && s.pixelBytes==referenceBytes,
       "10bit RExt RGB full-pixel hash differs by thread count or repeat");
  wall[n].push_back(s.wallMs);cpu[n].push_back(s.cpuMs);
  std::printf("HEIC_REXT_THREADS_SAMPLE sample=%s index=%d workers=%d ms=%.3f cpu_ms=%.3f rgb_bytes=%llu rgb_hash=%016llX\n",
     name,i,n,s.wallMs,s.cpuMs,
     static_cast<unsigned long long>(s.pixelBytes),
     static_cast<unsigned long long>(s.pixelHash));
 }
 const double t1=Median(wall[1]),t2=Median(wall[2]),t4=Median(wall[4]);
 std::printf("HEIC_REXT_THREADS_MEDIAN sample=%s passes=12 threads_1_ms=%.3f threads_2_ms=%.3f threads_4_ms=%.3f threads_2_cpu_ms=%.3f threads_4_cpu_ms=%.3f two_to_four_saved_pct=%.3f exact_rgb_all=1 production_enabled=0\n",
   name,t1,t2,t4,Median(cpu[2]),Median(cpu[4]),100.0*(1.0-t4/t2));
}
}
int wmain(int argc,wchar_t** argv){
 try{
  Need(argc==2,"Expected pinned GitHub CI sample folder");
  const std::wstring root=Env(L"GITHUB_WORKSPACE");
  Need(Env(L"GITHUB_ACTIONS")==L"true" &&
       !root.empty() && std::wstring(argv[1])==root+L"\\wictest\\samples",
       "No arbitrary file inputs allowed");
  Test(root+L"\\wictest\\samples\\04.heic","04",2048,2048);
  // The irot transform is recorded in HEIF metadata, not the raw decoded
  // HEVC frame. Some libheif APIs report displayed 1080x1920 geometry;
  // this test pins the already verified HEIF primary display dimensions.
  Test(root+L"\\wictest\\samples\\fixture-irot3-exif6-noaux.heic",
       "fixture-irot3-exif6-noaux",1080,1920);
  std::puts("HEIC_REXT_THREADS_TWO_OPAQUE_HEIC_12PASS_EACH_PIXEL_PARITY_PASS");
  return 0;
 }catch(const std::exception& e){
  std::printf("HEIC_REXT_THREADS_FATAL %s\n",e.what());return 1;
 }catch(...){std::puts("HEIC_REXT_THREADS_FATAL unknown");return 2;}
}
