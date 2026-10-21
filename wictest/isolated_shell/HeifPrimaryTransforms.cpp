// Research-only read-only HEIF primary item property analyzer.
// Uses library-level ordered transform associations, not filenames or EXIF guesses.
// CI accepts exactly seven repository-owned HEIC images. No decoding required.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <libheif/heif.h>
#include <libheif/heif_context.h>
#include <libheif/heif_properties.h>
#include <libheif/heif_items.h>
#pragma comment(lib,"heif.lib")
#pragma comment(lib,"libde265.lib")
#pragma comment(lib,"libkvazaar.lib")
namespace {
void Require(bool ok,const char* reason){
 if(!ok)throw std::runtime_error(reason);
}
std::wstring Env(const wchar_t* name){
 DWORD capacity=GetEnvironmentVariableW(name,nullptr,0);
 if(!capacity||capacity>32000)return {};
 std::wstring s(capacity,L'\0');
 DWORD size=GetEnvironmentVariableW(name,&s[0],capacity);
 if(!size||size>=capacity)return {};
 s.resize(size);return s;
}
std::string Ascii(const std::wstring& s){
 std::string output;
 for(wchar_t character:s){
  Require(character>0&&character<=127,"CI path must be ASCII");
  output.push_back(char(character));
 }
 return output;
}
}
int wmain(){
 try{
  Require(Env(L"GITHUB_ACTIONS")==L"true","CI only");
  std::wstring root=Env(L"GITHUB_WORKSPACE");
  Require(!root.empty(),"CI root unavailable");
  const wchar_t* names[]={
    L"03.heic",L"04.heic",L"05.heic",L"06.heic",L"07.heic",
    L"fixture-irot3-exif6-noaux.heic",L"fixture-irot3-exif6.heic"
  };
  int total=0,withRotation=0,withMirror=0,withCrop=0;
  for(const wchar_t* name:names){
    std::wstring path=root+L"\\wictest\\samples\\"+name;
    Require(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES,"missing pinned sample");
    heif_context* ctx=heif_context_alloc();
    Require(ctx!=nullptr,"heif context allocation failed");
    const auto cpath=Ascii(path);
    const heif_error loaded=heif_context_read_from_file(ctx,cpath.c_str(),nullptr);
    if(loaded.code!=heif_error_Ok){
      std::printf("HEIC_PROPERTY_ERROR item=%ls message=%s\n",name,loaded.message?loaded.message:"");
      heif_context_free(ctx);
      throw std::runtime_error("HEIC read failed");
    }
    heif_item_id id=0;
    const heif_error primary=heif_context_get_primary_image_ID(ctx,&id);
    if(primary.code!=heif_error_Ok || !id){
      heif_context_free(ctx);throw std::runtime_error("HEIF primary id missing");
    }
    // Research next-step: can libheif expose the compressed HEVC primary item
    // without an ffmpeg.exe subprocess? Inspect only, do not write bytes.
    uint8_t* raw=nullptr;
    size_t itemBytes=0;
    heif_metadata_compression compression{};
    const heif_error itemResult=heif_item_get_item_data(ctx,id,&compression,&raw,&itemBytes);
    if(itemResult.code==heif_error_Ok && raw && itemBytes>=4) {
      const uint32_t firstNAL=(uint32_t(raw[0])<<24)|(uint32_t(raw[1])<<16)|
                               (uint32_t(raw[2])<<8)|raw[3];
      const bool prefixLikely=firstNAL>0 && size_t(firstNAL)+4<=itemBytes;
      std::printf("HEIC_PRIMARY_RAW_ITEM file=%ls dataBytes=%llu firstNAL_length=%u lengthPrefixPlausible=%d compression=%d\n",
        name,static_cast<unsigned long long>(itemBytes),firstNAL,int(prefixLikely),int(compression));
    }else{
      std::printf("HEIC_PRIMARY_RAW_ITEM_UNAVAILABLE file=%ls code=%d message=%s\n",
        name,int(itemResult.code),itemResult.message?itemResult.message:"unknown");
    }
    if(raw)heif_release_item_data(ctx,&raw);
    const int configCount=heif_item_get_properties_of_type(ctx,id,
      static_cast<heif_item_property_type>(heif_fourcc('h','v','c','C')),nullptr,0);
    std::printf("HEIC_PRIMARY_CODEC_CONFIGURATION file=%ls hvcC_property_count=%d\n",name,configCount);
    if(configCount>0 && configCount<4){
      std::vector<heif_property_id> configIds(size_t(configCount),0);
      const int actual=heif_item_get_properties_of_type(ctx,id,
        static_cast<heif_item_property_type>(heif_fourcc('h','v','c','C')),
        configIds.data(),configCount);
      for(int entry=0;entry<actual;++entry){
        size_t bytes=0;
        const heif_error sizeResult=heif_item_get_property_raw_size(ctx,id,configIds[size_t(entry)],&bytes);
        std::printf("HEIC_PRIMARY_HVCC file=%ls property=%u code=%d payloadBytes=%llu\n",
          name,unsigned(configIds[size_t(entry)]),int(sizeResult.code),
          static_cast<unsigned long long>(bytes));
      }
    }
    const int count=heif_item_get_transformation_properties(ctx,id,nullptr,0);
    Require(count>=0 && count<64,"bad count of primary image transforms");
    std::vector<heif_property_id> props(size_t(count),0);
    const int fetched=count?heif_item_get_transformation_properties(ctx,id,props.data(),count):0;
    Require(fetched==count,"inconsistent HEIF transform list");
    int irot=0,imir=0,clap=0,lastCcw=-1;
    for(int index=0;index<fetched;++index){
      const auto prop=props[size_t(index)];
      const auto type=heif_item_get_property_type(ctx,id,prop);
      if(type==heif_item_property_type_transform_rotation){
        const int ccw=heif_item_get_property_transform_rotation_ccw(ctx,id,prop);
        Require(ccw==0||ccw==90||ccw==180||ccw==270,"invalid HEIF irot angle");
        irot++;lastCcw=ccw;
        std::printf("HEIC_PROPERTY_STEP file=%ls order=%d type=irot ccw=%d property=%u\n",
          name,index,ccw,unsigned(prop));
      } else if(type==heif_item_property_type_transform_mirror){
        const auto dir=heif_item_get_property_transform_mirror(ctx,id,prop);
        imir++;
        std::printf("HEIC_PROPERTY_STEP file=%ls order=%d type=imir direction=%d property=%u\n",
          name,index,int(dir),unsigned(prop));
      } else if(type==heif_item_property_type_transform_crop){
        clap++;
        std::printf("HEIC_PROPERTY_STEP file=%ls order=%d type=clap property=%u\n",
          name,index,unsigned(prop));
      } else {
        throw std::runtime_error("Unknown ordered transformation - cannot safely route GPU");
      }
    }
    if(irot)withRotation++;
    if(imir)withMirror++;
    if(clap)withCrop++;
    const bool safeOrientation=(clap==0 && imir==0 && irot<=1);
    std::printf("HEIC_PRIMARY_TRANSFORM file=%ls itemId=%u total=%d rotations=%d mirror=%d crops=%d ccw=%d conservative_supported=%d\n",
      name,unsigned(id),fetched,irot,imir,clap,lastCcw,int(safeOrientation));
    heif_context_free(ctx);
    total++;
  }
  std::printf("HEIC_PRIMARY_TRANSFORM_SUMMARY total=%d with_irot=%d with_imir=%d with_clap=%d\n",
    total,withRotation,withMirror,withCrop);
  Require(total==7,"incomplete pinned HEIC corpus");
  std::puts("HEIC_PRIMARY_TRANSFORM_READONLY_PASS");
  return 0;
 }catch(const std::exception& e){
  std::printf("HEIC_PRIMARY_TRANSFORM_FATAL %s\n",e.what());
  return 1;
 }
}
