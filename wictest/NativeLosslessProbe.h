#pragma once
#include "../wic_heic/HeicLosslessNative.h"
#include "../wic_heic/PhotosPureRotationBridge.h"
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
inline void Run() {
    const auto initial=Read("samples\\fixture-irot3-exif6-noaux.heic");
    const auto expected=Read("samples\\fixture-lossless-cw90.heic");
    const auto actual=HeicLosslessNative::Transform(initial,1);
    if(actual!=expected)
        throw std::runtime_error("Native lossless transform does not match Python reference");
    // Independent synthetic HEIF half-turn sample carries BOTH primary
    // irot=2 and TIFF Exif Orientation=3. The new lossless path must change
    // only those metadata bytes and equal the existing CW180 reference.
    const auto halfTurn=Read("samples\\fixture-irot2-exif3-noaux.heic");
    if(!HeicLosslessNative::PrimaryHasIrot180(halfTurn))
        throw std::runtime_error("Synthetic irot2 not authenticated");
    const auto halfExpected=Read("samples\\fixture-lossless-cw180.heic");
    const auto halfResult=HeicLosslessNative::Transform(halfTurn,1);
    if(halfResult!=halfExpected || halfResult.size()!=halfTurn.size())
        throw std::runtime_error("HEIF irot2 EXIF3 native rotation not byte-identical");
    auto notHalf=halfTurn;
    const auto oldBoxes=HeicLosslessNative::Children(notHalf,0,notHalf.size());
    const auto meta=HeicLosslessNative::One(oldBoxes,"meta");
    const auto sub=HeicLosslessNative::Children(notHalf,meta.data+4,meta.end);
    const auto props=HeicLosslessNative::One(sub,"iprp");
    const auto ps=HeicLosslessNative::Children(notHalf,props.data,props.end);
    const auto co=HeicLosslessNative::One(ps,"ipco");
    const auto allProps=HeicLosslessNative::Children(notHalf,co.data,co.end);
    const auto associations=HeicLosslessNative::Associations(
        notHalf,HeicLosslessNative::One(ps,"ipma"));
    const auto primary=HeicLosslessNative::Primary(notHalf,meta);
    for(const auto& e:associations)if(e.id==primary)
        for(uint32_t idx:e.props)
            if(idx>0 && idx<=allProps.size() && allProps[idx-1].type=="irot")
                notHalf[allProps[idx-1].data]=0;
    if(HeicLosslessNative::PrimaryHasIrot180(notHalf))
        throw std::runtime_error("Non-180 HEIF incorrectly authenticated");
    bool rejected=false;
    try { auto unsafe=HeicLosslessNative::Transform(notHalf,1); (void)unsafe; }
    catch(const std::runtime_error&) {rejected=true;}
    if(!rejected)throw std::runtime_error("Exif3 without irot2 was accepted");
    printf("NATIVE_IROT2_EXIF3_BYTE_EXACT_ROTATE_AND_FAILCLOSED_PASS\n");

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
        if(!HeicLosslessNative::HasLinkedVisualItems(withRelated))
            throw std::runtime_error("Related HEIF image not detected for safety guard");
        if(!PhotosPureRotationBridge::UnsafeToDiscardLinkedVisualsAfterExactMatch(withRelated))
            throw std::runtime_error("Concurrent source revision could silently discard linked HDR/thumbnail");
        if(PhotosPureRotationBridge::UnsafeToDiscardLinkedVisualsAfterExactMatch(initial))
            throw std::runtime_error("Simple HEIC wrongly marked as linked-visual-unsafe");
        if(!PhotosPureRotationBridge::UnsafeToDiscardLinkedVisualsAfterExactMatch(
                HeicLosslessNative::Bytes{0,1,2,3}))
            throw std::runtime_error("Invalid source graph must be treated as unsafe");
        bool rejected=false;
        try { auto unsafe=HeicLosslessNative::Transform(withRelated,1);(void)unsafe; }
        catch(const std::runtime_error&) { rejected=true; }
        if(!rejected)throw std::runtime_error("Unsafe thumbnail/HDR rotation accepted");
    }
    printf("NATIVE_RELATED_VISUAL_FALLBACK_GUARD_DETECTED_PASS\\n");
    // In-memory synthetic HEIF reference graph: a thumbnail (item 50)
    // references primary (item 49) and BOTH share exactly one irot property.
    // This layout is safe: one irot change rotates both, with unchanged HEVC.
    const HeicLosslessNative::Bytes refs={
        0,0,0,0,                    // iref fullbox v0
        0,0,0,14,'t','h','m','b',    // 14-byte reference box
        0,50,0,1,0,49              // from 50 -> one target: primary 49
    };
    const HeicLosslessNative::Box iref{"iref",0,refs.size(),0};
    const auto linked=HeicLosslessNative::LinkedVisualItems(refs,iref,49);
    if(linked.size()!=1 || linked[0]!=50)
        throw std::runtime_error("Synthetic linked HEIF thumbnail graph incorrect");
    const std::vector<HeicLosslessNative::Box> properties={
        {"irot",0,9,8}
    };
    const std::vector<HeicLosslessNative::Association> shared={
        {49,{1}},{50,{1}}
    };
    HeicLosslessNative::ValidateSharedRotation(
        refs,properties,shared,linked,49,1);
    bool rejectedMissing=false;
    try {
        const std::vector<HeicLosslessNative::Association> missing={
            {49,{1}},{50,{}}
        };
        HeicLosslessNative::ValidateSharedRotation(
            refs,properties,missing,linked,49,1);
    } catch(const std::runtime_error&) {rejectedMissing=true;}
    if(!rejectedMissing)
        throw std::runtime_error("Related HEIF thumbnail without irot accepted");
    bool rejectedUnrelated=false;
    try {
        const std::vector<HeicLosslessNative::Association> unrelated={
            {49,{1}},{50,{1}},{60,{1}}
        };
        HeicLosslessNative::ValidateSharedRotation(
            refs,properties,unrelated,linked,49,1);
    } catch(const std::runtime_error&) {rejectedUnrelated=true;}
    if(!rejectedUnrelated)
        throw std::runtime_error("Unrelated HEIF item sharing irot accepted");
    printf("NATIVE_SHARED_IROT_RELATED_ONLY_SAFETY_PASS\n");
    printf("NATIVE_LOSSLESS_HEIF_MATCHES_PYTHON_AND_ROTATION_TEST_PASSED\n");
}
}
