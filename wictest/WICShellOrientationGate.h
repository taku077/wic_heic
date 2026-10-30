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
     if(prop.end<prop.data || prop.end-prop.data<4)return false;
     const auto colorType=Read(bytes,prop.data,4);
     // Existing ICC route is already guarded by GetColorContexts and uses
     // the full WIC image; retain that proven behavior and its diagnostics.
     if(colorType==0x70726f66u || colorType==0x72494343u)continue; // prof/rICC
     if(colorType!=0x6e636c78u || prop.end-prop.data<11)return false; // nclx
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
// CI-only positive gate for a simple 8-bit SDR 8x6 grid with one HEVC
// thumbnail. This is deliberately NOT used for production/user HEIC.
// Reject unknown items/references/properties, metadata, transforms, alpha,
// ICC, HDR, and ambiguous relationships. Do not mutate any bytes.
inline bool ResearchGrid48PreviewSafe(const Bytes& bytes) noexcept {
 try {
  if(bytes.size()<24 || bytes.size()>24u*1024u*1024u)return false;
  const auto top=Children(bytes,0,bytes.size());
  const auto meta=One(top,"meta");
  const auto sub=Children(bytes,meta.data+4,meta.end);
  const auto primary=Primary(bytes,meta);
  // Reject all unknown top-level metadata except the limited structural
  // boxes required for a HEVC still image.
  for(const auto& box:sub)
   if(box.type!="hdlr"&&box.type!="pitm"&&box.type!="iinf"&&
      box.type!="iloc"&&box.type!="iprp"&&box.type!="iref"&&
      box.type!="idat")return false;
  const auto info=One(sub,"iinf");
  const unsigned infoVer=unsigned(Read(bytes,info.data,1));
  if(infoVer>1)return false;
  const size_t cw=infoVer?4:2;
  const uint32_t itemsCount=uint32_t(Read(bytes,info.data+4,cw));
  if(itemsCount!=50)return false; // 1 grid + 48 HEVC tiles + 1 thumb
  const auto records=Children(bytes,info.data+4+cw,info.end);
  if(records.size()!=itemsCount)return false;
  std::vector<uint32_t> hvc1s;
  uint32_t grids=0;
  for(const auto& item:records){
   if(item.type!="infe")return false;
   const unsigned v=unsigned(Read(bytes,item.data,1));
   if(v!=2 && v!=3)return false;
   const size_t iw=v==2?2:4;
   const uint32_t id=uint32_t(Read(bytes,item.data+4,iw));
   const uint32_t type=uint32_t(Read(bytes,item.data+4+iw+2,4));
   if(id==0)return false;
   if(type==0x67726964u){ // grid
    ++grids;
    if(id!=primary)return false;
   } else if(type==0x68766331u){ // hvc1
    if(std::find(hvc1s.begin(),hvc1s.end(),id)!=hvc1s.end())
     return false;
    hvc1s.push_back(id);
   } else return false; // Exif, mime, aux, overlay and unknown
  }
  if(grids!=1 || hvc1s.size()!=49)return false;
  const auto reference=One(sub,"iref");
  const unsigned refVer=unsigned(Read(bytes,reference.data,1));
  if(refVer>1)return false;
  const size_t refW=refVer?4:2;
  std::vector<uint32_t> tiles;
  uint32_t thumbId=0;
  unsigned dimgLinks=0,thumbLinks=0;
  for(const auto& relation:Children(bytes,reference.data+4,reference.end)){
   if(relation.type!="dimg" && relation.type!="thmb")return false;
   size_t p=relation.data;
   const uint32_t from=uint32_t(Read(bytes,p,refW));p+=refW;
   const uint32_t count=uint32_t(Read(bytes,p,2));p+=2;
   if(count==0 || count>48 || size_t(count)*refW>relation.end-p ||
      p+size_t(count)*refW!=relation.end)return false;
   if(relation.type=="dimg"){
    if(from!=primary || ++dimgLinks!=1 || count!=48)return false;
    for(uint32_t i=0;i<count;++i){
     uint32_t to=uint32_t(Read(bytes,p+size_t(i)*refW,refW));
     if(to==primary || std::find(hvc1s.begin(),hvc1s.end(),to)==hvc1s.end() ||
        std::find(tiles.begin(),tiles.end(),to)!=tiles.end())return false;
     tiles.push_back(to);
    }
   } else {
    // libheif thumbnail->master direction, not master->thumbnail.
    if(++thumbLinks!=1 || count!=1)return false;
    const uint32_t to=uint32_t(Read(bytes,p,refW));
    if(to!=primary || from==primary ||
       std::find(hvc1s.begin(),hvc1s.end(),from)==hvc1s.end())
      return false;
    thumbId=from;
   }
  }
  if(dimgLinks!=1 || thumbLinks!=1 || tiles.size()!=48 ||
     std::find(tiles.begin(),tiles.end(),thumbId)!=tiles.end())
   return false;
  const auto iprp=One(sub,"iprp");
  const auto parts=Children(bytes,iprp.data,iprp.end);
  const auto ipco=One(parts,"ipco");
  const auto properties=Children(bytes,ipco.data,ipco.end);
  const auto assoc=Associations(bytes,One(parts,"ipma"));
  // Ensure all 50 image items have exactly one association record; verify
  // EVERY tile/thumbnail (not only the grid primary).
  if(assoc.size()!=50)return false;
  std::vector<uint32_t> checked;
  for(const auto& entry:assoc){
   const uint32_t id=entry.id;
   if(id!=primary && std::find(hvc1s.begin(),hvc1s.end(),id)==hvc1s.end())
    return false;
   if(std::find(checked.begin(),checked.end(),id)!=checked.end())return false;
   checked.push_back(id);
   bool hasHvcC=false, hasIspe=false;
   for(const uint32_t index:entry.props){
    if(index==0 || index>properties.size())return false;
    const auto& prop=properties[index-1];
    if(prop.type=="hvcC"){
     if(id==primary || hasHvcC ||
        prop.end-prop.data<23 || Read(bytes,prop.data,1)!=1 ||
        (Read(bytes,prop.data+17,1)&7)!=0 ||
        (Read(bytes,prop.data+18,1)&7)!=0)return false;
     hasHvcC=true;
    } else if(prop.type=="ispe"){
     if(hasIspe || prop.end-prop.data<12)return false;
     const uint32_t w=uint32_t(Read(bytes,prop.data+4,4));
     const uint32_t h=uint32_t(Read(bytes,prop.data+8,4));
     if(!w||!h || w>16384 || h>16384)return false;
     hasIspe=true;
    } else if(prop.type=="pixi"){
     const size_t n=Read(bytes,prop.data+4,1);
     if(n==0||n>4||prop.data+5+n!=prop.end)return false;
     for(size_t j=0;j<n;++j)
      if(Read(bytes,prop.data+5+j,1)!=8)return false;
    } else if(prop.type=="colr"){
     // No ICC/prof/rICC or wide gamut. Only basic SDR CICP permitted.
     if(prop.end-prop.data<11 || Read(bytes,prop.data,4)!=0x6e636c78u)
      return false;
     const auto prim=Read(bytes,prop.data+4,2);
     const auto transf=Read(bytes,prop.data+6,2);
     const auto matrix=Read(bytes,prop.data+8,2);
     if((prim!=1&&prim!=2) || (transf!=1&&transf!=13) ||
         (matrix!=0&&matrix!=1&&matrix!=2))return false;
    } else return false; // irot/imir/clap/pasp/etc.
   }
   if(!hasIspe || (id!=primary && !hasHvcC))return false;
  }
  return checked.size()==50;
 }catch(...){return false;}
}
inline bool ResearchGrid48FromStream(IStream* stream) noexcept {
 if(!stream)return false;
 LARGE_INTEGER z{}; ULARGE_INTEGER original{};
 if(FAILED(stream->Seek(z,STREAM_SEEK_CUR,&original)))return false;
 struct Restore {
  IStream* s; ULARGE_INTEGER p;
  ~Restore(){LARGE_INTEGER to{};to.QuadPart=static_cast<LONGLONG>(p.QuadPart);
    s->Seek(to,STREAM_SEEK_SET,nullptr);}
 } restore{stream,original};
 STATSTG st{};
 if(FAILED(stream->Stat(&st,STATFLAG_NONAME)) ||
    st.cbSize.QuadPart<24 || st.cbSize.QuadPart>24ull*1024ull*1024ull)
   return false;
 if(FAILED(stream->Seek(z,STREAM_SEEK_SET,nullptr)))return false;
 try {
  Bytes bytes(static_cast<size_t>(st.cbSize.QuadPart));
  size_t done=0;
  while(done<bytes.size()){
   ULONG got=0;
   const ULONG amount=static_cast<ULONG>(
      min(size_t(65536),bytes.size()-done));
   if(FAILED(stream->Read(bytes.data()+done,amount,&got))||got==0)
     return false;
   done+=got;
  }
  return ResearchGrid48PreviewSafe(bytes);
 }catch(...){return false;}
}
// Fail closed without copying an entire 12MP/48-tile HEIC if its first
// metadata window already advertises complex image relations. The scan is
// intentionally a conservative REJECT ONLY: a match may be incidental and
// merely selects the ordinary full WIC path; it NEVER admits an image.
// The regular strict BMFF parser remains the sole acceptance authority.
inline bool ObviousComplexPrefix(const uint8_t* p,size_t n) noexcept {
 if(!p || n<4)return false;
 static const char* const tags[]={"grid","auxl","dimg","tmap"};
 for(size_t i=0;i+4<=n;++i) {
  for(const char* tag:tags) {
   if(p[i]==static_cast<uint8_t>(tag[0]) &&
      p[i+1]==static_cast<uint8_t>(tag[1]) &&
      p[i+2]==static_cast<uint8_t>(tag[2]) &&
      p[i+3]==static_cast<uint8_t>(tag[3]))return true;
  }
 }
 return false;
}
inline bool FromStream(IStream* stream,size_t* bytesRead=nullptr) noexcept {
 if(bytesRead)*bytesRead=0;
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
  // Allocate only a small prefix first: reserving/zeroing the whole HEIC
  // before rejecting grid/alpha/HDR would defeat the memory optimization.
  const size_t size=static_cast<size_t>(st.cbSize.QuadPart);
  const size_t prefix=min(size,size_t(65536));
  Bytes bytes(prefix);
  size_t done=0;
  while(done<prefix) {
   ULONG got=0;
   ULONG amount=static_cast<ULONG>(prefix-done);
   HRESULT hr=stream->Read(bytes.data()+done,amount,&got);
   if(FAILED(hr)||got==0)return false;
   done+=got;
   if(bytesRead)*bytesRead=done;
  }
  // Fail closed for obvious complex metadata without allocating or copying
  // any media payload. This never ACCEPTS an unverified HEIC.
  if(ObviousComplexPrefix(bytes.data(),done))return false;
  bytes.resize(size);
  while(done<size) {
   ULONG got=0;
   ULONG amount=static_cast<ULONG>(min(size_t(65536),size-done));
   HRESULT hr=stream->Read(bytes.data()+done,amount,&got);
   if(FAILED(hr)||got==0)return false;
   done+=got;
   if(bytesRead)*bytesRead=done;
  }
  return SimplePreviewSafe(bytes);
 } catch (...) {return false;}
}
}
