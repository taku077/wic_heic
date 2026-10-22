// Research only: bounded hvcC configuration profile / actual SPS crosscheck.
// Used for an HEIC primary item's own associated hvcC, NOT a filename match.
// Never mutates HEIC, installs a codec, or claims a GPU frame was decoded.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "HevcAnnexBSpsProfile.h"
namespace HeicHvcc {
struct Result {
 int profile=-1;
 int spsProfile=-1;
 unsigned spsCount=0;
 bool valid=false;
};
inline Result Inspect(const std::vector<uint8_t>& config){
 Result r;
 if(config.size()<23 || config.size()>65536 || config[0]!=1)return r;
 // General profile_space (top 2 bits) other than zero: never assume GPU.
 if((config[1]&0xC0u)!=0)return r;
 const int profile=int(config[1]&31u);
 if(profile!=1&&profile!=2&&profile!=4)return r;
 // lengthSizeMinusOne==2 is reserved per HEVCDecoderConfigurationRecord.
 if((config[21]&3u)==2u)return r;
 const unsigned arrays=config[22];
 if(arrays==0||arrays>64)return r;
 std::vector<uint8_t> annexB;
 size_t pos=23;
 for(unsigned i=0;i<arrays;++i){
  if(pos+3>config.size())return r;
  const unsigned type=config[pos]&63u;
  ++pos;
  const unsigned count=(unsigned(config[pos])<<8)|unsigned(config[pos+1]);
  pos+=2;
  if(count>4096)return r;
  for(unsigned j=0;j<count;++j){
   if(pos+2>config.size())return r;
   const unsigned n=(unsigned(config[pos])<<8)|unsigned(config[pos+1]);
   pos+=2;
   if(n<2||n>config.size()-pos)return r;
   // SPS array only. Reject invalid HEVC NAL header or mismatched type.
   if(type==33){
    if(n<7 || ((config[pos]>>1)&63u)!=33)return r;
    if(annexB.size()+4+n>65536)return r;
    annexB.insert(annexB.end(),{0,0,0,1});
    annexB.insert(annexB.end(),config.begin()+pos,config.begin()+pos+n);
   }
   pos+=n;
  }
 }
 if(pos!=config.size())return r;
 const HevcSps::ProfileProbe sps=HevcSps::ReadAnnexBSpsProfile(annexB);
 if(sps.malformed || sps.spsCount<=0 || sps.generalProfileIdc!=profile)return r;
 r.profile=profile;
 r.spsProfile=sps.generalProfileIdc;
 r.spsCount=unsigned(sps.spsCount);
 r.valid=true;
 return r;
}
inline std::vector<uint8_t> TestConfig(unsigned profile){
 // One minimal (SPS-shaped) config for the bounded header parser unit test,
 // not a real decodable HEVC file or permission to route hardware.
 std::vector<uint8_t> bytes(23,0);
 bytes[0]=1;bytes[1]=uint8_t(profile&31u);bytes[21]=3;bytes[22]=1;
 bytes.insert(bytes.end(),{0xA1,0,1,0,8,0x42,1,1,
     uint8_t(profile&31u),0x51,0x12,0x34,0x56});
 return bytes;
}
inline bool Selftest(){
 for(unsigned p:{1u,2u,4u}){
  const auto config=TestConfig(p);
  const auto got=Inspect(config);
  if(!got.valid||got.profile!=int(p)||got.spsProfile!=int(p)||got.spsCount!=1)return false;
  auto mismatch=config;
  mismatch[1]=1; // Main in hvcC disagrees with SPS for p=2 or p=4
  if(p!=1&&Inspect(mismatch).valid)return false;
  auto malformed=config;
  malformed.pop_back();
  if(Inspect(malformed).valid)return false;
  malformed=config;malformed[21]=2;
  if(Inspect(malformed).valid)return false;
  malformed=config;malformed[1]|=0x40;
  if(Inspect(malformed).valid)return false;
  malformed=config;malformed[22]=2;
  if(Inspect(malformed).valid)return false;
 }
 return true;
}
} // namespace HeicHvcc
