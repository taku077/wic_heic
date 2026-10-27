#pragma once
// Windows CI-only artificial IStream A/B. Reintroduce a Seek(CUR,0) inside
// Read to emulate the removed HEIF adapter round trip, while using the exact
// same codec DLL, image bytes, process, runner, and overall test sequence.
// Not a global registry/env switch, not an installed codec behavior.
#include <atomic>
#include <algorithm>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <cstring>
namespace WICStreamSameHostABProbe {
struct Stream final : public IStream {
    std::atomic<ULONG> references{1};
    CComPtr<IStream> source;
    const bool legacy;
    ULONG reads=0,extraSeeks=0;
    explicit Stream(IStream* s,bool old) : source(s),legacy(old) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(IsEqualIID(id,IID_IUnknown)||IsEqualIID(id,IID_ISequentialStream)||
           IsEqualIID(id,IID_IStream)){
            *out=static_cast<IStream*>(this);
            AddRef();return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n=--references;if(!n)delete this;return n;
    }
    HRESULT STDMETHODCALLTYPE Read(void* p,ULONG n,ULONG* actual) override {
        ULONG copied=0;
        const HRESULT hr=source->Read(p,n,&copied);
        if(actual)*actual=copied;
        ++reads;
        if(legacy && hr==S_OK){
            LARGE_INTEGER zero{};ULARGE_INTEGER pos{};
            ++extraSeeks;
            const HRESULT seeking=source->Seek(zero,STREAM_SEEK_CUR,&pos);
            if(FAILED(seeking))return seeking;
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Write(const void* p,ULONG n,ULONG* done) override {
        return source->Write(p,n,done);
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER n,DWORD origin,
                                    ULARGE_INTEGER* pos) override {
        return source->Seek(n,origin,pos);
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER n) override {
        return source->SetSize(n);
    }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* target,ULARGE_INTEGER bytes,
                         ULARGE_INTEGER* read,ULARGE_INTEGER* written) override {
        return source->CopyTo(target,bytes,read,written);
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD flags) override {
        return source->Commit(flags);
    }
    HRESULT STDMETHODCALLTYPE Revert() override {return source->Revert();}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER offset,
             ULARGE_INTEGER cb,DWORD type) override {
        return source->LockRegion(offset,cb,type);
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER offset,
             ULARGE_INTEGER cb,DWORD type) override {
        return source->UnlockRegion(offset,cb,type);
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* st,DWORD flags) override {
        return source->Stat(st,flags);
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** dest) override {
        if(!dest)return E_POINTER;
        *dest=nullptr;
        CComPtr<IStream> clone;
        const HRESULT hr=source->Clone(&clone);
        if(FAILED(hr))return hr;
        *dest=static_cast<IStream*>(new Stream(clone,legacy));
        return S_OK;
    }
};
inline void Need(HRESULT hr,const char* stage) {
    if(FAILED(hr)){
        printf("WIC_STREAM_AB_FAIL stage=%s hr=0x%08X\n",stage,unsigned(hr));
        throw std::runtime_error(stage);
    }
}
inline LARGE_INTEGER Tick() {
    LARGE_INTEGER q{};
    if(!QueryPerformanceCounter(&q))
        throw std::runtime_error("QPC failure");
    return q;
}
inline double Ms(LARGE_INTEGER a,LARGE_INTEGER b) {
    LARGE_INTEGER freq{};
    if(!QueryPerformanceFrequency(&freq)||freq.QuadPart<=0)
        throw std::runtime_error("Invalid QPC frequency");
    return (b.QuadPart-a.QuadPart)*1000.0/double(freq.QuadPart);
}
struct One {
    double openMs=0,frameMs=0,firstMs=0;
    ULONG reads=0,extraSeeks=0;
    std::vector<BYTE> rgb;
};
inline One Trial(IWICImagingFactory* factory,bool legacy) {
    CComPtr<IWICStream> bytes;
    Need(factory->CreateStream(&bytes),"create synthetic file stream");
    Need(bytes->InitializeFromFilename(
         L"samples\\fixture-irot3-exif6-noaux.heic",GENERIC_READ),
         "open public artificial HEIC");
    CComPtr<IStream> wrapped;
    auto* counter=new Stream(bytes,legacy);
    wrapped.Attach(counter);
    const auto t0=Tick();
    CComPtr<IWICBitmapDecoder> decoder;
    Need(factory->CreateDecoderFromStream(wrapped,nullptr,
        WICDecodeMetadataCacheOnDemand,&decoder),"decode HEIF from wrapped stream");
    const auto t1=Tick();
    CComPtr<IWICBitmapFrameDecode> frame;
    Need(decoder->GetFrame(0,&frame),"get primary");
    UINT w=0,h=0;
    Need(frame->GetSize(&w,&h),"primary size");
    if(w!=1080||h!=1920)throw std::runtime_error("Orientation changed");
    UINT count=0;Need(decoder->GetFrameCount(&count),"frame count");
    if(count!=2)throw std::runtime_error("Auxiliary item lost");
    CComPtr<IWICBitmapSource> bgr;
    Need(WICConvertBitmapSource(GUID_WICPixelFormat24bppBGR,frame,&bgr),
         "make 24-bit visual source");
    const auto t2=Tick();
    // Only 64px tall: exercises first HEVC full decode and stream I/O,
    // while minimizing time spent copying out huge BGR buffers.
    WICRect rc{0,0,64,64};
    One o; o.rgb.resize(64u*64u*3u);
    Need(bgr->CopyPixels(&rc,64u*3u,UINT(o.rgb.size()),o.rgb.data()),
         "first CopyPixels ROI");
    const auto t3=Tick();
    o.openMs=Ms(t0,t1);
    o.frameMs=Ms(t1,t2);
    o.firstMs=Ms(t2,t3);
    o.reads=counter->reads;
    o.extraSeeks=counter->extraSeeks;
    if(legacy && o.extraSeeks==0)
        throw std::runtime_error("Legacy seek emulation did not run");
    if(!legacy && o.extraSeeks)
        throw std::runtime_error("Optimized seek emulation unexpectedly ran");
    return o;
}
inline double Median(std::vector<double> v) {
    std::sort(v.begin(),v.end());
    return v[v.size()/2];
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Need(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),
        "factory for synthetic A/B");
    std::vector<double> totals[2],openings[2],decodes[2];
    ULONG legacyReads=0,legacyExtra=0,optimizedReads=0;
    // Interleave BAAB to average warmed cache/order and host noise.
    for(int round=0;round<12;round++){
        const bool legacy=(round%4==0 || round%4==3);
        One o=Trial(factory,legacy);
        if(round==0){
            // Read a second time in the opposite mode, and assert the
            // displayed raster is identical, not merely the dimensions.
            One other=Trial(factory,false);
            if(o.rgb!=other.rgb)
                throw std::runtime_error("Legacy and optimized RGB differ");
        }
        const int i=legacy?1:0;
        totals[i].push_back(o.openMs+o.frameMs+o.firstMs);
        openings[i].push_back(o.openMs);
        decodes[i].push_back(o.firstMs);
        if(legacy){legacyReads+=o.reads;legacyExtra+=o.extraSeeks;}
        else optimizedReads+=o.reads;
    }
    if(totals[0].size()!=6 || totals[1].size()!=6 ||
       legacyExtra==0 || optimizedReads==0)
        throw std::runtime_error("A/B samples incomplete");
    printf("WIC_SAME_HOST_STREAM_AB samples_per_path=6 legacy_extra_seek=%lu "
           "legacy_reads=%lu optimized_reads=%lu "
           "legacy_open_median_ms=%.3f optimized_open_median_ms=%.3f "
           "legacy_first_median_ms=%.3f optimized_first_median_ms=%.3f "
           "legacy_total_median_ms=%.3f optimized_total_median_ms=%.3f "
           "fixture=artificial two_frames=2 same_rgb=1\n",
           static_cast<unsigned long>(legacyExtra),
           static_cast<unsigned long>(legacyReads),
           static_cast<unsigned long>(optimizedReads),
           Median(openings[1]),Median(openings[0]),
           Median(decodes[1]),Median(decodes[0]),
           Median(totals[1]),Median(totals[0]));
    printf("WIC_SAME_HOST_STREAM_AB_PASS comparable_image=1\n");
}
} // namespace WICStreamSameHostABProbe
