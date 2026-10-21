// CI-ONLY proof of concept: obtain original HEVC image bytes through libheif
// and rebuild an Annex B elementary bitstream without ffmpeg.exe extraction.
// Parses only HEIF meta/iprp/ipco/ipma; rejects ambiguous configurations.
// Original HEIC is read-only. Output only inside RUNNER_TEMP.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <libheif/heif.h>
#include <libheif/heif_context.h>
#include <libheif/heif_items.h>
#include <cstdio>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#pragma comment(lib,"heif.lib")
#pragma comment(lib,"libde265.lib")
#pragma comment(lib,"libkvazaar.lib")
namespace {
using Bytes=std::vector<uint8_t>;
void Require(bool good,const char* reason) {
 if(!good){std::printf("HEIC_NATIVE_ANNEXB_REJECT reason=%s\n",reason);throw std::runtime_error(reason);}
}
std::wstring Env(const wchar_t* name){
 DWORD length=GetEnvironmentVariableW(name,nullptr,0);
 if(!length||length>32000)return {};
 std::wstring s(length,L'\0');
 DWORD got=GetEnvironmentVariableW(name,&s[0],length);
 if(!got||got>=length)return {};
 s.resize(got);return s;
}
std::string Ascii(const std::wstring& w){
 std::string s;for(wchar_t ch:w){Require(ch>0&&ch<128,"non ASCII CI path");s.push_back(char(ch));}return s;
}
uint32_t Read(const Bytes& b,size_t p,unsigned n) {
 Require(n>0&&n<=4&&p<=b.size()&&n<=b.size()-p,"truncated unsigned integer");
 uint32_t result=0;for(unsigned i=0;i<n;++i)result=(result<<8)|b[p+i];return result;
}
uint64_t Read64(const Bytes& b,size_t p){
 const uint64_t hi=Read(b,p,4),lo=Read(b,p+4,4);return (hi<<32)|lo;
}
uint32_t Four(const char* p){
 return uint32_t(uint8_t(p[0]))<<24 | uint32_t(uint8_t(p[1]))<<16 |
        uint32_t(uint8_t(p[2]))<<8 | uint32_t(uint8_t(p[3]));
}
constexpr uint32_t TAG_META=0x6d657461, TAG_IPRP=0x69707270,
 TAG_IPCO=0x6970636f,TAG_IPMA=0x69706d61,TAG_HVCC=0x68766343;
struct Box{size_t start=0,content=0,end=0;uint32_t type=0;};
Box Parse(const Bytes& b,size_t start,size_t limit){
 Require(start<=limit&&limit<=b.size()&&limit-start>=8,"invalid ISOBMFF box offset");
 const uint32_t length=Read(b,start,4);
 const uint32_t type=Read(b,start+4,4);
 size_t header=8;
 uint64_t total=length;
 if(length==1){Require(limit-start>=16,"large box truncated");total=Read64(b,start+8);header=16;}
 if(length==0)total=uint64_t(limit-start);
 Require(total>=header&&total<=limit-start,"invalid ISOBMFF box length");
 return {start,start+header,start+size_t(total),type};
}
std::vector<Box> Children(const Bytes& b,size_t start,size_t end){
 std::vector<Box> result;
 while(start<end){
   Require(result.size()<4096,"too many box children");
   const Box box=Parse(b,start,end);result.push_back(box);start=box.end;
 }
 return result;
}
Box Unique(const std::vector<Box>& boxes,uint32_t type) {
 Box result{};int count=0;
 for(const Box& b:boxes){if(b.type==type){result=b;++count;}}
 Require(count==1,"missing or ambiguous HEIF property container");
 return result;
}
struct HvcC {Bytes contents;unsigned lenSize=0;};
HvcC FindPrimaryConfig(const Bytes& b,uint32_t primaryId){
 const Box meta=Unique(Children(b,0,b.size()),TAG_META);
 Require(meta.end-meta.content>=4,"truncated meta FullBox");
 const Box iprp=Unique(Children(b,meta.content+4,meta.end),TAG_IPRP);
 const auto list=Children(b,iprp.content,iprp.end);
 const Box ipco=Unique(list,TAG_IPCO);
 const Box ipma=Unique(list,TAG_IPMA);
 const auto properties=Children(b,ipco.content,ipco.end);
 Require(!properties.empty()&&properties.size()<128,"unexpected item property count");
 std::vector<unsigned> hvcIndexes;
 for(size_t index=0;index<properties.size();++index){
   if(properties[index].type==TAG_HVCC)hvcIndexes.push_back(unsigned(index)+1);
 }
 Require(!hvcIndexes.empty(),"HEIC hvcC property absent");
 Require(ipma.end-ipma.content>=8,"truncated property association FullBox");
 const uint32_t full=Read(b,ipma.content,4);
 const unsigned version=full>>24;
 const bool large=(full & 1u)!=0;
 Require(version<=1,"unsupported ipma version");
 size_t pos=ipma.content+4;
 const uint32_t itemCount=Read(b,pos,4);pos+=4;
 Require(itemCount<65536,"too many property item associations");
 unsigned matching=0;
 unsigned selected=0;
 for(uint32_t item=0;item<itemCount;++item){
   const unsigned idSize=version?4:2;
   Require(pos+idSize+1<=ipma.end,"truncated ipma item header");
   const uint32_t id=Read(b,pos,idSize);pos+=idSize;
   const uint32_t associations=Read(b,pos,1);++pos;
   for(uint32_t assoc=0;assoc<associations;++assoc){
     const unsigned bytes=large?2:1;
     Require(pos+bytes<=ipma.end,"truncated ipma property reference");
     const uint32_t raw=Read(b,pos,bytes);pos+=bytes;
     const unsigned propertyIndex=raw & (large?0x7FFF:0x7F);
     Require(propertyIndex<=properties.size(),"invalid ipma property index");
     if(id==primaryId && propertyIndex &&
        properties[propertyIndex-1].type==TAG_HVCC){
       selected=propertyIndex;
       ++matching;
     }
   }
 }
 Require(matching==1 && selected>0,"primary HEIC hvcC property association missing or ambiguous");
 const auto& config=properties[selected-1];
 HvcC result;
 result.contents.assign(b.begin()+config.content,b.begin()+config.end);
 Require(result.contents.size()>=23 && result.contents[0]==1,"unsupported HEVC decoder configuration");
 result.lenSize=(result.contents[21]&3u)+1u;
 Require(result.lenSize==1||result.lenSize==2||result.lenSize==4,"unsupported HEVC NAL length size");
 return result;
}
void AnnexNal(Bytes& output,const uint8_t* nal,size_t bytes){
 Require(bytes>1 && bytes<100*1024*1024,"invalid HEVC NAL length");
 Require(output.size()+bytes+4<=128*1024*1024,"Annex B bounded output exceeded");
 output.insert(output.end(),{0,0,0,1});
 output.insert(output.end(),nal,nal+bytes);
}
void EncodeConfig(const HvcC& config,Bytes& out,unsigned& nals){
 const Bytes& b=config.contents;
 const unsigned numArrays=b[22];
 size_t pos=23;nals=0;
 Require(numArrays>0 && numArrays<=64,"invalid hvcC array count");
 for(unsigned array=0;array<numArrays;++array){
   Require(pos+3<=b.size(),"hvcC array truncated");
   const unsigned nalType=b[pos]&63u;++pos;
   const unsigned count=Read(b,pos,2);pos+=2;
   Require(count<4096,"too many hvcC parameter NAL units");
   for(unsigned i=0;i<count;++i){
     Require(pos+2<=b.size(),"hvcC parameter length truncated");
     const unsigned n=Read(b,pos,2);pos+=2;
     Require(n>1&&pos+n<=b.size(),"hvcC parameter payload truncated");
     // Parameter sets 32/33/34 are enough for HEVC Main decode; preserve
     // any additional configuration NALs supplied by this item's hvcC.
     if(nalType==32||nalType==33||nalType==34)++nals;
     AnnexNal(out,&b[pos],n);pos+=n;
   }
 }
 Require(pos==b.size(),"unexpected HEVC config trailing bytes");
 Require(nals>=3,"missing VPS/SPS/PPS required for a standalone HEVC stream");
}
void EncodeImage(const Bytes& item,unsigned bytes,Bytes& out,unsigned& nals){
 size_t p=0;nals=0;
 while(p<item.size()){
   Require(++nals<=65536,"too many picture NAL units");
   const uint32_t n=Read(item,p,bytes);p+=bytes;
   Require(n>1&&n<=item.size()-p,"invalid HEVC item NAL length");
   AnnexNal(out,&item[p],n);p+=n;
 }
 Require(nals>0 && p==item.size(),"empty HEVC item stream");
}
// New independent malformed-input tests. No files, GPU, or registry needed.
void RunAnnexBMalformedSelfTests() {
 auto rejectPicture=[](const char* name,const Bytes& bytes,unsigned prefix){
   Bytes output;unsigned nalCount=0;bool refused=false;
   try{EncodeImage(bytes,prefix,output,nalCount);}
   catch(const std::runtime_error&){refused=true;}
   if(!refused)throw std::runtime_error("Malformed picture accepted");
   std::printf("HEIC_NATIVE_ANNEXB_NEGATIVE_TEST name=%s rejected=1\n",name);
 };
 {
   Bytes output;unsigned count=0;
   EncodeImage(Bytes{0,0,0,2,0x26,0x01},4,output,count);
   Require(count==1 && output==Bytes({0,0,0,1,0x26,0x01}),
           "valid four-byte picture NAL reconstruction failed");
   EncodeImage(Bytes{2,0x26,0x01},1,output,count);
   Require(count==1 && output.size()==12,
           "valid one-byte length prefix reconstruction failed");
 }
 rejectPicture("truncated_length_prefix",Bytes{0,0,0},4);
 rejectPicture("zero_length_nal",Bytes{0,0,0,0},4);
 rejectPicture("single_byte_nal",Bytes{0,0,0,1,0x26},4);
 rejectPicture("picture_nal_exceeds_buffer",Bytes{0,0,0,8,0x26,0x01},4);
 rejectPicture("second_nal_truncated",Bytes{0,0,0,2,0x26,0x01,0,0,0,2,0x26},4);
 {
   HvcC config;config.lenSize=4;config.contents.assign(23,0);
   config.contents[0]=1;config.contents[21]=3;config.contents[22]=3;
   for(const unsigned type:{32u,33u,34u}) {
     config.contents.push_back(uint8_t(type)); // hvcC NAL array type
     config.contents.insert(config.contents.end(),{0,1,0,2,uint8_t(type<<1),1});
   }
   Bytes output;unsigned nalCount=0;
   EncodeConfig(config,output,nalCount);
   Require(nalCount==3 && output.size()==18,
           "valid synthetic VPS/SPS/PPS config failed");
   config.contents.pop_back();
   bool refused=false;
   try{Bytes broken;unsigned n=0;EncodeConfig(config,broken,n);}
   catch(const std::runtime_error&){refused=true;}
   Require(refused,"truncated hvcC NAL payload was accepted");
   std::puts("HEIC_NATIVE_ANNEXB_NEGATIVE_TEST name=truncated_hvcc_payload rejected=1");
 }
 {
   const Bytes overlong{0,0,0,100,'m','e','t','a'};
   bool refused=false;
   try{Parse(overlong,0,overlong.size());}
   catch(const std::runtime_error&){refused=true;}
   Require(refused,"ISOBMFF box exceeding input accepted");
   std::puts("HEIC_NATIVE_ANNEXB_NEGATIVE_TEST name=overlong_box rejected=1");
 }
 std::puts("HEIC_NATIVE_ANNEXB_MALFORMED_BOUNDARY_SELFTESTS_PASSED");
}
}
int wmain(int argc,wchar_t** argv) {
 try{
  Require(Env(L"GITHUB_ACTIONS")==L"true","CI only");
  if(argc==2 && std::wstring(argv[1])==L"--selftest"){
    RunAnnexBMalformedSelfTests();
    return 0;
  }
  Require(argc==3,"usage: pinned repo sample HEIC / runner temp resulting HEVC");
  const std::wstring original=argv[1],target=argv[2];
  const auto root=Env(L"GITHUB_WORKSPACE");
  const auto temp=Env(L"RUNNER_TEMP");
  Require(!root.empty()&&!temp.empty(),"CI paths missing");
  const std::wstring prefix=root+L"\\wictest\\samples\\";
  const std::wstring generated=temp+L"\\wicfast_synthetic_4032x3024.heic";
  const bool pinned=original.compare(0,prefix.size(),prefix)==0 &&
      original.size()>prefix.size() &&
      original.find(L'\\',prefix.size())==std::wstring::npos;
  Require(pinned||original==generated,"not a permitted CI HEIC fixture");
  const std::wstring outputPrefix=temp+L"\\heic_native_bridge\\";
  Require(target.compare(0,outputPrefix.size(),outputPrefix)==0 &&
     target.size()>outputPrefix.size() &&
     target.find(L'\\',outputPrefix.size())==std::wstring::npos,
     "output must be flat under isolated RUNNER_TEMP subfolder");
  Require(GetFileAttributesW(original.c_str())!=INVALID_FILE_ATTRIBUTES,"input absent");
  Require(GetFileAttributesW(target.c_str())==INVALID_FILE_ATTRIBUTES,"refusing existing output");
  std::ifstream input(Ascii(original),std::ios::binary|std::ios::ate);
  Require(input.good(),"HEIC input cannot open");
  const auto fileSize=input.tellg();
  Require(fileSize>0 && fileSize<=128*1024*1024,"unsafe HEIC input size");
  Bytes file(size_t(fileSize),0);
  input.seekg(0);
  Require(bool(input.read(reinterpret_cast<char*>(file.data()),std::streamsize(file.size()))),"HEIC cannot read");
  heif_context* ctx=heif_context_alloc();
  Require(ctx!=nullptr,"libheif context allocation failed");
  const heif_error loaded=heif_context_read_from_file(ctx,Ascii(original).c_str(),nullptr);
  Require(loaded.code==heif_error_Ok,"HEIC libheif parse failed");
  heif_item_id primary=0;
  const heif_error iderr=heif_context_get_primary_image_ID(ctx,&primary);
  Require(iderr.code==heif_error_Ok&&primary>0,"HEIC primary item id absent");
  const HvcC hvcc=FindPrimaryConfig(file,primary);
  uint8_t* data=nullptr;size_t bytes=0;
  heif_metadata_compression compression{};
  const heif_error read=heif_item_get_item_data(ctx,primary,&compression,&data,&bytes);
  Require(read.code==heif_error_Ok&&data&&bytes>4,"libheif primary encoded data absent");
  Require(bytes<128*1024*1024,"compressed image exceeds safe bounds");
  const Bytes item(data,data+bytes);
  heif_release_item_data(ctx,&data);
  heif_context_free(ctx);
  Bytes elementary;
  unsigned configNALs=0,pictureNALs=0;
  EncodeConfig(hvcc,elementary,configNALs);
  EncodeImage(item,hvcc.lenSize,elementary,pictureNALs);
  std::ofstream output(Ascii(target),std::ios::binary|std::ios::out|std::ios::trunc);
  Require(output.good(),"cannot create synthetic output");
  output.write(reinterpret_cast<const char*>(elementary.data()),std::streamsize(elementary.size()));
  Require(output.good(),"cannot write Annex B output");
  output.close();
  std::printf("HEIC_NATIVE_ANNEXB_PASS item=%u configNals=%u pictureNals=%u sourceBytes=%llu outputBytes=%llu prefixBytes=%u\n",
    unsigned(primary),configNALs,pictureNALs,
    static_cast<unsigned long long>(item.size()),
    static_cast<unsigned long long>(elementary.size()),hvcc.lenSize);
  return 0;
 }catch(const std::exception& e){
  std::printf("HEIC_NATIVE_ANNEXB_ERROR %s\n",e.what());
  return 1;
 }
}
