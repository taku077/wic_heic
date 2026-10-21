// CI-ONLY native JPEG / HEIC Shell comparison.
// The JPEG is encoded from the exact WIC raster decoded from each checked-in
// artificial HEIC. Each pair uses fresh, unique paths and alternating order.
// Not an Explorer UI benchmark. No user files or .heic associations touched.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <thumbcache.h>
#include <shobjidl.h>
#include <shlobj_core.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../wic_heic/wic_heic.h"

using Microsoft::WRL::ComPtr;
namespace {
constexpr int kCases=10;
constexpr UINT kTarget=256;
const CLSID kCacheClsid={0x50ef4544,0xac9f,0x4a8e,
                       {0xb2,0x1b,0x8a,0x26,0x18,0x0d,0xb1,0x3f}};
using Clock=std::chrono::steady_clock;

void Require(bool ok,const char* step) {
    if(!ok){std::printf("SHELL_JPEG_HEIC_ERROR step=%s\n",step);
            throw std::runtime_error(step);}
}
void Check(HRESULT hr,const char* step) {
    if(FAILED(hr)){std::printf("SHELL_JPEG_HEIC_ERROR step=%s hr=0x%08X\n",
                        step,unsigned(hr));throw std::runtime_error(step);}
}
struct CoSession {
    CoSession(){Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"COM initialize");}
    ~CoSession(){CoUninitialize();}
};
std::wstring Environment(const wchar_t* name) {
    DWORD size=GetEnvironmentVariableW(name,nullptr,0);
    if(!size||size>32767)return {};
    std::wstring value(size,L'\0');
    DWORD got=GetEnvironmentVariableW(name,&value[0],size);
    if(!got||got>=size)return {};
    value.resize(got);return value;
}
std::wstring FullPath(const std::wstring& value) {
    DWORD n=GetFullPathNameW(value.c_str(),0,nullptr,nullptr);
    Require(n>0&&n<32767,"invalid path length");
    std::wstring result(n,L'\0');
    const DWORD length=GetFullPathNameW(value.c_str(),n,&result[0],nullptr);
    Require(length>0&&length<n,"path normalization failed");
    result.resize(length);
    while(!result.empty()&&(result.back()==L'\\'||result.back()==L'/'))
        result.pop_back();
    return result;
}
void GuardArtificialCiFolder(const std::wstring& folder) {
    Require(Environment(L"GITHUB_ACTIONS")==L"true","GitHub Actions only");
    const auto runner=FullPath(Environment(L"RUNNER_TEMP"));
    const auto actual=FullPath(folder);
    Require(!runner.empty()&&actual.size()>runner.size()+16,"short benchmark folder");
    Require(_wcsnicmp(actual.c_str(),runner.c_str(),runner.size())==0 &&
            actual[runner.size()]==L'\\',"outside isolated runner temp");
    const std::wstring tail=actual.substr(runner.size()+1);
    Require(tail.rfind(L"wic_shell_trial_",0)==0 &&
            tail.find_first_of(L"\\/")==std::wstring::npos,
            "not an expected artificial trial directory");
    DWORD attrs=GetFileAttributesW(actual.c_str());
    Require(attrs!=INVALID_FILE_ATTRIBUTES &&
            (attrs&FILE_ATTRIBUTE_DIRECTORY)!=0 &&
            (attrs&FILE_ATTRIBUTE_REPARSE_POINT)==0,
            "invalid or reparse-point folder");
}
std::wstring SyntheticPath(const std::wstring& root,int index,const wchar_t* ext,
                           const wchar_t* prefix) {
    wchar_t name[96]{};
    swprintf_s(name,L"%ls_%02d.%ls",prefix,index,ext);
    return root+L"\\"+name;
}
void MakeJpegFromHeic(IWICImagingFactory* factory,
                      const std::wstring& heic,const std::wstring& jpeg) {
    ComPtr<IWICStream> input;
    Check(factory->CreateStream(input.GetAddressOf()),"HEIC input stream");
    Check(input->InitializeFromFilename(heic.c_str(),GENERIC_READ),"open HEIC");
    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoder(GUID_ContainerFormatHEIC,
          &GUID_VendorPrsyahmi,decoder.GetAddressOf()),"create HEIC decoder");
    Check(decoder->Initialize(input.Get(),WICDecodeMetadataCacheOnDemand),
          "decode HEIC");
    ComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,source.GetAddressOf()),"decode HEIC frame");
    UINT width=0,height=0;
    Check(source->GetSize(&width,&height),"HEIC frame dimensions");
    Require(width>0&&height>0&&width<=10000&&height<=10000,
            "HEIC frame unreasonable");
    ComPtr<IWICFormatConverter> conversion;
    Check(factory->CreateFormatConverter(conversion.GetAddressOf()),
          "24-bit JPEG source converter");
    Check(conversion->Initialize(source.Get(),GUID_WICPixelFormat24bppBGR,
          WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),
          "convert source to 24-bit BGR");
    ComPtr<IWICStream> output;
    Check(factory->CreateStream(output.GetAddressOf()),"JPEG output stream");
    Check(output->InitializeFromFilename(jpeg.c_str(),GENERIC_WRITE),
          "create CI JPEG");
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,
          encoder.GetAddressOf()),"create Windows JPEG encoder");
    Check(encoder->Initialize(output.Get(),WICBitmapEncoderNoCache),
          "initialize JPEG encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(frame.GetAddressOf(),nullptr),
          "create JPEG frame");
    Check(frame->Initialize(nullptr),"initialize JPEG frame");
    Check(frame->SetSize(width,height),"set JPEG source dimensions");
    WICPixelFormatGUID fmt=GUID_WICPixelFormat24bppBGR;
    Check(frame->SetPixelFormat(&fmt),"set JPEG pixel format");
    Require(IsEqualGUID(fmt,GUID_WICPixelFormat24bppBGR),
            "JPEG encoder changed pixel format");
    Check(frame->WriteSource(conversion.Get(),nullptr),"write JPEG pixels");
    Check(frame->Commit(),"commit JPEG frame");
    Check(encoder->Commit(),"commit JPEG file");
    frame.Reset();encoder.Reset();output.Reset();
    // Confirm the generated JPEG can be independently decoded by WIC and
    // has exactly the source raster dimensions.
    ComPtr<IWICBitmapDecoder> checkDecoder;
    Check(factory->CreateDecoderFromFilename(jpeg.c_str(),nullptr,GENERIC_READ,
          WICDecodeMetadataCacheOnDemand,checkDecoder.GetAddressOf()),
          "reopen generated JPEG");
    ComPtr<IWICBitmapFrameDecode> checkFrame;
    Check(checkDecoder->GetFrame(0,checkFrame.GetAddressOf()),
          "reopen JPEG frame");
    UINT jw=0,jh=0;
    Check(checkFrame->GetSize(&jw,&jh),"JPEG dimensions");
    Require(jw==width&&jh==height,"HEIC/JPEG source dimension mismatch");
}
// An in-process WIC breakdown (not Shell or Explorer UI time). These timings
// run only AFTER both Shell first-extraction and cache-only phases, so they
// cannot warm the code or file system ahead of the Shell comparisons.
struct WicTiming {
    double create=0,initialize=0,setup=0,copy=0,total=0;
};
WicTiming ProfileWic(IWICImagingFactory* factory,const std::wstring& file,bool heic) {
    const auto t0=Clock::now();
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(stream.GetAddressOf()),"timed WIC stream");
    Check(stream->InitializeFromFilename(file.c_str(),GENERIC_READ),
          "timed WIC open");
    ComPtr<IWICBitmapDecoder> decoder;
    if(heic) {
        Check(factory->CreateDecoder(GUID_ContainerFormatHEIC,
              &GUID_VendorPrsyahmi,decoder.GetAddressOf()),
              "timed direct HEIC decoder");
    } else {
        Check(factory->CreateDecoder(GUID_ContainerFormatJpeg,nullptr,
              decoder.GetAddressOf()),"timed direct JPEG decoder");
    }
    const auto t1=Clock::now();
    Check(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand),
          "timed WIC initialize");
    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0,frame.GetAddressOf()),"timed WIC frame");
    UINT w=0,h=0;
    Check(frame->GetSize(&w,&h),"timed WIC source bounds");
    Require(w>0 && h>0 && w<=10000 && h<=10000,
            "timed WIC unreasonable dimensions");
    const auto t2=Clock::now();
    const UINT maxSide=(std::max)(w,h);
    const UINT tw=(std::max)(1u,UINT(uint64_t(w)*kTarget/maxSide));
    const UINT th=(std::max)(1u,UINT(uint64_t(h)*kTarget/maxSide));
    const uint64_t bytes=uint64_t(tw)*th*4u;
    Require(bytes<=16*1024*1024 && bytes<=UINT_MAX,
            "timed WIC output too large");
    ComPtr<IWICBitmapScaler> scaler;
    Check(factory->CreateBitmapScaler(scaler.GetAddressOf()),"timed WIC scaler");
    Check(scaler->Initialize(frame.Get(),tw,th,WICBitmapInterpolationModeFant),
          "timed WIC scaling");
    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(converter.GetAddressOf()),
          "timed WIC converter");
    Check(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,
          WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),
          "timed WIC conversion");
    const auto t3=Clock::now();
    std::vector<BYTE> pixels(static_cast<size_t>(bytes));
    Check(converter->CopyPixels(nullptr,tw*4u,UINT(bytes),pixels.data()),
          "timed WIC CopyPixels");
    const auto t4=Clock::now();
    auto Ms=[](Clock::time_point start,Clock::time_point end) {
        return std::chrono::duration<double,std::milli>(end-start).count();
    };
    WicTiming result;
    result.create=Ms(t0,t1);
    result.initialize=Ms(t1,t2);
    result.setup=Ms(t2,t3);
    result.copy=Ms(t3,t4);
    result.total=Ms(t0,t4);
    return result;
}
// Read-only availability probe. Never substitute these embedded thumbnails
// for the main image until orientation/color/quality parity is demonstrated.
bool ReportEmbeddedThumbnail(IWICImagingFactory* factory,
                             const std::wstring& path,int index) {
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(stream.GetAddressOf()),"embedded stream");
    Check(stream->InitializeFromFilename(path.c_str(),GENERIC_READ),
          "embedded stream open");
    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoder(GUID_ContainerFormatHEIC,
          &GUID_VendorPrsyahmi,decoder.GetAddressOf()),
          "embedded decoder creation");
    Check(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand),
          "embedded decoder initialization");
    ComPtr<IWICBitmapSource> thumb;
    const auto start=Clock::now();
    const HRESULT hr=decoder->GetThumbnail(thumb.GetAddressOf());
    UINT width=0,height=0;
    if(SUCCEEDED(hr)) {
        Require(thumb!=nullptr,"embedded thumbnail success without source");
        Check(thumb->GetSize(&width,&height),"embedded thumbnail dimensions");
        Require(width>0&&height>0&&width<=10000&&height<=10000,
                "embedded thumbnail invalid dimensions");
    }
    const auto end=Clock::now();
    const double ms=std::chrono::duration<double,std::milli>(end-start).count();
    std::printf("SHELL_JPEG_HEIC_EMBEDDED_PROBE index=%d available=%u width=%u height=%u hr=0x%08X lookup_ms=%.2f\n",
          index,SUCCEEDED(hr)?1u:0u,width,height,unsigned(hr),ms);
    return SUCCEEDED(hr);
}
struct Extraction {
    double milliseconds=0;
    UINT width=0,height=0;
    WTS_CACHEFLAGS cacheFlags{};
};
Extraction ShellThumbnail(IThumbnailCache* cache,const std::wstring& path,
                          bool cacheOnly) {
    ComPtr<IShellItem> item;
    Check(SHCreateItemFromParsingName(path.c_str(),nullptr,
          IID_PPV_ARGS(item.GetAddressOf())),"Shell item");
    ComPtr<ISharedBitmap> shared;
    WTS_THUMBNAILID id{};
    WTS_CACHEFLAGS flags{};
    const auto start=Clock::now();
    HRESULT hr=cache->GetThumbnail(item.Get(),kTarget,
        cacheOnly?WTS_INCACHEONLY:WTS_EXTRACT,
        shared.GetAddressOf(),&flags,&id);
    const auto end=Clock::now();
    Check(hr,cacheOnly?"Shell cached read":"Shell first extraction");
    Require(shared!=nullptr,"Shell provided null thumbnail");
    HBITMAP bitmap=nullptr;
    Check(shared->GetSharedBitmap(&bitmap),"Shell shared HBITMAP");
    Require(bitmap!=nullptr,"Shell shared bitmap null");
    BITMAP bm{};
    Require(GetObjectW(bitmap,sizeof(bm),&bm)==sizeof(bm) &&
            bm.bmWidth>0&&bm.bmHeight>0,"Shell bitmap dimensions");
    Extraction r;
    r.milliseconds=std::chrono::duration<double,std::milli>(end-start).count();
    r.width=UINT(bm.bmWidth);r.height=UINT(bm.bmHeight);r.cacheFlags=flags;
    return r;
}
struct Summary {
    double median=0,p90=0,mean=0,min=0,max=0;
};
Summary Summarize(std::vector<double> values) {
    Require(!values.empty(),"missing timing samples");
    std::sort(values.begin(),values.end());
    Summary s;
    s.min=values.front();s.max=values.back();
    const size_t n=values.size();
    s.median=n%2?values[n/2]:(values[n/2-1]+values[n/2])/2;
    s.p90=values[size_t(std::ceil(double(n)*0.9))-1];
    for(double value:values)s.mean+=value;
    s.mean/=double(n);
    return s;
}
void PrintSummary(const char* type,const char* phase,
                  const std::vector<double>& values) {
    Summary s=Summarize(values);
    std::printf("SHELL_JPEG_HEIC_SUMMARY format=%s phase=%s count=%zu median_ms=%.2f p90_ms=%.2f mean_ms=%.2f min_ms=%.2f max_ms=%.2f\n",
        type,phase,values.size(),s.median,s.p90,s.mean,s.min,s.max);
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        Require(argc==2,"usage: ShellJpegComparison.exe <CI artificial trial directory>");
        const auto root=FullPath(argv[1]);
        GuardArtificialCiFolder(root);
        CoSession com;
        ComPtr<IWICImagingFactory> factory;
        Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
              IID_PPV_ARGS(factory.GetAddressOf())),"WIC imaging factory");
        ComPtr<IThumbnailCache> cache;
        Check(CoCreateInstance(kCacheClsid,nullptr,CLSCTX_INPROC_SERVER,
              IID_PPV_ARGS(cache.GetAddressOf())),"Windows Shell thumbnail cache");

        std::vector<std::wstring> heicPaths,jpegPaths;
        for(int i=0;i<kCases;++i) {
            const auto source=SyntheticPath(root,i,L"wictrial",L"synthetic");
            const auto heic=SyntheticPath(root,i,L"wictrial",L"paired");
            const auto jpeg=SyntheticPath(root,i,L"jpg",L"paired");
            Require(GetFileAttributesW(source.c_str())!=INVALID_FILE_ATTRIBUTES,
                    "missing synthetic source");
            Require(GetFileAttributesW(heic.c_str())==INVALID_FILE_ATTRIBUTES &&
                    GetFileAttributesW(jpeg.c_str())==INVALID_FILE_ATTRIBUTES,
                    "refuse to replace existing pair files");
            Require(CopyFileW(source.c_str(),heic.c_str(),TRUE)!=FALSE,
                    "make new isolated HEIC pair");
            MakeJpegFromHeic(factory.Get(),source,jpeg);
            heicPaths.push_back(heic);jpegPaths.push_back(jpeg);
        }
        std::printf("SHELL_JPEG_HEIC_PAIRS_CREATED=%d SOURCE=decoded_checked_in_HEIC JPEG_ENCODING=Windows_WIC_24bppBGR\n",kCases);
        std::vector<double> heicCold,jpegCold,heicWarm,jpegWarm;
        std::vector<double> matchedHeicCold,matchedJpegCold;
        std::vector<double> matchedHeicWarm,matchedJpegWarm;
        std::vector<bool> sameSize(kCases,false);
        for(int i=0;i<kCases;++i) {
            // Alternate which format is asked for first, to limit order bias.
            const bool jpegFirst=(i%2)==0;
            Extraction h,j;
            if(jpegFirst) {
                j=ShellThumbnail(cache.Get(),jpegPaths[i],false);
                h=ShellThumbnail(cache.Get(),heicPaths[i],false);
            } else {
                h=ShellThumbnail(cache.Get(),heicPaths[i],false);
                j=ShellThumbnail(cache.Get(),jpegPaths[i],false);
            }
            heicCold.push_back(h.milliseconds);jpegCold.push_back(j.milliseconds);
            sameSize[i]=(h.width==j.width && h.height==j.height);
            if(sameSize[i]){
                matchedHeicCold.push_back(h.milliseconds);
                matchedJpegCold.push_back(j.milliseconds);
            }
            std::printf("SHELL_JPEG_HEIC_SAMPLE index=%d order=%s jpeg_cold_ms=%.2f heic_cold_ms=%.2f jpeg_size=%ux%u heic_size=%ux%u\n",
                i,jpegFirst?"JPEG_FIRST":"HEIC_FIRST",j.milliseconds,h.milliseconds,
                j.width,j.height,h.width,h.height);
        }
        // All cache reads happen *after* first extraction. No regeneration
        // should be required. The Shell's own cache flags/bitmap validate this.
        for(int i=0;i<kCases;++i) {
            auto h=ShellThumbnail(cache.Get(),heicPaths[i],true);
            auto j=ShellThumbnail(cache.Get(),jpegPaths[i],true);
            heicWarm.push_back(h.milliseconds);jpegWarm.push_back(j.milliseconds);
            if(sameSize[i]){
                matchedHeicWarm.push_back(h.milliseconds);
                matchedJpegWarm.push_back(j.milliseconds);
            }
        }
        const size_t matchedCount=matchedHeicCold.size();
        std::printf("SHELL_JPEG_HEIC_GEOMETRY_FILTER equal_dimensions=%zu unequal_dimensions=%zu\n",
                    matchedCount,size_t(kCases)-matchedCount);
        // The 100x100 sources can be enlarged by the experimental HEIC
        // handler but left at native size by the Windows JPEG handler. Such
        // pairs cannot support an apples-to-apples thumbnail comparison.
        Require(matchedCount>=5,"insufficient equal-dimension comparisons");
        PrintSummary("HEIC","first_matched_geometry",matchedHeicCold);
        PrintSummary("JPEG","first_matched_geometry",matchedJpegCold);
        PrintSummary("HEIC","cache_only_matched_geometry",matchedHeicWarm);
        PrintSummary("JPEG","cache_only_matched_geometry",matchedJpegWarm);
        PrintSummary("HEIC","first_all_including_unequal_dimensions",heicCold);
        PrintSummary("JPEG","first",jpegCold);
        PrintSummary("HEIC","cache_only",heicWarm);
        PrintSummary("JPEG","cache_only",jpegWarm);
        // Dissect direct WIC overhead in-process, using only pairs where
        // both Shell routes returned exactly the same bitmap geometry.
        std::vector<double> hCreate,hInit,hSetup,hCopy,hTotal;
        std::vector<double> jCreate,jInit,jSetup,jCopy,jTotal;
        for(int i=0;i<kCases;++i) {
            if(!sameSize[i])continue;
            WicTiming ht,jt;
            if(i%2==0) {
                jt=ProfileWic(factory.Get(),jpegPaths[i],false);
                ht=ProfileWic(factory.Get(),heicPaths[i],true);
            } else {
                ht=ProfileWic(factory.Get(),heicPaths[i],true);
                jt=ProfileWic(factory.Get(),jpegPaths[i],false);
            }
            hCreate.push_back(ht.create);hInit.push_back(ht.initialize);
            hSetup.push_back(ht.setup);hCopy.push_back(ht.copy);
            hTotal.push_back(ht.total);
            jCreate.push_back(jt.create);jInit.push_back(jt.initialize);
            jSetup.push_back(jt.setup);jCopy.push_back(jt.copy);
            jTotal.push_back(jt.total);
        }
        PrintSummary("HEIC","direct_wic_stream_decoder_create",hCreate);
        PrintSummary("JPEG","direct_wic_stream_decoder_create",jCreate);
        PrintSummary("HEIC","direct_wic_initialize_frame",hInit);
        PrintSummary("JPEG","direct_wic_initialize_frame",jInit);
        PrintSummary("HEIC","direct_wic_scaler_converter_setup",hSetup);
        PrintSummary("JPEG","direct_wic_scaler_converter_setup",jSetup);
        PrintSummary("HEIC","direct_wic_copy_pixels",hCopy);
        PrintSummary("JPEG","direct_wic_copy_pixels",jCopy);
        PrintSummary("HEIC","direct_wic_total",hTotal);
        PrintSummary("JPEG","direct_wic_total",jTotal);
        std::puts("SHELL_JPEG_HEIC_WIC_PHASES_PASS");
        UINT embeddedCount=0;
        for(int i=0;i<kCases;++i) {
            embeddedCount+=ReportEmbeddedThumbnail(factory.Get(),heicPaths[i],i)?1u:0u;
        }
        std::printf("SHELL_JPEG_HEIC_EMBEDDED_COUNT=%u OF=%d\n",embeddedCount,kCases);
        std::puts("SHELL_JPEG_HEIC_EMBEDDED_AVAILABILITY_PROBE_PASS");
        std::puts("SHELL_JPEG_HEIC_CACHE_REVISIT_PASS");
        std::puts("SHELL_JPEG_HEIC_FAIR_SOURCE_PROBE_PASS");
        return 0;
    }catch(const std::exception& e) {
        std::printf("SHELL_JPEG_HEIC_FAIR_SOURCE_PROBE_FAILED step=%s\n",e.what());
        return 1;
    }
}
