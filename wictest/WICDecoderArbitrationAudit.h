#pragma once
// Registry READ-ONLY inspection on the disposable Windows CI runner.
// Enumerates WIC decoders and tests signature patterns against the public
// synthetic HEIC. Never edits registry, file associations or codecs.
#include <windows.h>
#include <wincodec.h>
#include <atlbase.h>
#include <cstdio>
#include <stdexcept>
namespace WICDecoderArbitrationAudit {
inline void Require(HRESULT hr,const char* stage) {
    if(FAILED(hr)){
        printf("WIC_ARBITRATION_AUDIT_FAIL stage=%s hr=0x%08X\n",
               stage,(unsigned)hr);
        throw std::runtime_error(stage);
    }
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
            CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"factory");
    CComPtr<IEnumUnknown> all;
    Require(factory->CreateComponentEnumerator(WICDecoder,
            WICComponentEnumerateDefault,&all),"registry decoder enumerator");
    UINT total=0,matches=0,ours=0,foreignMatches=0,failedQueries=0;
    for(;;){
        CComPtr<IUnknown> unknown;
        ULONG obtained=0;
        HRESULT result=all->Next(1,&unknown,&obtained);
        if(result==S_FALSE||obtained==0)break;
        Require(result,"component enumeration Next");
        ++total;
        if(total>256)throw std::runtime_error("Abnormal decoder count");
        CComPtr<IWICBitmapDecoderInfo> info;
        if(FAILED(unknown->QueryInterface(IID_PPV_ARGS(&info))))continue;
        CLSID clsid{};
        if(FAILED(info->GetCLSID(&clsid)))continue;
        const bool self=IsEqualGUID(clsid,CLSID_HEICDecoder);
        if(self)++ours;
        CComPtr<IWICStream> input;
        Require(factory->CreateStream(&input),"read-only match stream");
        Require(input->InitializeFromFilename(
            L"samples\\fixture-irot3-exif6-noaux.heic",GENERIC_READ),
            "synthetic fixture open");
        BOOL matching=FALSE;
        const HRESULT checked=info->MatchesPattern(input,&matching);
        if(FAILED(checked)){++failedQueries;continue;}
        if(matching){
            ++matches;
            if(!self) {
                ++foreignMatches;
                wchar_t guid[64]{};
                if(StringFromGUID2(clsid,guid,64)<=0)
                    throw std::runtime_error("Could not format registry candidate");
                // Only a public COM CLSID from the disposable CI runner.
                // No user photo, path, process info, or private metadata.
                printf("WIC_ARBITRATION_OTHER_CLSID=%ls\n",guid);
            }
        }
    }
    if(total==0||ours!=1)
        throw std::runtime_error("HEIC codec registry mismatch in CI");
    // The selected codec must still be the custom WicHeic; this test may
    // discover competing patterns, but does not alter prioritization.
    CComPtr<IWICBitmapDecoder> selected;
    Require(factory->CreateDecoderFromFilename(
         L"samples\\fixture-irot3-exif6-noaux.heic",
         nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&selected),
         "automatic HEIC selection");
    CComPtr<IWICBitmapDecoderInfo> chosen;
    Require(selected->GetDecoderInfo(&chosen),"selected codec info");
    CLSID chosenId{};
    Require(chosen->GetCLSID(&chosenId),"selected codec CLSID");
    printf("WIC_ARBITRATION_AUDIT decoder_entries=%u "
           "signature_matches=%u other_matches=%u "
           "query_errors=%u ours=%u selected_ours=%u "
           "registry_writes=0 fixture=public_only\n",
           total,matches,foreignMatches,failedQueries,ours,
           IsEqualGUID(chosenId,CLSID_HEICDecoder)?1u:0u);
    if(!IsEqualGUID(chosenId,CLSID_HEICDecoder))
        throw std::runtime_error("Automatic HEIC selection switched codecs");
    printf("WIC_ARBITRATION_READONLY_AUDIT_PASS\n");
}
} // namespace WICDecoderArbitrationAudit
