// Research-only, in-process HEIC decoder-engine A/B benchmark.
// Requires a libheif build with both decoder IDs "libde265" and "ffmpeg".
// Never accesses private images, modifies files, registers COM or builds an MSI.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
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
#pragma comment(lib,"psapi.lib")
using Clock=std::chrono::steady_clock;
namespace {
void Require(bool x,const char* reason){
 if(!x) {std::printf("HEIF_BACKEND_AB_FAILED stage=%s\n",reason);throw std::runtime_error(reason);}
}
std::wstring Env(const wchar_t* name){
 DWORD n=GetEnvironmentVariableW(name,nullptr,0);
 if(n<1||n>32000)return {};
 std::wstring v(n,L'\0');
 DWORD len=GetEnvironmentVariableW(name,&v[0],n);
 if(!len||len>=n)return {};
 v.resize(len);return v;
}
uint64_t CpuTime(){
 FILETIME create{},end{},kernel{},user{};
 Require(GetProcessTimes(GetCurrentProcess(),&create,&end,&kernel,&user)!=0,"CPU clock");
 auto u=[](FILETIME f){return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;};
 return u(kernel)+u(user);
}
uint64_t Hash(const std::vector<uint8_t>& img){
 uint64_t h=14695981039346656037ULL;
 for(uint8_t b:img){h^=b;h*=1099511628211ULL;}
 return h;
}
struct Sample{
 double wall=0,cpu=0;
 size_t privateMiB=0;
 uint64_t hash=0;
 std::vector<uint8_t> rgb;
};
Sample Decode(const heif::ImageHandle& primary,const char* backend){
 // The documented 'decoder_id' overrides plugin priority; this prevents
 // accidentally timing libde265 when ffmpeg plugin is linked but priority=90.
 heif_decoding_options* options=heif_decoding_options_alloc();
 Require(options!=nullptr,"heif_decoding_options_alloc");
 options->decoder_id=backend;
 options->num_codec_threads=2;
 heif_image* img=nullptr;
 const uint64_t cpuBefore=CpuTime();
 const auto started=Clock::now();
 const heif_error result=heif_decode_image(primary.get_raw_image_handle(),
    &img,heif_colorspace_RGB,heif_chroma_interleaved_RGB,options);
 const auto stop=Clock::now();
 const uint64_t cpuAfter=CpuTime();
 heif_decoding_options_free(options);
 if(result.code!=heif_error_Ok || !img){
  if(img)heif_image_release(img);
  std::printf("HEIF_BACKEND_AB_UNAVAILABLE decoder_id=%s code=%d message=%s\n",
    backend,int(result.code),result.message?result.message:"");
  throw std::runtime_error("requested decoder not usable");
 }
 const int width=heif_image_get_width(img,heif_channel_interleaved);
 const int height=heif_image_get_height(img,heif_channel_interleaved);
 int stride=0;
 const uint8_t* ptr=heif_image_get_plane_readonly(img,heif_channel_interleaved,&stride);
 Require(width==4032&&height==3024&&stride>=width*3&&ptr!=nullptr,
         "RGB image output dimensions");
 Sample s;
 s.rgb.resize(size_t(width)*height*3);
 for(int y=0;y<height;y++){
  const auto* from=ptr+size_t(y)*stride;
  std::copy_n(from,width*3,s.rgb.data()+size_t(y)*width*3);
 }
 heif_image_release(img);
 s.wall=std::chrono::duration<double,std::milli>(stop-started).count();
 s.cpu=double(cpuAfter-cpuBefore)/10000.0;
 s.hash=Hash(s.rgb);
 PROCESS_MEMORY_COUNTERS_EX mem{};
 mem.cb=sizeof(mem);
 Require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof(mem))!=0,"GetProcessMemoryInfo");
 s.privateMiB=mem.PrivateUsage/(1024*1024);
 return s;
}
double Median(std::vector<double> x){
 std::sort(x.begin(),x.end());
 return (x[x.size()/2-1]+x[x.size()/2])/2.0;
}
}
int wmain(int argc,wchar_t** argv){
 try {
  Require(argc==2,"usage synthetic12MP.heic");
  const std::wstring path=argv[1];
  const std::wstring runner=Env(L"RUNNER_TEMP");
  Require(Env(L"GITHUB_ACTIONS")==L"true" &&
       !runner.empty()&&path==runner+L"\\wicfast_synthetic_4032x3024.heic",
       "only pinned synthetic CI HEIC");
  Require(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES,"source missing");
  std::string ascii;
  for(wchar_t ch:path){
   Require(ch>0&&ch<=127,"ASCII-only CI temporary path");
   ascii.push_back(char(ch));
  }
  heif::Context ctx;
  ctx.read_from_file(ascii);
  auto frame=ctx.get_primary_image_handle();
  Require(frame.get_width()==4032&&frame.get_height()==3024,"source dimensions");
  // A/B balanced; no process launches, same libheif and same target RGB24.
  const int order[]={0,1,1,0,0,1,1,0};
  const char* names[]={"libde265","ffmpeg"};
  std::vector<double> wall[2],cpu[2];
  std::vector<uint8_t> reference[2];
  uint64_t hashes[2]{};
  for(int i=0;i<8;++i){
    const int mode=order[i];
    Sample sample=Decode(frame,names[mode]);
    if(wall[mode].empty()){
      reference[mode]=sample.rgb;hashes[mode]=sample.hash;
    } else {
      Require(hashes[mode]==sample.hash,"unstable output for same HEVC backend");
    }
    wall[mode].push_back(sample.wall);
    cpu[mode].push_back(sample.cpu);
    std::printf("HEIF_BACKEND_AB_SAMPLE backend=%s index=%d decode_ms=%.3f cpu_ms=%.3f private_mib=%llu pixel_hash=%016llX\n",
        names[mode],i,sample.wall,sample.cpu,
        static_cast<unsigned long long>(sample.privateMiB),
        static_cast<unsigned long long>(sample.hash));
  }
  Require(wall[0].size()==4 && wall[1].size()==4,"unbalanced ABBA");
  const double b=Median(wall[0]),f=Median(wall[1]);
  std::printf("HEIF_BACKEND_AB_MEDIAN libde265_ms=%.3f ffmpeg_ms=%.3f delta_pct=%.2f libde265_cpu_ms=%.3f ffmpeg_cpu_ms=%.3f\n",
      b,f,100.0*(b-f)/b,Median(cpu[0]),Median(cpu[1]));
  Require(reference[0].size()==reference[1].size(),"output size changed");
  uint64_t absSum=0,sqSum=0;
  unsigned peak=0;
  for(size_t i=0;i<reference[0].size();++i){
    const int difference=int(reference[0][i])-int(reference[1][i]);
    const unsigned ad=unsigned(difference<0?-difference:difference);
    absSum+=ad;sqSum+=uint64_t(difference)*difference;
    peak=(std::max)(peak,ad);
  }
  const double mae=double(absSum)/reference[0].size();
  const double mse=double(sqSum)/reference[0].size();
  const double psnr=mse==0?99.0:10*std::log10(255.0*255.0/mse);
  std::printf("HEIF_BACKEND_AB_RGB_QUALITY pixel_exact=%d mae=%.5f psnr_db=%.3f peak_abs_error=%u\n",
        int(absSum==0),mae,psnr,peak);
  std::puts("HEIF_BACKEND_AB_NO_REGISTER_NO_MSI_READONLY_PASS");
  return 0;
 }catch(const heif::Error& e){
  std::printf("HEIF_BACKEND_AB_HEIF_FATAL %s\n",e.get_message().c_str());
  return 2;
 }catch(const std::exception& e){
  std::printf("HEIF_BACKEND_AB_FATAL %s\n",e.what());
  return 1;
 }
}
