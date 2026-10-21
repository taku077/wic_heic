// Disposable synthetic-HEIC fixture writer. NEVER accepts user image paths.
// Builds identical primary compressed image both with/without embedded thumbnail.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#define LIBHEIF_STATIC_BUILD
#define LIBDE265_STATIC_BUILD
#define KVZ_STATIC_LIB
#include <libheif/heif_cxx.h>
#pragma comment(lib,"heif.lib")
#pragma comment(lib,"libde265.lib")
#pragma comment(lib,"libkvazaar.lib")
namespace {
std::wstring Env(const wchar_t* name){
 DWORD n=GetEnvironmentVariableW(name,nullptr,0);
 if(n<2||n>32000)return {};
 std::wstring s(n,L'\0');
 DWORD read=GetEnvironmentVariableW(name,&s[0],n);
 if(!read||read>=n)return {};
 s.resize(read);return s;
}
void GuardPath(const std::wstring& path,const std::wstring& expected,bool input=false){
 if(Env(L"GITHUB_ACTIONS")!=L"true")throw std::runtime_error("CI ONLY");
 const std::wstring root=Env(L"RUNNER_TEMP");
 if(root.empty()||path!=root+L"\\"+expected)throw std::runtime_error("NOT SYNTHETIC RUNNER PATH");
 const bool exists=GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;
 if(input && !exists)throw std::runtime_error("SYNTHETIC INPUT MISSING");
 if(!input && exists)throw std::runtime_error("WOULD OVERWRITE FILE");
}
std::string NarrowAscii(const std::wstring& text) {
 std::string out;
 for(wchar_t ch:text){
   if(ch>127)throw std::runtime_error("TEMP PATH ASCII ONLY");
   out.push_back(static_cast<char>(ch));
 }
 return out;
}
}
int wmain(int argc,wchar_t** argv) {
 try {
  if(argc!=4)throw std::runtime_error("expected original_synthetic.heic main_only.heic with_thumb.heic");
  const std::wstring input=argv[1],without=argv[2],with=argv[3];
  GuardPath(input,L"wicfast_synthetic_4032x3024.heic",true);
  GuardPath(without,L"wicfast_baseline_4032x3024.heic");
  GuardPath(with,L"wicfast_embedded_4032x3024.heic");
  heif::Context original;
  original.read_from_file(NarrowAscii(input));
  heif::ImageHandle source=original.get_primary_image_handle();
  if(source.get_width()!=4032 || source.get_height()!=3024)throw std::runtime_error("bad synthetic primary size");
  heif::Image image=source.decode_image(heif_colorspace_RGB,heif_chroma_interleaved_RGB);
  heif::Context generated;
  heif::Encoder encoder(heif_compression_HEVC);
  encoder.set_lossy_quality(85);
  heif::ImageHandle main=generated.encode_image(image,encoder);
  generated.write_to_file(NarrowAscii(without));
  heif::Context::EncodingOptions options;
  heif::ImageHandle thumb=generated.encode_thumbnail(image,main,encoder,options,512);
  if(thumb.empty()||thumb.get_width()<500||thumb.get_height()<370)
     throw std::runtime_error("failed to attach >=500px embedded thumbnail");
  generated.write_to_file(NarrowAscii(with));
  std::printf("EMBEDDED_FIXTURES_PASSED primary=%dx%d thumb=%dx%d\n",
   main.get_width(),main.get_height(),thumb.get_width(),thumb.get_height());
  return 0;
 }catch(const heif::Error& e){
  std::printf("EMBEDDED_FIXTURES_HEIF_FAILED %s\n",e.get_message().c_str());
  return 2;
 }catch(const std::exception& e){
  std::printf("EMBEDDED_FIXTURES_FAILED %s\n",e.what());
  return 3;
 }
}
