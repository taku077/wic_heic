// Read-only, fail-closed HEVC SPS general_profile_idc probe.
// Research-only utility: profile code alone is not sufficient to certify
// a hardware decoder; actual D3D11 frames and correct pixels are required.
// HEVC Annex B NAL units, not HEIC file contents. Does not alter input bytes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace HevcSps {
struct ProfileProbe {
 int generalProfileIdc = -1;
 int spsCount = 0;
 bool malformed = false;
};
inline ProfileProbe ReadAnnexBSpsProfile(const std::vector<uint8_t>& data) {
 ProfileProbe probe;
 for(size_t i=0;i+9<data.size();++i){
  size_t marker=0;
  if(data[i]!=0 || data[i+1]!=0)continue;
  if(data[i+2]==1)marker=3;
  else if(data[i+2]==0&&data[i+3]==1)marker=4;
  if(!marker)continue;
  const size_t n=i+marker;
  if(n+4>=data.size())continue;
  const uint8_t header1=data[n],header2=data[n+1];
  if((header1&0x80) || (header2&0x07)==0){probe.malformed=true;continue;}
  const int type=(header1>>1)&0x3f;
  if(type!=33)continue; // sequence_parameter_set NAL only
  ++probe.spsCount;
  // hevc NAL header = 16 bits.
  // sps_video_parameter_set_id(4), sps_max_sub_layers_minus1(3),
  // sps_temporal_id_nesting_flag(1) = one byte.
  // Next: general_profile_space(2), tier(1), general_profile_idc(5).
  // These first SPS bytes are before any start-code emulation prevention.
  const int value=data[n+3]&31;
  if(probe.generalProfileIdc>=0 && probe.generalProfileIdc!=value){
   probe.malformed=true;continue;
  }
  probe.generalProfileIdc=value;
 }
 if(probe.spsCount==0)probe.malformed=true;
 return probe;
}
} // namespace HevcSps
