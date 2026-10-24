#pragma once
#include "../wic_heic/HeicLosslessNative.h"
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace NativeLosslessProbe {
inline HeicLosslessNative::Bytes Read(const char* file) {
    std::ifstream in(file,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot open synthetic HEIC fixture");
    return HeicLosslessNative::Bytes(std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}
// Structural parser regression only. Build a tiny artificial BMFF metadata
// graph with ONE hvc1 primary and no iref, Exif, thumbnail or auxiliary item.
// No user photo, codec invocation, encoding, registry or file writes.
inline void U16(HeicLosslessNative::Bytes& b,uint16_t v) {
    b.push_back(uint8_t(v>>8));b.push_back(uint8_t(v));
}
inline void U32(HeicLosslessNative::Bytes& b,uint32_t v) {
    for(int n=3;n>=0;--n)b.push_back(uint8_t(v>>(n*8)));
}
inline HeicLosslessNative::Bytes Box(
    const char* id,const HeicLosslessNative::Bytes& payload) {
    HeicLosslessNative::Bytes b;
    U32(b,uint32_t(payload.size()+8));
    for(int i=0;i<4;i++)b.push_back(uint8_t(id[i]));
    b.insert(b.end(),payload.begin(),payload.end());
    return b;
}
inline void Append(HeicLosslessNative::Bytes& b,
                   const HeicLosslessNative::Bytes& rhs) {
    b.insert(b.end(),rhs.begin(),rhs.end());
}
inline void VerifyReferenceFreePrimary() {
    using B=HeicLosslessNative::Bytes;
    B file=Box("ftyp",{'h','e','i','c',0,0,0,0});
    const B pitm=Box("pitm",{0,0,0,0,0,1});
    const B infe=Box("infe",{2,0,0,0,0,1,0,0,'h','v','c','1',0});
    B iinfBody{0,0,0,0,0,1};Append(iinfBody,infe);
    const B iinf=Box("iinf",iinfBody);
    const B irot=Box("irot",{3});
    const B ipco=Box("ipco",irot);
    // ipma FullBox v0, entry_count=1, item=1, properties=(essential,idx1).
    const B ipma=Box("ipma",{0,0,0,0,0,0,0,1,0,1,1,0x81});
    B iprpBody;Append(iprpBody,ipco);Append(iprpBody,ipma);
    const B iprp=Box("iprp",iprpBody);
    B metaBody{0,0,0,0};
    Append(metaBody,pitm);Append(metaBody,iinf);Append(metaBody,iprp);
    Append(file,Box("meta",metaBody));
    const auto rotated=HeicLosslessNative::Transform(file,1);
    if(rotated.size()!=file.size())
        throw std::runtime_error("Reference-free primary changed HEIF length");
    size_t changed=0;
    for(size_t i=0;i<file.size();++i) {
        if(file[i]==rotated[i])continue;
        ++changed;
        if(file[i]!=3||rotated[i]!=2)
            throw std::runtime_error("Reference-free primary modified unknown HEIF bytes");
    }
    if(changed!=1)
        throw std::runtime_error("Reference-free HEIF rotation was not applied");
    // An unreferenced Exif or second image is NOT safe: one listed image only.
    // Corrupt the iinf item_count in a separate test copy.
    B secondary=file;
    const uint8_t iinfMarker[]={'i','i','n','f'};
    auto p=std::search(secondary.begin(),secondary.end(),
                       std::begin(iinfMarker),std::end(iinfMarker));
    if(p==secondary.end())throw std::runtime_error("Reference-free iinf missing");
    const size_t marker=static_cast<size_t>(p-secondary.begin());
    secondary.at(marker+9)=2; // iinf v0 item count (big-endian u16)
    bool rejected=false;
    try {(void)HeicLosslessNative::Transform(secondary,1);}
    catch(const std::runtime_error&){rejected=true;}
    if(!rejected)throw std::runtime_error("Unsafe unreferenced multi-item HEIF accepted");
    // Unknown item types must not be treated as simple primary hvc1.
    B exif=file;
    const uint8_t hvc[]={'h','v','c','1'};
    auto type=std::search(exif.begin(),exif.end(),std::begin(hvc),std::end(hvc));
    if(type==exif.end())throw std::runtime_error("Primary HEVC type not found");
    *type='E';
    rejected=false;
    try {(void)HeicLosslessNative::Transform(exif,1);}
    catch(const std::runtime_error&){rejected=true;}
    if(!rejected)throw std::runtime_error("Unknown metadata/item type was rotated");
    puts("NATIVE_HEIC_REFERENCE_FREE_SINGLE_PRIMARY_SAFE_ROTATION_PASS");
}
inline void Run() {
    VerifyReferenceFreePrimary();
    const auto initial=Read("samples\\fixture-irot3-exif6-noaux.heic");
    const auto expected=Read("samples\\fixture-lossless-cw90.heic");
    const auto actual=HeicLosslessNative::Transform(initial,1);
    if(actual!=expected)
        throw std::runtime_error("Native lossless transform does not match Python reference");
    auto current=initial;
    for(int i=0;i<4;i++) {
        auto next=HeicLosslessNative::Transform(current,1);
        if(next.size()!=initial.size())
            throw std::runtime_error("Native lossless changed container length");
        size_t changed=0;
        for(size_t k=0;k<next.size();k++)if(next[k]!=current[k])++changed;
        if(changed>2 || changed<1)
            throw std::runtime_error("Native lossless changed unrelated HEIC bytes");
        current=next;
    }
    for(auto bad : {HeicLosslessNative::Bytes{0,1,2,3},
                    HeicLosslessNative::Bytes(initial.begin(),initial.begin()+40)}) {
        try {
            auto transformed=HeicLosslessNative::Transform(bad,1);
            throw std::runtime_error("Malformed native HEIC accepted");
        } catch(const std::runtime_error& e) {
            if(std::string(e.what())=="Malformed native HEIC accepted") throw;
        }
    }
    // A synthetic "thmb" or "auxl" primary link must be rejected,
    // because a primary-only rotation would leave related images stale.
    for(const char* tag : {"thmb","auxl"}) {
        auto withRelated=initial;
        const unsigned char marker[4]={'c','d','s','c'};
        auto it=std::search(withRelated.begin(),withRelated.end(),marker,marker+4);
        if(it==withRelated.end())throw std::runtime_error("Synthetic EXIF reference missing");
        for(int j=0;j<4;j++)it[j]=static_cast<unsigned char>(tag[j]);
        bool rejected=false;
        try { auto unsafe=HeicLosslessNative::Transform(withRelated,1);(void)unsafe; }
        catch(const std::runtime_error&) { rejected=true; }
        if(!rejected)throw std::runtime_error("Unsafe thumbnail/HDR rotation accepted");
    }
    printf("NATIVE_LOSSLESS_HEIF_MATCHES_PYTHON_AND_ROTATION_TEST_PASSED\n");
}
}
