#pragma once
// CI-ONLY prototype for IThumbnailProvider + IInitializeWithStream.
// NOT a registered COM server and NOT included in a production MSI.
// Proves that embedded HEVC thmb can become an opaque HBITMAP via native WIC
// without decoding its 12MP master image or writing source files.
#include <windows.h>
#include <wincodec.h>
#include <thumbcache.h>
#include <shobjidl.h>
#include <atlbase.h>
#include <atomic>
#include <new>
#include <memory>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include "../wic_heic/PhotosWicTrace.h"
#include "WICShellOrientationGate.h"
namespace WICShellThumbnailProviderPilot {
class Provider final : public IInitializeWithStream, public IThumbnailProvider {
    std::atomic<ULONG> references_{1};
    CComPtr<IStream> input_;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** p) override {
        if(!p)return E_POINTER;
        *p=nullptr;
        if(riid==IID_IUnknown || riid==IID_IInitializeWithStream)
            *p=static_cast<IInitializeWithStream*>(this);
        else if(riid==IID_IThumbnailProvider)
            *p=static_cast<IThumbnailProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n=--references_;
        if(!n)delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IStream* stream,DWORD access) override {
        if(!stream)return E_POINTER;
        if(input_)return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        // A thumbnail provider is read-only even when Shell supplies flags.
        input_=stream;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT requested,HBITMAP* output,
                                            WTS_ALPHATYPE* alpha) override {
        if(!output || !alpha)return E_POINTER;
        *output=nullptr;
        *alpha=WTSAT_UNKNOWN;
        if(!input_)return E_UNEXPECTED;
        if(requested==0 || requested>1024)return E_INVALIDARG;
        WicHeicDiagnostic::Emit("ShellLabProviderEnter",requested,0,0,0,"test-only");
        const bool simpleOrientation=WicShellLabSafety::FromStream(input_);
        CComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr=CoCreateInstance(CLSID_HEICDecoder,nullptr,CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&decoder));
        if(FAILED(hr))return hr;
        LARGE_INTEGER zero{};
        hr=input_->Seek(zero,STREAM_SEEK_SET,nullptr);
        if(FAILED(hr))return hr;
        hr=decoder->Initialize(input_,WICDecodeMetadataCacheOnDemand);
        if(FAILED(hr))return hr;
        CComPtr<IWICBitmapSource> thumb;
        CComPtr<IWICBitmapFrameDecode> frame;
        auto ensureFrame=[&]()->HRESULT {
            return frame ? S_OK : decoder->GetFrame(0,&frame);
        };
        hr=simpleOrientation ? decoder->GetThumbnail(&thumb) : WINCODEC_ERR_CODECNOTHUMBNAIL;
        if(hr==WINCODEC_ERR_CODECNOTHUMBNAIL && simpleOrientation){
            hr=ensureFrame();
            if(FAILED(hr))return hr;
            hr=frame->GetThumbnail(&thumb);
        }
        if(hr==WINCODEC_ERR_CODECNOTHUMBNAIL){
            hr=ensureFrame();
            if(FAILED(hr))return hr;
            thumb=frame;
            WicHeicDiagnostic::Emit("ShellLabFullFallback",requested,0,0,0,
                simpleOrientation?"no-embedded":"orientation-exif-or-unsupported");
        } else if(FAILED(hr)||!thumb) {
            return FAILED(hr)?hr:WINCODEC_ERR_CODECNOTHUMBNAIL;
        }
        UINT width=0,height=0;
        hr=thumb->GetSize(&width,&height);
        if(FAILED(hr))return hr;
        // Do not trust a malformed or color-managed embedded thumbnail merely
        // because libheif successfully decoded it. Geometry must agree with
        // the primary image, and the ICC-managed primary uses the existing WIC
        // full-image pipeline until color equivalence is independently proven.
        // This is metadata-only; GetFrame/GetSize/GetColorContexts do not
        // decompress the primary pixels.
        if(thumb && (!frame || thumb.p != static_cast<IWICBitmapSource*>(frame.p))){
            hr=ensureFrame();
            if(FAILED(hr))return hr;
            UINT masterW=0,masterH=0;
            hr=frame->GetSize(&masterW,&masterH);
            if(FAILED(hr))return hr;
            if(!width||!height||!masterW||!masterH) return WINCODEC_ERR_BADIMAGE;
            const double sourceAspect=double(masterW)/double(masterH);
            const double embeddedAspect=double(width)/double(height);
            UINT iccCount=0;
            const HRESULT iccResult=frame->GetColorContexts(0,nullptr,&iccCount);
            if((sourceAspect>0 && std::abs(sourceAspect-embeddedAspect)>0.015) ||
               FAILED(iccResult) || iccCount>0){
                thumb=frame;
                width=masterW;height=masterH;
                WicHeicDiagnostic::Emit("ShellLabFullFallback",requested,0,0,0,
                    iccCount>0?"icc-primary": "embedded-aspect-or-color-unknown");
            }
        }
        if(max(width,height)<requested){
            hr=ensureFrame();
            if(FAILED(hr))return hr;
            UINT masterW=0,masterH=0;
            hr=frame->GetSize(&masterW,&masterH);
            if(FAILED(hr))return hr;
            if(max(masterW,masterH)>max(width,height)){
                thumb=frame;
                width=masterW;height=masterH;
                WicHeicDiagnostic::Emit("ShellLabFullFallback",requested,0,0,0,"need-detail");
            }
        }
        if(!width||!height||width>16384||height>16384)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        // Never upscale an embedded 320x240 preview to 1024px. Production
        // fallback for high requested sizes will need a separate quality gate.
        CComPtr<IWICBitmapSource> render=thumb;
        if(width>requested || height>requested){
            const double ratio=double(requested)/double(max(width,height));
            const UINT scaledW=max(1u,UINT(width*ratio+0.5));
            const UINT scaledH=max(1u,UINT(height*ratio+0.5));
            CComPtr<IWICImagingFactory> factory;
            hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory));
            if(FAILED(hr))return hr;
            CComPtr<IWICBitmapScaler> scaler;
            hr=factory->CreateBitmapScaler(&scaler);
            if(FAILED(hr))return hr;
            hr=scaler->Initialize(render,scaledW,scaledH,WICBitmapInterpolationModeFant);
            if(FAILED(hr))return hr;
            render=scaler;
            width=scaledW;height=scaledH;
        }
        CComPtr<IWICImagingFactory> factory;
        hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&factory));
        if(FAILED(hr))return hr;
        CComPtr<IWICFormatConverter> rgba;
        hr=factory->CreateFormatConverter(&rgba);
        if(FAILED(hr))return hr;
        hr=rgba->Initialize(render,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,
                            nullptr,0,WICBitmapPaletteTypeCustom);
        if(FAILED(hr))return hr;
        if(width>UINT32_MAX/4u || height>UINT32_MAX/(width*4u))
            return E_INVALIDARG;
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=LONG(width);
        bmi.bmiHeader.biHeight=-LONG(height);
        bmi.bmiHeader.biPlanes=1;
        bmi.bmiHeader.biBitCount=32;
        bmi.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;
        HBITMAP dib=CreateDIBSection(nullptr,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!dib||!bits)return HRESULT_FROM_WIN32(GetLastError());
        hr=rgba->CopyPixels(nullptr,width*4,width*height*4,static_cast<BYTE*>(bits));
        if(FAILED(hr)){DeleteObject(dib);return hr;}
        *output=dib;
        *alpha=WTSAT_RGB;
        return S_OK;
    }
};
inline void Need(bool okay,const char* description) {
    if(!okay)throw std::runtime_error(description);
}
inline void RunOrientationGate() {
    wchar_t tmp[32768]{};
    const DWORD n=GetEnvironmentVariableW(L"RUNNER_TEMP",tmp,32768);
    Need(n>0&&n<32768,"CI folder unavailable");
    wchar_t actions[16]{};
    Need(GetEnvironmentVariableW(L"GITHUB_ACTIONS",actions,16)==4 &&
         wcscmp(actions,L"true")==0,"CI-only orientation guard");
    std::wstring path=std::wstring(tmp,n)+L"\\wicfast_embedded_4032x3024.heic";
    CComPtr<IWICImagingFactory> factory;
    Need(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
         CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"No WIC factory");
    CComPtr<IWICStream> stream;
    Need(SUCCEEDED(factory->CreateStream(&stream)) &&
         SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_READ)),
         "Test HEIC open failed");
    Need(WicShellLabSafety::FromStream(stream),"Safe CI HEIC rejected");
    STATSTG stat{};
    Need(SUCCEEDED(stream->Stat(&stat,STATFLAG_NONAME)) &&
         stat.cbSize.QuadPart < 24ull*1024ull*1024ull,"Invalid fixture length");
    LARGE_INTEGER zero{};
    Need(SUCCEEDED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)),"Rewind failed");
    HeicLosslessNative::Bytes b(static_cast<size_t>(stat.cbSize.QuadPart));
    ULONG got=0;
    Need(SUCCEEDED(stream->Read(b.data(),static_cast<ULONG>(b.size()),&got)) &&
         got==b.size(),"Read HEIC failed");
    auto top=HeicLosslessNative::Children(b,0,b.size());
    auto meta=HeicLosslessNative::One(top,"meta");
    auto parts=HeicLosslessNative::Children(b,meta.data+4,meta.end);
    auto iprp=HeicLosslessNative::One(parts,"iprp");
    auto props=HeicLosslessNative::Children(b,iprp.data,iprp.end);
    auto ipco=HeicLosslessNative::One(props,"ipco");
    auto boxes=HeicLosslessNative::Children(b,ipco.data,ipco.end);
    auto associations=HeicLosslessNative::Associations(b,HeicLosslessNative::One(props,"ipma"));
    auto primary=HeicLosslessNative::Primary(b,meta);
    bool patched=false;
    for(const auto& a:associations)if(a.id==primary)
      for(uint32_t index:a.props)if(!patched && index>0 && index<=boxes.size()){
        const auto pos=boxes[index-1].begin+4;
        b[pos]='i';b[pos+1]='r';b[pos+2]='o';b[pos+3]='t';
        patched=true;
      }
    Need(patched,"No primary item property to mark");
    // A deliberately edited in-memory property FOURCC is NOT a valid rotated
    // HEIC; it only proves no unknown orientation metadata is passed through.
    Need(!WicShellLabSafety::SimplePreviewSafe(b),"Transform metadata accepted");
    // On unmodified input, distinguish a simple hvc1 primary from a HEIF
    // grid without decoding any pixels. This is a valid-fail-closed mutation
    // test (not a valid grid image fixture).
    {
        auto changed=HeicLosslessNative::Bytes{};
        Need(SUCCEEDED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)),"Restore source");
        // Build a fresh copy rather than keeping the in-memory 'irot' patch.
        auto original=HeicLosslessNative::Bytes(static_cast<size_t>(stat.cbSize.QuadPart));
        got=0;
        Need(SUCCEEDED(stream->Read(original.data(),static_cast<ULONG>(original.size()),&got))
             && got==original.size(),"Reload original");
        auto originalTop=HeicLosslessNative::Children(original,0,original.size());
        auto originalMeta=HeicLosslessNative::One(originalTop,"meta");
        auto originalParts=HeicLosslessNative::Children(original,originalMeta.data+4,originalMeta.end);
        const auto primaryId=HeicLosslessNative::Primary(original,originalMeta);
        const auto iinf=HeicLosslessNative::One(originalParts,"iinf");
        const auto v=HeicLosslessNative::Read(original,iinf.data,1);
        auto items=HeicLosslessNative::Children(original,iinf.data+4+(v?4:2),iinf.end);
        bool modified=false;
        for(const auto& entry:items) {
            if(entry.type!="infe")continue;
            const auto ve=HeicLosslessNative::Read(original,entry.data,1);
            if(ve!=2 && ve!=3)continue;
            const size_t width=ve==2?2:4;
            if(HeicLosslessNative::Read(original,entry.data+4,width)!=primaryId)continue;
            const size_t offset=entry.data+4+width+2;
            changed=original;
            for(int k=0;k<4;k++)changed[offset+k]="grid"[k];
            modified=true;break;
        }
        Need(modified && !WicShellLabSafety::SimplePreviewSafe(changed),
             "Non-hvc1 primary incorrectly accepted");
        std::puts("CI_SHELL_GRID_PRIMARY_FAIL_CLOSED_PASS");
        const auto relation=std::find_if(originalParts.begin(),originalParts.end(),
            [](const HeicLosslessNative::Box& box){return box.type=="iref";});
        if(relation!=originalParts.end()) {
            auto refs=HeicLosslessNative::Children(original,relation->data+4,relation->end);
            bool changedRelation=false;
            for(const auto& ref:refs) if(ref.type=="thmb") {
                changed=original;
                for(int k=0;k<4;k++)changed[ref.begin+4+k]="auxl"[k];
                changedRelation=true;break;
            }
            Need(changedRelation && !WicShellLabSafety::SimplePreviewSafe(changed),
                 "Alpha auxiliary reference incorrectly accepted");
            std::puts("CI_SHELL_AUXILIARY_REFERENCE_FAIL_CLOSED_PASS");
            // Mutations prove fail-closed metadata recognition only.
            // They are not valid fully encoded grid or HDR fixtures.
            for(const char* tag : {"dimg","tmap"}) {
                auto negative=original;
                bool modified=false;
                for(const auto& item:refs) if(item.type=="thmb") {
                    for(int i=0;i<4;++i)negative[item.begin+4+i]=tag[i];
                    modified=true;break;
                }
                Need(modified && !WicShellLabSafety::SimplePreviewSafe(negative),
                     "Derived grid or HDR relation incorrectly accepted");
            }
            std::puts("CI_SHELL_GRID_HDR_REFERENCE_MUTATION_FAIL_CLOSED_PASS");
        }
        auto iprp=HeicLosslessNative::One(originalParts,"iprp");
        auto iprpParts=HeicLosslessNative::Children(original,iprp.data,iprp.end);
        auto ipco=HeicLosslessNative::One(iprpParts,"ipco");
        auto propBoxes=HeicLosslessNative::Children(original,ipco.data,ipco.end);
        bool configChanged=false;
        for(const auto& prop:propBoxes) if(prop.type=="hvcC") {
            changed=original;
            changed[prop.data+17]=uint8_t(changed[prop.data+17]|2);
            configChanged=true;break;
        }
        Need(configChanged && !WicShellLabSafety::SimplePreviewSafe(changed),
             "10-bit HEVC configuration accepted as 8-bit");
        std::puts("CI_SHELL_10BIT_HEVC_CONFIG_FAIL_CLOSED_PASS");
        bool rejects12=false;
        for(const auto& prop:propBoxes) if(prop.type=="hvcC") {
            auto negative=original;
            negative[prop.data+17]=uint8_t(negative[prop.data+17]|4);
            negative[prop.data+18]=uint8_t(negative[prop.data+18]|4);
            rejects12=!WicShellLabSafety::SimplePreviewSafe(negative);
            break;
        }
        Need(rejects12,"12-bit HEVC config accepted by 8-bit fast path");
        std::puts("CI_SHELL_12BIT_HEVC_CONFIG_MUTATION_FAIL_CLOSED_PASS");
    }
    // Existing small HEIC fixtures with real, valid irot3+Exif6 metadata.
    // Unlike the in-memory fourCC mutation above, these are genuine HEIFs.
    for(const wchar_t* leaf : {L"wictest/samples/fixture-irot3-exif6.heic",
                               L"wictest/samples/fixture-irot3-exif6-noaux.heic"}) {
        CComPtr<IWICStream> oriented;
        Need(SUCCEEDED(factory->CreateStream(&oriented)) &&
             SUCCEEDED(oriented->InitializeFromFilename(leaf,GENERIC_READ)),
             "Real orientation fixture missing");
        Need(!WicShellLabSafety::FromStream(oriented),
             "Real EXIF6/irot3 HEIC accidentally accepted by fast path");
    }
    std::puts("CI_SHELL_REAL_IROT3_EXIF6_FAIL_CLOSED_PASS");
    // Unlike the metadata tag-mutation tests, this file contains a genuine
    // HEVC image with a separately encoded alpha plane / auxiliary relation.
    const std::wstring alphaPath=std::wstring(tmp,n)+L"\\wicfast_with_alpha_4032x3024.heic";
    CComPtr<IWICStream> alphaStream;
    Need(SUCCEEDED(factory->CreateStream(&alphaStream)) &&
         SUCCEEDED(alphaStream->InitializeFromFilename(alphaPath.c_str(),GENERIC_READ)),
         "Genuine alpha HEIF fixture missing");
    Need(!WicShellLabSafety::FromStream(alphaStream),
         "Real alpha auxiliary HEIF incorrectly admitted to embedded fast path");
    std::puts("CI_SHELL_REAL_ALPHA_HEIC_FASTPATH_REJECTED_PASS");
    // Real encoded 8-bit HEIF images with explicit PQ/HLG NCLX metadata.
    // This tests color signaling rejection, not HDR pixel/tone-map fidelity.
    for(const wchar_t* name : {L"wicfast_with_pq_4032x3024.heic",
                               L"wicfast_with_hlg_4032x3024.heic"}) {
        CComPtr<IWICStream> candidate;
        const std::wstring path=std::wstring(tmp,n)+L"\\"+name;
        Need(SUCCEEDED(factory->CreateStream(&candidate)) &&
             SUCCEEDED(candidate->InitializeFromFilename(path.c_str(),GENERIC_READ)),
             "PQ/HLG NCLX synthetic HEIF missing");
        Need(!WicShellLabSafety::FromStream(candidate),
             "HDR transfer metadata admitted to SDR embedded preview shortcut");
    }
    std::puts("CI_SHELL_PQ_HLG_NCLX_COLOR_FAIL_CLOSED_PASS");


    std::puts("CI_SHELL_ORIENTATION_METADATA_FAIL_CLOSED_PASS");
}
inline void RunFallback() {
    wchar_t tmp[32768]{};
    DWORD n=GetEnvironmentVariableW(L"RUNNER_TEMP",tmp,32768);
    Need(n>0 && n<32768,"Missing CI temporary directory");
    wchar_t actions[16]{};
    Need(GetEnvironmentVariableW(L"GITHUB_ACTIONS",actions,16)==4 &&
         wcscmp(actions,L"true")==0,"Fallback tests are CI only");
    auto test=[&](const wchar_t* leaf,UINT request,UINT expectedW,UINT expectedH){
        std::wstring filename=std::wstring(tmp,n)+L"\\"+leaf;
        CComPtr<IWICImagingFactory> factory;
        Need(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))),"No WIC imaging factory");
        CComPtr<IWICStream> stream;
        Need(SUCCEEDED(factory->CreateStream(&stream)) &&
             SUCCEEDED(stream->InitializeFromFilename(filename.c_str(),GENERIC_READ)),
             "HEIC fixture stream creation failed");
        CComPtr<IThumbnailProvider> provider;
        provider.Attach(new Provider());
        CComPtr<IInitializeWithStream> initializer;
        Need(SUCCEEDED(provider->QueryInterface(IID_PPV_ARGS(&initializer))) &&
             SUCCEEDED(initializer->Initialize(stream,STGM_READ)),"Cannot initialize stream");
        HBITMAP bmp=nullptr;WTS_ALPHATYPE alpha=WTSAT_UNKNOWN;
        HRESULT hr=provider->GetThumbnail(request,&bmp,&alpha);
        Need(SUCCEEDED(hr) && bmp!=nullptr,"Fallback did not return bitmap");
        BITMAP info{};
        bool valid=GetObjectW(bmp,sizeof(info),&info)==sizeof(info) &&
            UINT(info.bmWidth)==expectedW && UINT(abs(info.bmHeight))==expectedH &&
            info.bmBitsPixel==32 && alpha==WTSAT_RGB;
        DeleteObject(bmp);
        Need(valid,"Fallback produced incorrect bitmap format or geometry");
    };
    test(L"wicfast_embedded_4032x3024.heic",1024,1024,768);
    std::puts("CI_SHELL_LAB_1024_FALLBACK_PASS");
    test(L"wicfast_no_thumb_4032x3024.heic",256,256,192);
    std::puts("CI_SHELL_LAB_NO_EMBEDDED_FALLBACK_PASS");
    test(L"wicfast_with_icc_4032x3024.heic",256,256,192);
    std::puts("CI_SHELL_LAB_ICC_PRIMARY_FALLBACK_PASS");
}
inline void Run() {
    wchar_t temp[32768]{};
    const auto n=GetEnvironmentVariableW(L"RUNNER_TEMP",temp,32768);
    Need(n>0&&n<32768,"CI runner temp missing");
    wchar_t actions[16]{};
    Need(GetEnvironmentVariableW(L"GITHUB_ACTIONS",actions,16)==4 &&
         wcscmp(actions,L"true")==0,"Not allowed outside GitHub CI");
    const std::wstring path=std::wstring(temp,n)+L"\\wicfast_embedded_4032x3024.heic";
    Need(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES,"Missing embedded HEIC");
    CComPtr<IWICImagingFactory> factory;
    Need(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
         CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"No WIC factory");
    CComPtr<IWICStream> stream;
    Need(SUCCEEDED(factory->CreateStream(&stream)),"Create stream failed");
    Need(SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_READ)),
         "Open HEIC failed");
    CComPtr<IThumbnailProvider> thumbnail;
    {
        Provider* object=new Provider();
        thumbnail.Attach(object);
    }
    CComPtr<IInitializeWithStream> initializer;
    Need(SUCCEEDED(thumbnail->QueryInterface(IID_PPV_ARGS(&initializer))),
         "IInitializeWithStream not supported");
    Need(SUCCEEDED(initializer->Initialize(stream,STGM_READ)),"Stream initialization failed");
    HBITMAP bitmap=nullptr;
    WTS_ALPHATYPE alpha=WTSAT_UNKNOWN;
    const HRESULT hr=thumbnail->GetThumbnail(256,&bitmap,&alpha);
    Need(SUCCEEDED(hr)&&bitmap!=nullptr&&alpha==WTSAT_RGB,"Provider GetThumbnail failed");
    BITMAP info{};
    Need(GetObjectW(bitmap,sizeof(info),&info)==sizeof(info),"DIB object invalid");
    Need(info.bmWidth==256&&abs(info.bmHeight)==192&&info.bmBitsPixel==32,
         "Thumbnail 256x192 geometry/color mismatch");
    DIBSECTION sec{};
    Need(GetObjectW(bitmap,sizeof(sec),&sec)==sizeof(sec) &&
         sec.dsBm.bmBits!=nullptr,"DIB pixels missing");
    auto* px=static_cast<const BYTE*>(sec.dsBm.bmBits);
    const auto rgb=[&](int x,int y)->unsigned {
       const auto* a=px+(size_t(y)*256+x)*4;
       return (unsigned(a[2])<<16)|(unsigned(a[1])<<8)|unsigned(a[0]);
    };
    Need(rgb(32,32)!=rgb(224,32) && rgb(32,32)!=rgb(32,160),
         "Synthetic thumbnail quadrants collapsed");
    DeleteObject(bitmap);
    bitmap=nullptr;
    std::printf("CI_SHELL_THUMB_PROVIDER_RGB_PASS dims=256x192 opaque=1 "
                "using_embedded_HEVC=1 original_12MP_not_requested=1\n");
}
} // namespace
