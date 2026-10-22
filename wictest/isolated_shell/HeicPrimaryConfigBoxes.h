// Research-only bounded HEIF primary-image hvcC association lookup.
// The known libheif heif_item_get_property_raw_* API refuses hvcC (code 5).
// Decode only fixed repo-owned HEICs in CI. No writes to the HEIC source.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace HeicPrimaryBoxes {
using Bytes=std::vector<uint8_t>;
inline void Need(bool good,const char* message){
 if(!good)throw std::runtime_error(message);
}
inline uint32_t Get(const Bytes& b,size_t pos,unsigned n){
 Need(n>0&&n<=4&&pos<=b.size()&&n<=b.size()-pos,"HEIF integer truncated");
 uint32_t result=0;
 for(unsigned i=0;i<n;++i)result=(result<<8)|b[pos+i];
 return result;
}
inline uint64_t Get64(const Bytes& b,size_t pos){
 return (uint64_t(Get(b,pos,4))<<32)|Get(b,pos+4,4);
}
constexpr uint32_t Meta=0x6d657461, Iprp=0x69707270,
 Ipco=0x6970636f,Ipma=0x69706d61,Hvcc=0x68766343;
struct Box {size_t payload=0,end=0;uint32_t type=0;};
inline Box Parse(const Bytes& b,size_t start,size_t limit){
 Need(start<=limit&&limit<=b.size()&&limit-start>=8,
      "Invalid HEIF box header");
 uint64_t len=Get(b,start,4);const uint32_t type=Get(b,start+4,4);
 size_t header=8;
 if(len==1){
  Need(limit-start>=16,"Extended HEIF box truncated");
  len=Get64(b,start+8);header=16;
 }else if(len==0)len=limit-start;
 Need(len>=header&&len<=limit-start,"HEIF box length overrun");
 return {start+header,start+size_t(len),type};
}
inline std::vector<Box> Children(const Bytes& b,size_t start,size_t end){
 std::vector<Box> output;
 while(start<end){
  Need(output.size()<4096,"HEIF too many child boxes");
  const Box child=Parse(b,start,end);
  Need(child.end>start,"HEIF box cannot be empty");
  output.push_back(child);start=child.end;
 }
 return output;
}
inline Box Only(const std::vector<Box>& boxes,uint32_t type){
 Box found{};unsigned count=0;
 for(const Box& b:boxes)if(b.type==type){found=b;++count;}
 Need(count==1,"Missing or ambiguous required HEIF box");
 return found;
}
inline Bytes AssociatedHvcc(const Bytes& file,uint32_t primaryId){
 Need(primaryId>0,"HEIF primary item ID invalid");
 const Box meta=Only(Children(file,0,file.size()),Meta);
 Need(meta.end-meta.payload>=4,"HEIF meta FullBox too short");
 const Box iprp=Only(Children(file,meta.payload+4,meta.end),Iprp);
 const std::vector<Box> pair=Children(file,iprp.payload,iprp.end);
 const Box ipco=Only(pair,Ipco);
 const Box ipma=Only(pair,Ipma);
 const std::vector<Box> props=Children(file,ipco.payload,ipco.end);
 Need(!props.empty()&&props.size()<128,"HEIF properties count unsafe");
 Need(ipma.end-ipma.payload>=8,"HEIF ipma FullBox truncated");
 const uint32_t full=Get(file,ipma.payload,4);
 const unsigned version=full>>24;
 const bool wide=(full&1u)!=0;
 Need(version<=1,"Unknown HEIF ipma version");
 size_t pos=ipma.payload+4;
 const uint32_t items=Get(file,pos,4);pos+=4;
 Need(items<65536,"Excess HEIF item property records");
 size_t selected=0;unsigned count=0;
 for(uint32_t i=0;i<items;++i){
  const unsigned idBytes=version?4:2;
  Need(pos+idBytes+1<=ipma.end,"HEIF ipma item truncated");
  const uint32_t itemId=Get(file,pos,idBytes);pos+=idBytes;
  const unsigned associationCount=Get(file,pos,1);++pos;
  for(unsigned j=0;j<associationCount;++j){
   const unsigned n=wide?2:1;
   Need(pos+n<=ipma.end,"HEIF ipma association truncated");
   const uint32_t association=Get(file,pos,n);pos+=n;
   const uint32_t index=association&(wide?0x7fff:0x7f);
   Need(index<=props.size(),"HEIF ipma property out of range");
   if(itemId==primaryId&&index&&props[index-1].type==Hvcc){
    selected=index;++count;
   }
  }
 }
 Need(pos==ipma.end&&count==1&&selected>0,
      "Missing/ambiguous primary HEVC hvcC association");
 const Box config=props[selected-1];
 const size_t size=config.end-config.payload;
 Need(size>=23&&size<=65536,"Primary HEVC hvcC size invalid");
 return Bytes(file.begin()+config.payload,file.begin()+config.end);
}
inline Bytes ReadPrimaryHvcc(const std::string& path,uint32_t primaryId){
 std::ifstream stream(path,std::ios::binary|std::ios::ate);
 Need(stream.good(),"HEIF source cannot be read");
 const auto length=stream.tellg();
 Need(length>0&&length<=128*1024*1024,"HEIF source size unsafe");
 Bytes file(size_t(length),0);
 stream.seekg(0);
 Need(bool(stream.read(reinterpret_cast<char*>(file.data()),
                       std::streamsize(file.size()))),"HEIF source read incomplete");
 return AssociatedHvcc(file,primaryId);
}
} // namespace HeicPrimaryBoxes
