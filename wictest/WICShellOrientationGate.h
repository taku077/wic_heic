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
  unsigned matches=0;
  for(const auto& e:entries) if(e.id==primary) {
   if(++matches!=1)return false;
   for(uint32_t index:e.props) {
    if(index<1 || index>properties.size())return false;
    const auto& prop=properties[index-1];
    if(prop.type=="irot" || prop.type=="imir" || prop.type=="clap" ||
       prop.type=="auxC" || prop.type=="clli" || prop.type=="mdcv")return false;
    if(prop.type=="pixi") {
     size_t p=prop.data;
     if(p+5>prop.end)return false;
     size_t n=Read(bytes,p+4,1);
     if(n==0 || n>8 || p+5+n!=prop.end)return false;
     for(size_t c=0;c<n;c++)if(Read(bytes,p+5+c,1)!=8)return false;
    }
   }
  }
  if(matches!=1)return false;
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
