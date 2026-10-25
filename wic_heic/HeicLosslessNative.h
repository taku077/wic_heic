#pragma once
// Narrow, fail-closed ISO-BMFF/HEIF lossless clockwise rotation engine.
// NO file I/O and NO HEVC decode/re-encode. Never edits caller's input.
// Supports existing primary irot and simple primary EXIF. A shared irot
// may rotate related thumbnails/HDR auxiliary images ONLY when all those
// images explicitly share the same property and no ambiguity is detected.
// Never guesses source geometry or original HEVC bytes from edited WIC rasters.
#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

namespace HeicLosslessNative {
using Bytes=std::vector<uint8_t>;
struct Box {std::string type; size_t begin,end,data;};
inline void Bad(const char* why) {throw std::runtime_error(why);}
inline uint64_t Read(const Bytes& b,size_t p,size_t n){
    if(n>8 || p>b.size() || n>b.size()-p)Bad("Invalid HEIF data offset");
    uint64_t v=0;for(size_t i=0;i<n;++i)v=(v<<8)|b[p+i];return v;
}
inline uint64_t Int(const Bytes& b,size_t p,size_t n,bool le){
    if(p>b.size()||n>b.size()-p)Bad("Invalid EXIF offset");
    uint64_t v=0;
    if(le){for(size_t i=0;i<n;i++)v|=uint64_t(b[p+i])<<(i*8);}
    else {for(size_t i=0;i<n;i++)v=(v<<8)|b[p+i];}
    return v;
}
inline std::vector<Box> Children(const Bytes& b,size_t begin,size_t end) {
    if(end>b.size()||begin>end)Bad("HEIF box region out of range");
    std::vector<Box> list;
    for(size_t p=begin;p<end;){
        if(end-p<8)Bad("Truncated HEIF box header");
        uint64_t size=Read(b,p,4);
        std::string type(reinterpret_cast<const char*>(&b[p+4]),4);
        size_t hs=8;
        if(size==1){if(end-p<16)Bad("Truncated large HEIF box");size=Read(b,p+8,8);hs=16;}
        if(size==0)size=end-p;
        if(size<hs||size>end-p)Bad("Out-of-bounds HEIF box size");
        list.push_back({type,p,p+size,p+hs});
        if(list.size()>100000)Bad("Too many HEIF boxes");
        p+=size;
    }
    return list;
}
inline Box One(const std::vector<Box>& list,const char* type) {
    Box found{};bool yes=false;
    for(const auto& b:list)if(b.type==type){
        if(yes)Bad("Duplicate HEIF box");
        found=b;yes=true;
    }
    if(!yes)Bad("Required HEIF box not found");
    return found;
}
inline uint32_t Primary(const Bytes& b,const Box& meta) {
    auto pitm=One(Children(b,meta.data+4,meta.end),"pitm");
    auto version=Read(b,pitm.data,1);
    if(version>1)Bad("Unsupported primary item version");
    return uint32_t(Read(b,pitm.data+4,version?4:2));
}
struct Association {uint32_t id;std::vector<uint32_t> props;};
inline std::vector<Association> Associations(const Bytes& b,const Box& ipma) {
    size_t p=ipma.data;
    uint32_t version=uint32_t(Read(b,p,1));
    uint32_t flags=uint32_t(Read(b,p+1,3));
    if(version>1||flags>1)Bad("Unsupported HEIF item association");
    uint32_t count=uint32_t(Read(b,p+4,4));p+=8;
    if(count>65535)Bad("Too many item associations");
    std::vector<Association> a;
    for(uint32_t i=0;i<count;i++){
        uint32_t id=uint32_t(Read(b,p,version?4:2));p+=version?4:2;
        uint32_t n=uint32_t(Read(b,p,1));p++;
        Association entry{id,{}};
        for(uint32_t j=0;j<n;j++){
            uint32_t raw=uint32_t(Read(b,p,flags?2:1));p+=flags?2:1;
            uint32_t idx=raw&(flags?0x7fff:0x7f);
            if(idx)entry.props.push_back(idx);
        }
        a.push_back(entry);
    }
    if(p!=ipma.end)Bad("Trailing item association data");
    return a;
}
inline std::vector<uint32_t> ExifIDs(const Bytes& b,const Box& iinf) {
    size_t p=iinf.data;uint32_t version=uint32_t(Read(b,p,1));
    if(version>1)Bad("Unsupported iinf version");
    const size_t cntWidth=version?4:2;
    const uint32_t n=uint32_t(Read(b,p+4,cntWidth));
    auto entries=Children(b,p+4+cntWidth,iinf.end);
    if(n!=entries.size())Bad("Invalid iinf count");
    std::vector<uint32_t> result;
    for(const auto& e:entries){
        if(e.type!="infe")continue;
        uint32_t v=uint32_t(Read(b,e.data,1));
        if(v!=2 && v!=3)continue;
        uint32_t id=uint32_t(Read(b,e.data+4,v==2?2:4));
        size_t typeAt=e.data+4+(v==2?2:4)+2;
        if(Read(b,typeAt,4)==0x45786966)result.push_back(id); // Exif
    }
    return result;
}
inline bool HasCdsc(const Bytes& b,const Box& iref,uint32_t exifId,uint32_t primary) {
    uint32_t version=uint32_t(Read(b,iref.data,1));
    if(version>1)Bad("Unsupported iref");
    const size_t width=version?4:2;
    for(const auto& e:Children(b,iref.data+4,iref.end)){
        if(e.type!="cdsc")continue;
        size_t p=e.data;uint32_t from=uint32_t(Read(b,p,width));p+=width;
        uint32_t n=uint32_t(Read(b,p,2));p+=2;
        if(p+uint64_t(n)*width!=e.end)Bad("Invalid cdsc");
        if(from==exifId){
            for(uint32_t k=0;k<n;k++)if(Read(b,p+k*width,width)==primary)
                return true;
        }
    }
    return false;
}
// Related visual items (thumbnail, HDR/auxiliary) must rotate in exactly
// the same transform as the primary. HEIF permits a SINGLE irot property
// to be associated with multiple items, in which case changing its one-byte
// value coherently rotates primary + thumbnail + aux without re-encoding.
// It is UNSAFE when a related visual lacks that exact shared association.
inline std::vector<uint32_t> LinkedVisualItems(
    const Bytes& b,const Box& iref,uint32_t primary)
{
    const uint32_t version=uint32_t(Read(b,iref.data,1));
    if(version>1)Bad("Unsupported iref version");
    const size_t width=version?4:2;
    std::vector<uint32_t> linked;
    for(const auto& e:Children(b,iref.data+4,iref.end)){
        if(e.type!="thmb" && e.type!="auxl")continue;
        size_t p=e.data;
        const uint32_t from=uint32_t(Read(b,p,width));p+=width;
        const uint32_t count=uint32_t(Read(b,p,2));p+=2;
        if(p+uint64_t(count)*width!=e.end)
            Bad("Invalid thumbnail/auxiliary references");
        for(uint32_t i=0;i<count;i++){
            const uint32_t to=uint32_t(Read(b,p+i*width,width));
            uint32_t related=0;
            if(from==primary)related=to;
            else if(to==primary)related=from;
            if(related==primary)Bad("Self-referencing visual image");
            if(related &&
                std::find(linked.begin(),linked.end(),related)==linked.end())
                linked.push_back(related);
        }
    }
    return linked;
}
inline void ValidateSharedRotation(
    const Bytes& b,const std::vector<Box>& properties,
    const std::vector<Association>& associations,
    const std::vector<uint32_t>& linked,
    uint32_t primary,uint32_t irotIndex)
{
    // Each associated item may be listed exactly once; duplicate ipma
    // entries or a separate transform are too ambiguous to patch in place.
    for(const auto& item:linked){
        uint32_t found=0;bool sameAngle=false;
        for(const auto& entry:associations)if(entry.id==item){
            ++found;
            for(uint32_t idx:entry.props){
                if(idx<1||idx>properties.size())
                    Bad("Invalid related image property index");
                const auto& q=properties[idx-1];
                if(q.type=="imir"||q.type=="clap")
                    Bad("Related image has unsupported compound transform");
                if(q.type=="irot" && idx!=irotIndex)
                    Bad("Related visual has separate irot property");
                if(idx==irotIndex)sameAngle=true;
            }
        }
        if(found!=1||!sameAngle)
            Bad("Related visual does not share primary irot property");
    }
    for(const auto& entry:associations)if(entry.id!=primary){
        if(std::find(entry.props.begin(),entry.props.end(),irotIndex)==entry.props.end())
            continue;
        if(std::find(linked.begin(),linked.end(),entry.id)==linked.end())
            Bad("Rotation property shared by unrelated item");
    }
}
inline std::pair<size_t,size_t> ExifRange(const Bytes& b,const Box& iloc,const Box* idat,uint32_t wanted) {
    size_t p=iloc.data;
    uint32_t version=uint32_t(Read(b,p,1));
    if(version>2)Bad("Unsupported iloc version");
    uint32_t sizes=uint32_t(Read(b,p+4,2));p+=6;
    size_t ofs=(sizes>>12)&15,len=(sizes>>8)&15,baseSize=(sizes>>4)&15,index=sizes&15;
    if(!ofs||!len||ofs>8||len>8||baseSize>8||index>8)
        Bad("Unsupported iloc field size");
    size_t countWidth=version<2?2:4;
    uint32_t count=uint32_t(Read(b,p,countWidth));p+=countWidth;
    if(count>65535)Bad("Too many iloc records");
    for(uint32_t i=0;i<count;i++){
        uint32_t id=uint32_t(Read(b,p,countWidth));p+=countWidth;
        uint32_t method=0;
        if(version){method=uint32_t(Read(b,p,2))&15;p+=2;}
        if(Read(b,p,2)!=0)Bad("External EXIF data reference");p+=2;
        uint64_t base=Read(b,p,baseSize);p+=baseSize;
        uint32_t n=uint32_t(Read(b,p,2));p+=2;
        if(n>16384)Bad("Too many EXIF extents");
        for(uint32_t j=0;j<n;j++){
            if(version&&index)p+=index;
            uint64_t offset=Read(b,p,ofs);p+=ofs;
            uint64_t length=Read(b,p,len);p+=len;
            if(id!=wanted)continue;
            if(n!=1||length<12)Bad("Unsupported EXIF extent");
            uint64_t absolute=base+offset;
            if(absolute<base)Bad("EXIF extent overflow");
            if(method==1){
                if(!idat)Bad("IDAT missing");
                absolute+=idat->data;
            }else if(method!=0)Bad("Unsupported EXIF construction");
            if(absolute>b.size()||length>b.size()-absolute)
                Bad("EXIF extent out of bounds");
            return {size_t(absolute),size_t(length)};
        }
    }
    Bad("Referenced EXIF item missing");
    return {0,0};
}
struct Patch {size_t offset;uint8_t value;};
inline void NormalizeExif(const Bytes& b,size_t start,size_t length,std::vector<Patch>& changes,uint32_t oldAngle) {
    uint64_t offset=Read(b,start,4);
    if(offset>length-4)Bad("Bad Exif TIFF offset");
    size_t tiff=start+4+size_t(offset);
    if(tiff+8>start+length)Bad("Truncated Exif TIFF");
    bool le;
    if(b[tiff]=='I'&&b[tiff+1]=='I')le=true;
    else if(b[tiff]=='M'&&b[tiff+1]=='M')le=false;
    else Bad("Invalid TIFF byte order");
    if(Int(b,tiff+2,2,le)!=42)Bad("Invalid TIFF magic");
    uint64_t ifdOffset=Int(b,tiff+4,4,le);
    if(ifdOffset>length||tiff+ifdOffset>start+length-2)Bad("Invalid Exif IFD");
    size_t dir=tiff+size_t(ifdOffset);
    uint64_t count=Int(b,dir,2,le);
    if(count>2048||count>(start+length-dir-2)/12)Bad("Oversized Exif IFD");
    size_t found=0;
    for(size_t i=0;i<count;i++){
        size_t e=dir+2+i*12;
        if(Int(b,e,2,le)!=274)continue;
        ++found;
        if(found>1)Bad("Ambiguous Exif orientations");
        if(Int(b,e+2,2,le)!=3||Int(b,e+4,4,le)!=1)
            Bad("Unexpected Exif Orientation encoding");
        uint64_t value=Int(b,e+8,2,le);
        if(value==6||value==8){
            if(oldAngle%2!=1)Bad("Ambiguous HEIF/EXIF rotation");
            changes.push_back({e+8,uint8_t(le?1:0)});
            changes.push_back({e+9,uint8_t(le?0:1)});
        }else if(value!=1)Bad("Unsupported Exif Orientation");
    }
}
// Read only the primary HEIF item's container rotation property. This does
// not infer rotation from the unchanged width/height of a 180-degree image.
// Ambiguous/malformed HEIF never authorizes altering WIC-visible EXIF.
inline bool PrimaryHasIrot180(const Bytes& input) {
    const auto top=Children(input,0,input.size());
    const auto meta=One(top,"meta");
    const auto m=Children(input,meta.data+4,meta.end);
    const uint32_t id=Primary(input,meta);
    const auto iprp=One(m,"iprp");
    const auto p=Children(input,iprp.data,iprp.end);
    const auto ipco=One(p,"ipco");
    const auto properties=Children(input,ipco.data,ipco.end);
    const auto entries=Associations(input,One(p,"ipma"));
    size_t count=0;
    uint32_t orientation=0;
    for(const auto& entry:entries)if(entry.id==id){
        for(uint32_t index:entry.props){
            if(index<1 || index>properties.size())
                Bad("Invalid primary property association");
            const auto& item=properties[index-1];
            if(item.type=="imir" || item.type=="clap")return false;
            if(item.type=="irot"){
                if(++count>1 || item.end-item.data!=1)
                    Bad("Ambiguous HEIF primary rotation");
                orientation=uint32_t(Read(input,item.data,1));
            }
        }
    }
    return count==1 && orientation==2;
}
// transform: 0 identity, 1 CW90, 2 CW180, 3 CCW90.
inline Bytes Transform(const Bytes& original,uint8_t transform) {
    if(transform>3)Bad("Unsupported HEIF transform option");
    if(original.empty()||original.size()>128*1024*1024)
        Bad("HEIF size outside strict bounds");
    Bytes out=original;
    if(transform==0)return out;
    const auto top=Children(original,0,original.size());
    const Box ftyp=One(top,"ftyp"),meta=One(top,"meta");
    if (ftyp.end-ftyp.data < 8) Bad("Truncated HEIF brand information");
    const uint64_t majorBrand=Read(original,ftyp.data,4);
    bool supported=(majorBrand==0x68656963 || majorBrand==0x68656978); // heic/heix
    for (size_t offset=ftyp.data+8;offset+4<=ftyp.end;offset+=4) {
        uint64_t brand=Read(original,offset,4);
        if (brand==0x68656963 || brand==0x68656978) supported=true;
    }
    if(!supported)Bad("Unsupported non-HEIC HEIF brand");
    const auto m=Children(original,meta.data+4,meta.end);
    const auto iprp=One(m,"iprp"),pitm=One(m,"pitm");
    (void)pitm;
    const uint32_t primary=Primary(original,meta);
    const auto p=Children(original,iprp.data,iprp.end);
    const auto list=Children(original,One(p,"ipco").data,One(p,"ipco").end);
    const auto associations=Associations(original,One(p,"ipma"));
    uint32_t irotIndex=0;
    for(const auto& entry:associations)if(entry.id==primary) {
        for(uint32_t idx:entry.props){
            if(idx<1||idx>list.size())Bad("Invalid HEIF property association");
            const auto& q=list[idx-1];
            if(q.type=="imir"||q.type=="clap")Bad("Compound transforms unsupported");
            if(q.type=="irot"){
                if(irotIndex)Bad("Multiple primary irot properties");
                irotIndex=idx;
            }
        }
    }
    if(!irotIndex)Bad("Primary image lacks irot property");
    const Box iref=One(m,"iref");
    const auto linked=LinkedVisualItems(original,iref,primary);
    ValidateSharedRotation(original,list,associations,linked,primary,irotIndex);
    const auto& irot=list[irotIndex-1];
    if(irot.end-irot.data!=1)Bad("Unsupported irot box");
    uint32_t old=uint32_t(Read(original,irot.data,1));
    if(old>3)Bad("Malformed irot value");
    out[irot.data]=uint8_t((old+4-transform)%4);
    std::vector<Patch> exifPatches;
    Box iinf=One(m,"iinf"),iloc=One(m,"iloc");
    const auto exifs=ExifIDs(original,iinf);
    for(uint32_t exifId:exifs)
        for(uint32_t related:linked)
            if(HasCdsc(original,iref,exifId,related))
                Bad("Related image has independent Exif orientation");
    uint32_t exifId=0;
    for(uint32_t id:exifs)if(HasCdsc(original,iref,id,primary)){
        if(exifId)Bad("Multiple primary Exif items");
        exifId=id;
    }
    if(exifId){
        const Box* idat=nullptr;
        for(const auto& item:m)if(item.type=="idat"){if(idat)Bad("Multiple idat");idat=&item;}
        const auto range=ExifRange(original,iloc,idat,exifId);
        NormalizeExif(original,range.first,range.second,exifPatches,old);
    }
    for(const auto& patch:exifPatches)out[patch.offset]=patch.value;
    return out;
}
} // namespace HeicLosslessNative
