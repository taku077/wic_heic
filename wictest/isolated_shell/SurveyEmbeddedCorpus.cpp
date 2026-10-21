// Metadata-only census of repository-owned HEIC fixtures and two generated CI HEICs.
// This executable never reads arbitrary paths or private user image files.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#define LIBHEIF_STATIC_BUILD
#include <libheif/heif_cxx.h>
#pragma comment(lib,"heif.lib")
#pragma comment(lib,"libde265.lib")
#pragma comment(lib,"libkvazaar.lib")
namespace {
std::wstring Env(const wchar_t* name){
 DWORD count=GetEnvironmentVariableW(name,nullptr,0);
 if(!count || count>32000)return {};
 std::wstring s(count,L'\0');
 DWORD actual=GetEnvironmentVariableW(name,&s[0],count);
 if(!actual||actual>=count)return {};
 s.resize(actual);return s;
}
void Require(bool ok,const char* what){
 if(!ok)throw std::runtime_error(what);
}
std::string Ascii(const std::wstring& s){
 std::string t;
 for(wchar_t ch:s){
  if(ch<0 || ch>127)throw std::runtime_error("CI paths must be ASCII");
  t.push_back(char(ch));
 }
 return t;
}
void Read(const std::wstring& filepath,const char* label,int& total,
          int& embedded,int& enoughAt256,int& anyExif){
 if(GetFileAttributesW(filepath.c_str())==INVALID_FILE_ATTRIBUTES)
   throw std::runtime_error("repo-pinned input not found");
 heif::Context context;
 context.read_from_file(Ascii(filepath));
 heif::ImageHandle master=context.get_primary_image_handle();
 if(master.empty())throw std::runtime_error("primary image absent");
 const auto th=master.get_list_of_thumbnail_IDs();
 const int w=master.get_width(),h=master.get_height();
 const int luma=master.get_luma_bits_per_pixel();
 const int chroma=master.get_chroma_bits_per_pixel();
 const auto metadata=master.get_list_of_metadata_block_IDs("Exif");
 const heif_image_handle* handle=master.get_raw_image_handle();
 const size_t rawICC=heif_image_handle_get_raw_color_profile_size(handle);
 const auto color=heif_image_handle_get_color_profile_type(handle);
 int max=0,firstW=0,firstH=0;
 bool eligible=false;
 const int side=(w>h)?w:h;
 const int tw=side>0?int(uint64_t(w)*256u/unsigned(side)):0;
 const int targetH=side>0?int(uint64_t(h)*256u/unsigned(side)):0;
 for(auto id:th){
   heif::ImageHandle sub=master.get_thumbnail(id);
   if(sub.empty())continue;
   const int sw=sub.get_width(),sh=sub.get_height();
   if(!firstW){firstW=sw;firstH=sh;}
   if(sw>max)max=sw;
   const uint64_t lhs=uint64_t(sw)*uint64_t(h);
   const uint64_t rhs=uint64_t(sh)*uint64_t(w);
   const uint64_t delta=(lhs>rhs)?lhs-rhs:rhs-lhs;
   if(sw>=tw&&sh>=targetH&&
      sw<=w&&sh<=h&&w>0&&h>0&&
      delta*100ull<=uint64_t(w)*uint64_t(sh))eligible=true;
 }
 ++total;
 if(!th.empty())++embedded;
 if(eligible)++enoughAt256;
 if(!metadata.empty())++anyExif;
 std::printf("HEIC_CORPUS label=%s width=%d height=%d ispe=%dx%d bpp=%d/%d alpha=%d thumbs=%u first=%dx%d maxW=%d usable256=%d exif=%u rawICCbytes=%llu profileKind=%d\n",
  label,w,h,master.get_ispe_width(),master.get_ispe_height(),luma,chroma,
  int(master.has_alpha_channel()),unsigned(th.size()),firstW,firstH,max,
  int(eligible),unsigned(metadata.size()),static_cast<unsigned long long>(rawICC),
  int(color));
}
}
int wmain(){
 try {
  Require(Env(L"GITHUB_ACTIONS")==L"true","CI ONLY");
  const auto work=Env(L"GITHUB_WORKSPACE");
  const auto temp=Env(L"RUNNER_TEMP");
  Require(!work.empty()&&!temp.empty(),"CI directories absent");
  int total=0,thumbnailFiles=0,usable256=0,exif=0;
  const std::vector<std::wstring> names={
   L"03.heic",L"04.heic",L"05.heic",L"06.heic",L"07.heic",
   L"fixture-irot3-exif6-noaux.heic",L"fixture-irot3-exif6.heic"
  };
  for(const auto& name:names){
    const auto input=work+L"\\wictest\\samples\\"+name;
    Read(input,Ascii(name).c_str(),total,thumbnailFiles,usable256,exif);
  }
  Read(temp+L"\\wicfast_baseline_4032x3024.heic","synthetic_no_thumb",
     total,thumbnailFiles,usable256,exif);
  Read(temp+L"\\wicfast_embedded_4032x3024.heic","synthetic_with_thumb",
     total,thumbnailFiles,usable256,exif);
  std::printf("HEIC_CORPUS_SUMMARY total=%d embedded=%d eligible256=%d exifPresent=%d\n",
      total,thumbnailFiles,usable256,exif);
  std::puts("HEIC_CORPUS_READONLY_PASS");
  return 0;
 }catch(const heif::Error& e){
  std::printf("HEIC_CORPUS_HEIF_FAILED %s\n",e.get_message().c_str());
  return 2;
 }catch(const std::exception& e){
  std::printf("HEIC_CORPUS_FAILED %s\n",e.what());return 1;
 }
}
