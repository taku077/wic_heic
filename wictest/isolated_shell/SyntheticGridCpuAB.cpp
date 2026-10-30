// GitHub Actions ONLY. Generate artificial HEIF grid; never open user photos.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <libheif/heif.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#pragma comment(lib,"kernel32.lib")
namespace {
constexpr int tw=384,th=384,cols=8,rows=6;
constexpr int width=tw*cols,height=th*rows;
void Require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
void Check(heif_error e,const char* stage){
 if(e.code!=heif_error_Ok) {
  std::printf("GRID_CPU_ERROR stage=%s code=%d sub=%d text=%s\n",
       stage,int(e.code),int(e.subcode),e.message?e.message:"");
  throw std::runtime_error(stage);
 }
}
using Context=std::unique_ptr<heif_context,decltype(&heif_context_free)>;
using Image=std::unique_ptr<heif_image,decltype(&heif_image_release)>;
using Handle=std::unique_ptr<heif_image_handle,decltype(&heif_image_handle_release)>;
using Encoder=std::unique_ptr<heif_encoder,decltype(&heif_encoder_release)>;
uint64_t Ticks(FILETIME f){return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
double CpuMs(){
 FILETIME a{},b{},kernel{},user{};
 Require(GetProcessTimes(GetCurrentProcess(),&a,&b,&kernel,&user)!=0,"GetProcessTimes");
 return double(Ticks(kernel)+Ticks(user))/10000.0;
}
struct Result {double wall;double cpu;};
std::string FilePath(){
 char root[32768]{};
 const DWORD n=GetEnvironmentVariableA("RUNNER_TEMP",root,32768);
 Require(n>0&&n<32000,"RUNNER_TEMP absent");
 char flag[10]{};
 Require(GetEnvironmentVariableA("GITHUB_ACTIONS",flag,10)==4 &&
         std::strcmp(flag,"true")==0,"CI only");
 return std::string(root,n)+"\\wicgrid_artificial_3072x2304_48tiles.heic";
}
void Generate(const std::string& file){
 Require(GetFileAttributesA(file.c_str())==INVALID_FILE_ATTRIBUTES,
         "Refuse to overwrite any fixture");
 Context ctx(heif_context_alloc(),heif_context_free);
 Require(ctx!=nullptr,"context allocation");
 heif_encoder* encoderRaw=nullptr;
 Check(heif_context_get_encoder_for_format(ctx.get(),heif_compression_HEVC,
                                            &encoderRaw),"get encoder");
 Encoder enc(encoderRaw,heif_encoder_release);
 Check(heif_encoder_set_lossy_quality(enc.get(),48),"set quality");
 std::vector<Image> owned;
 std::vector<heif_image*> raw;
 owned.reserve(cols*rows);raw.reserve(cols*rows);
 for(int y=0;y<rows;y++)for(int x=0;x<cols;x++){
  heif_image* img=nullptr;
  Check(heif_image_create(tw,th,heif_colorspace_RGB,
                          heif_chroma_interleaved_RGB,&img),"image create");
  owned.emplace_back(img,heif_image_release);
  Check(heif_image_add_plane(img,heif_channel_interleaved,tw,th,24),"RGB plane");
  size_t stride=0;
  uint8_t* p=heif_image_get_plane2(img,heif_channel_interleaved,&stride);
  Require(p&&stride>=size_t(tw)*3,"RGB stride");
  for(int py=0;py<th;py++)for(int px=0;px<tw;px++){
   int gx=x*tw+px,gy=y*th+py;
   auto* z=p+size_t(py)*stride+size_t(px)*3;
   z[0]=uint8_t((gx/17+gy/71+(gx^gy)/113)%256);
   z[1]=uint8_t((gy/13+gx/93+(gx^gy)/127)%256);
   z[2]=uint8_t(((gx+gy)/19+(gx^gy)/97)%256);
  }
  raw.push_back(img);
 }
 heif_image_handle* gridRaw=nullptr;
 // libheif v1.23.7 API implementation interprets args as (columns,rows)
 // despite the public heif_tiling.h declaration naming them (rows,columns).
 // Pass actual columns first and verify the decoded primary geometry.
 Check(heif_context_encode_grid(ctx.get(),raw.data(),cols,rows,
                                enc.get(),nullptr,&gridRaw),"encode grid");
 Handle grid(gridRaw,heif_image_handle_release);
 Require(grid!=nullptr,"grid handle");
 Check(heif_context_set_primary_image(ctx.get(),grid.get()),"set grid primary");
 Check(heif_context_write_to_file(ctx.get(),file.c_str()),"write grid");
 std::printf("GRID_CPU_ARTIFICIAL_CREATED cols=%d rows=%d tiles=%d width=%d height=%d\n",
             cols,rows,cols*rows,width,height);
}
Result Decode(const std::string& path,int tileThreads,
              std::vector<uint8_t>& reference){
 Context ctx(heif_context_alloc(),heif_context_free);
 Require(ctx!=nullptr,"decode context");
 heif_context_set_max_decoding_threads(ctx.get(),tileThreads);
 Require(heif_context_get_max_decoding_threads(ctx.get())==tileThreads,
         "tile thread option ignored");
 Check(heif_context_read_from_file(ctx.get(),path.c_str(),nullptr),"open grid");
 heif_image_handle* nativeHandle=nullptr;
 Check(heif_context_get_primary_image_handle(ctx.get(),&nativeHandle),"get primary");
 Handle handle(nativeHandle,heif_image_handle_release);
 const int gotW=heif_image_handle_get_width(handle.get());
 const int gotH=heif_image_handle_get_height(handle.get());
 std::printf("GRID_CPU_PRIMARY_DIM width=%d height=%d expected=%dx%d\n",
             gotW,gotH,width,height);
 Require(gotW==width && gotH==height,"Unexpected grid size");
 heif_decoding_options* opt=heif_decoding_options_alloc();
 Require(opt!=nullptr,"decoder options");
 // Codec thread count fixed at one: isolate the grid tile parallelism.
 opt->num_codec_threads=1;
 LARGE_INTEGER freq{},begin{},end{};
 Require(QueryPerformanceFrequency(&freq)!=0,"QPC frequency");
 const double c0=CpuMs();
 QueryPerformanceCounter(&begin);
 heif_image* output=nullptr;
 const heif_error err=heif_decode_image(handle.get(),&output,
                          heif_colorspace_RGB,heif_chroma_interleaved_RGB,opt);
 heif_decoding_options_free(opt);
 Check(err,"decode tiled HEIC");
 Image img(output,heif_image_release);
 Require(img!=nullptr,"no decoded grid");
 size_t stride=0;
 const uint8_t* p=heif_image_get_plane_readonly2(img.get(),
                                  heif_channel_interleaved,&stride);
 Require(p&&stride>=size_t(width)*3,"decoded grid stride");
 std::vector<uint8_t> pixels(size_t(width)*height*3);
 for(int y=0;y<height;y++)
   std::memcpy(pixels.data()+size_t(y)*width*3,
               p+size_t(y)*stride,size_t(width)*3);
 QueryPerformanceCounter(&end);
 const double c1=CpuMs();
 if(reference.empty())reference=std::move(pixels);
 else Require(reference==pixels,"GRID_CPU_PIXEL_MISMATCH");
 const double wall=1000.0*double(end.QuadPart-begin.QuadPart)/double(freq.QuadPart);
 Require(wall>0&&c1>=c0,"invalid clock");
 std::printf("GRID_CPU_AB_SAMPLE tile_threads=%d codec_threads=1 wall_ms=%.2f "
             "process_cpu_ms=%.2f cpu_per_wall=%.3f rgb_exact=1\n",
             tileThreads,wall,c1-c0,(c1-c0)/wall);
 return {wall,c1-c0};
}
}
int main(){
 std::string path;
 try{
  path=FilePath();
  Generate(path);
  std::vector<uint8_t> reference;
  constexpr std::array<int,4> modes{{4,2,1,0}};
  std::array<std::vector<Result>,4> results;
  for(int round=0;round<3;round++){
   for(int j=0;j<4;j++){
    const int i=(round+j)%4;
    results[i].push_back(Decode(path,modes[i],reference));
   }
  }
  for(size_t i=0;i<modes.size();i++){
   auto list=results[i];
   Require(list.size()==3,"incomplete grid timings");
   std::vector<double> wall,cpu;
   for(const auto& x:list){wall.push_back(x.wall);cpu.push_back(x.cpu);}
   std::sort(wall.begin(),wall.end());
   std::sort(cpu.begin(),cpu.end());
   std::printf("GRID_CPU_AB_MEDIAN tile_threads=%d codec_threads=1 "
               "wall_ms=%.2f process_cpu_ms=%.2f cpu_per_wall=%.3f "
               "samples=3 rgb_exact=1\n",
               modes[i],wall[1],cpu[1],cpu[1]/wall[1]);
  }
  Require(DeleteFileA(path.c_str())!=0,"fixture cleanup failed");
  std::puts("GRID_CPU_AB_RGB_PARITY_PASS tiles=48 original_files_touched=0");
  return 0;
 }catch(const std::exception& e){
  std::printf("GRID_CPU_AB_FAIL reason=%s\n",e.what());
  if(!path.empty())DeleteFileA(path.c_str());
  return 1;
 }
}
