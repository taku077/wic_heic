#pragma once
// CI-only conservative preflight; no image edits, no HEVC pixel decoding.
#include <windows.h>
#include <objidl.h>
#include <vector>
#include <cstdint>
#include "../wic_heic/HeicLosslessNative.h"
namespace WicShellLabSafety {
using namespace HeicLosslessNative;
inline bool SimplePreviewSafe(const Bytes& bytes) noexcept {
 try {
  if(bytes.size()<24 || bytes.size()>24u*1024u*1024u)return false;
  auto top=Children(bytes,0,bytes.size());
  auto meta=One(top,"meta");
  auto sub=Children(bytes,meta.data+4,meta.end);
  auto primary=Primary(bytes,meta);
  auto iprp=One(sub,"iprp");
  auto parts=Children(bytes,iprp.data,iprp.end);
  auto ipco=One(parts,"ipco");
  auto properties=Children(bytes,ipco.data,ipco.end);
  auto entries=Associations(bytes,One(parts,"ipma"));
  // HEIF permits grid, overlay, alpha and HDR auxiliary item structures.
  // The quick 8-bit HEVC embedded path is only safe for a single hvc1 primary.
  // Other item forms defer to the existing full WIC decoder.
  auto info=One(sub,"iinf");
  const unsigned infoVersion=static_cast<unsigned>(Read(bytes,info.data,1));
  if(infoVersion>1)return false;
  const size_t countSize=infoVersion?4:2;
  const auto itemBoxes=Children(bytes,info.data+4+countSize,info.end);
  if(Read(bytes,info.data+4,countSize)!=itemBoxes.size())return false;
  unsigned primaryItems=0;
  for(const auto& item:itemBoxes) {
   if(item.type!="infe")continue;
   const unsigned v=static_cast<unsigned>(Read(bytes,item.data,1));
   if(v!=2 && v!=3)continue;
   const size_t idSize=(v==2?2:4);
   const auto id=Read(bytes,item.data+4,idSize);
   if(id==primary) {
    primaryItems++;
    const size_t itemTypeAt=item.data+4+idSize+2;
    if(Read(bytes,itemTypeAt,4)!=0x68766331u)return false; // hvc1
   }
  }
  if(primaryItems!=1)return false;
  // An alpha, gain-map, tiled grid or other image relation must never be
  // flattened into an apparently opaque 8-bit preview. A plain thmb link is
  // allowed because we verify the actual thumbnail geometry separately.
  for(const auto& item:sub) if(item.type=="iref") {
   const unsigned v=static_cast<unsigned>(Read(bytes,item.data,1));
   if(v>1)return false;
   const size_t idWidth=v?4:2;
   for(const auto& rel:Children(bytes,item.data+4,item.end)) {
    if(rel.type=="auxl"||rel.type=="dimg"||rel.type=="tmap")return false;
    // Unknown relationships may alter the displayed primary image.
    if(rel.type!="thmb" && rel.type!="cdsc")return false;
    size_t pos=rel.data+idWidth;
    const unsigned count=static_cast<unsigned>(Read(bytes,pos,2));
    pos+=2;
    if(count>65535 || count*idWidth>rel.end-pos ||
       pos+size_t(count)*idWidth!=rel.end)return false;
   }
  }
  unsigned matches=0;
  bool hevcConfig=false;
  for(const auto& e:entries) if(e.id==primary) {
   if(++matches!=1)return false;
   for(uint32_t index:e.props) {
    if(index<1 || index>properties.size())return false;
    const auto& prop=properties[index-1];
    if(prop.type!="hvcC" && prop.type!="ispe" && prop.type!="pixi" &&
       prop.type!="colr")return false;
    if(prop.type=="colr") {
     // HEIF colr: 4-byte type followed by CICP for nclx.
     // Embedded previews are not independently tone-mapped or ICC matched.
     // Refuse PQ/HLG, wide gamut, opaque/unknown color encodings.
     if(prop.end<prop.data || prop.end-prop.data<11)return false;
     if(Read(bytes,prop.data,4)!=0x6e636c78u)return false; // only nclx
     const auto primaries=Read(bytes,prop.data+4,2);
     const auto transfer=Read(bytes,prop.data+6,2);
     const auto matrix=Read(bytes,prop.data+8,2);
     // Transfer 16=PQ, 18=HLG (ITU-T H.273). Restrict to SDR.
     if(transfer!=1 && transfer!=13)return false;
     if(primaries!=1 && primaries!=2)return false;
     if(matrix!=0 && matrix!=1 && matrix!=2)return false;
    }
    if(prop.type=="hvcC") {
     // ISO/IEC 14496-15 HEVCDecoderConfigurationRecord:
     // offsets 17 and 18 carry bitDepthLumaMinus8/chromaMinus8.
     if(prop.end-prop.data<23 || Read(bytes,prop.data,1)!=1)return false;
     if((Read(bytes,prop.data+17,1)&7)!=0 ||
        (Read(bytes,prop.data+18,1)&7)!=0)return false;
     if(hevcConfig)return false;
     hevcConfig=true;
    }
    if(prop.type=="pixi") {
     size_t p=prop.data;
     if(p+5>prop.end)return false;
     size_t n=Read(bytes,p+4,1);
     if(n==0 || n>8 || p+5+n!=prop.end)return false;
     for(size_t c=0;c<n;c++)if(Read(bytes,p+5+c,1)!=8)return false;
    }
   }
  }
  if(matches!=1 || !hevcConfig)return false;
  // EXIF orientation has vendor-dependent semantics relative to HEIF irot;
  // do not accelerate until equivalence is independently established.
  for(const auto& box:sub)if(box.type=="iinf")
   if(!ExifIDs(bytes,box).empty())return false;
  return true;
 } catch (...) {return false;}
}
inline bool FromStream(IStream* stream) noexcept {
 if(!stream)return false;
 LARGE_INTEGER zero{}; ULARGE_INTEGER original{};
 if(FAILED(stream->Seek(zero,STREAM_SEEK_CUR,&original)))return false;
 struct Restore {
  IStream* stream; ULARGE_INTEGER pos;
  ~Restore(){LARGE_INTEGER target{}; target.QuadPart=static_cast<LONGLONG>(pos.QuadPart);
   stream->Seek(target,STREAM_SEEK_SET,nullptr);}
 } reset{stream,original};
 STATSTG st{};
 if(FAILED(stream->Stat(&st,STATFLAG_NONAME)) ||
    st.cbSize.QuadPart<24 || st.cbSize.QuadPart>24ull*1024ull*1024ull)return false;
 if(FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)))return false;
 try {
  Bytes bytes(static_cast<size_t>(st.cbSize.QuadPart));
  size_t done=0;
  while(done<bytes.size()) {
   ULONG got=0;
   ULONG amount=static_cast<ULONG>(min(size_t(65536),bytes.size()-done));
   HRESULT hr=stream->Read(bytes.data()+done,amount,&got);
   if(FAILED(hr)||got==0)return false;
   done+=got;
  }
  return SimplePreviewSafe(bytes);
 } catch (...) {return false;}
}
}
