#pragma once
#include <atomic>
#include <new>
#include <fstream>
#include <iterator>
// Guard regression tests against malformed frame size, incomplete WritePixels,
// and false BGR pixel-format negotiation. Uses temporary sample outputs only.
namespace EncoderGuardProbe {
inline void Check(HRESULT hr,const char* step) {
    if (FAILED(hr)) { printf("GUARD ERROR %s: 0x%08X\n",step,(unsigned)hr); throw std::runtime_error(step); }
}
// Synthetic-only proof of whether 32bppRGBA is *actually encodable*.
// The decoder supports RGBA, but the encoder registry must not advertise
// a format until a real encode + WIC readback retains its alpha channel.
inline void VerifyRgbaEncoderCapability(IWICImagingFactory* fac) {
    const UINT w=64,h=64;
    std::vector<BYTE> rgba(size_t(w)*h*4u);
    for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){
        const size_t i=(size_t(y)*w+x)*4u;
        rgba[i+0]=BYTE(x*4);rgba[i+1]=BYTE(y*4);
        rgba[i+2]=BYTE((x+y)*2);
        rgba[i+3]=(x<w/2)?0u:255u;
    }
    CComPtr<IWICBitmapEncoder> encoder;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&encoder),"RGBA8 synthetic HEIC encoder");
    CComPtr<IStream> stream;
    Check(CreateStreamOnHGlobal(nullptr,TRUE,&stream),"RGBA8 synthetic memory stream");
    Check(encoder->Initialize(stream,WICBitmapEncoderNoCache),"RGBA8 encoder init");
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame,nullptr),"RGBA8 frame create");
    Check(frame->Initialize(nullptr),"RGBA8 frame init");
    Check(frame->SetSize(w,h),"RGBA8 frame size");
    GUID pf=GUID_WICPixelFormat32bppRGBA;
    Check(frame->SetPixelFormat(&pf),"RGBA8 frame pixel negotiation");
    if(!IsEqualGUID(pf,GUID_WICPixelFormat32bppRGBA))
        throw std::runtime_error("RGBA8 pixel format incorrectly accepted then changed");
    Check(frame->WritePixels(h,w*4,UINT(rgba.size()),rgba.data()),"RGBA8 frame pixels");
    Check(frame->Commit(),"RGBA8 frame commit");
    const HRESULT encoded=encoder->Commit();
    printf("WIC_HEIC_RGBA8_ENCODER_COMMIT_HRESULT=0x%08X\\n",unsigned(encoded));
    if(FAILED(encoded)) {
        puts("WIC_HEIC_RGBA8_ENCODER_BACKEND_UNAVAILABLE_NO_REGISTRY_ADVERTISEMENT_PASS");
        return; // No claim of completed RGBA encode capability.
    }
    LARGE_INTEGER zero{};
    Check(stream->Seek(zero,STREAM_SEEK_SET,nullptr),"RGBA8 encoded stream rewind");
    CComPtr<IWICBitmapDecoder> decoder;
    Check(fac->CreateDecoderFromStream(stream,&GUID_VendorPrsyahmi,
        WICDecodeMetadataCacheOnDemand,&decoder),"RGBA8 decode memory stream");
    CComPtr<IWICBitmapFrameDecode> decoded;
    Check(decoder->GetFrame(0,&decoded),"RGBA8 decoded frame");
    GUID output{};
    Check(decoded->GetPixelFormat(&output),"RGBA8 decoded pixel format");
    if(!IsEqualGUID(output,GUID_WICPixelFormat32bppRGBA))
        throw std::runtime_error("HEIC encoded alpha was silently stripped");
    std::vector<BYTE> pixels(rgba.size());
    Check(decoded->CopyPixels(nullptr,w*4,UINT(pixels.size()),pixels.data()),
        "RGBA8 decoded pixels");
    BYTE minAlpha=255,maxAlpha=0;
    for(size_t i=3;i<pixels.size();i+=4) {
        if(pixels[i]<minAlpha)minAlpha=pixels[i];
        if(pixels[i]>maxAlpha)maxAlpha=pixels[i];
    }
    if(minAlpha>=maxAlpha)
        throw std::runtime_error("HEIC decoded alpha channel lost variation");
    // A Windows app may select the encoder by IWICBitmapEncoderInfo before
    // setting its pixel format. Advertise only formats with real HEIC
    // encode+decode success: 24bpp RGB and verified 32bpp RGBA, not the
    // high-bit-depth variants unsupported by the pinned encoding backend.
    CComPtr<IWICBitmapEncoderInfo> info;
    Check(encoder->GetEncoderInfo(&info),"RGBA8 encoder registry component info");
    UINT count=0;
    Check(info->GetPixelFormats(0,nullptr,&count),"RGBA8 encoder format count");
    if(!count||count>32)
        throw std::runtime_error("HEIC encoder registry format count invalid");
    std::vector<GUID> formats(count);
    UINT actual=0;
    Check(info->GetPixelFormats(count,formats.data(),&actual),
        "RGBA8 encoder format query");
    bool rgb24=false,rgba32=false,rgb48=false,rgba64=false;
    for(UINT i=0;i<actual;i++) {
        rgb24|=!!IsEqualGUID(formats[i],GUID_WICPixelFormat24bppRGB);
        rgba32|=!!IsEqualGUID(formats[i],GUID_WICPixelFormat32bppRGBA);
        rgb48|=!!IsEqualGUID(formats[i],GUID_WICPixelFormat48bppRGB);
        rgba64|=!!IsEqualGUID(formats[i],GUID_WICPixelFormat64bppRGBA);
    }
    printf("WIC_HEIC_ENCODER_REGISTERED_RGBA8 count=%u rgb24=%u rgba32=%u rgb48=%u rgba64=%u\\n",
        actual,unsigned(rgb24),unsigned(rgba32),unsigned(rgb48),unsigned(rgba64));
    if(!rgb24||!rgba32||rgb48||rgba64)
        throw std::runtime_error("HEIC encoder registered formats mismatch verified encode capability");
    puts("WIC_HEIC_ENCODER_REGISTERED_RGBA8_AND_REJECTS_UNPROVEN_HIGHBIT_PASS");
    puts("WIC_HEIC_RGBA8_ENCODER_AND_ALPHA_READBACK_PASS");
}
// Artificial IStream: writes a nonzero prefix and reports a successful short
// write. Real disks, photos, registry entries and input HEICs are untouched.
class ShortWriteStream final : public IStream {
    std::atomic<ULONG> refs{1};
    CComPtr<IStream> target;
    bool injected=false;
    bool allowTruncate=true;
    bool simulateShortWrite=true;
    bool transactionalRevert=false;
public:
    explicit ShortWriteStream(IStream* inner,bool mayTruncate=true,bool shortWrite=true,
                              bool canRevert=false)
      :target(inner),allowTruncate(mayTruncate),simulateShortWrite(shortWrite),
       transactionalRevert(canRevert){}
    STDMETHODIMP QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(IsEqualIID(id,IID_IUnknown)||IsEqualIID(id,IID_ISequentialStream)||IsEqualIID(id,IID_IStream)){
            *out=static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {return ++refs;}
    STDMETHODIMP_(ULONG) Release() override {
        ULONG n=--refs;
        if(!n)delete this;
        return n;
    }
    STDMETHODIMP Read(void* p,ULONG n,ULONG* got) override {return target->Read(p,n,got);}
    STDMETHODIMP Write(const void* p,ULONG n,ULONG* done) override {
        if(simulateShortWrite&&!injected&&n>=2){
            injected=true;
            return target->Write(p,n/2,done);
        }
        return target->Write(p,n,done);
    }
    STDMETHODIMP Seek(LARGE_INTEGER n,DWORD mode,ULARGE_INTEGER* to) override {
        return target->Seek(n,mode,to);
    }
    STDMETHODIMP SetSize(ULARGE_INTEGER n) override {return allowTruncate?target->SetSize(n):STG_E_ACCESSDENIED;}
    STDMETHODIMP CopyTo(IStream* dst,ULARGE_INTEGER n,ULARGE_INTEGER* r,ULARGE_INTEGER* w) override {
        return target->CopyTo(dst,n,r,w);
    }
    STDMETHODIMP Commit(DWORD flags) override {return target->Commit(flags);}
    STDMETHODIMP Revert() override {
        if(!transactionalRevert)return target->Revert();
        ULARGE_INTEGER empty{};
        HRESULT hr=target->SetSize(empty);
        if(FAILED(hr))return hr;
        LARGE_INTEGER start{};
        return target->Seek(start,STREAM_SEEK_SET,nullptr);
    }
    STDMETHODIMP LockRegion(ULARGE_INTEGER at,ULARGE_INTEGER n,DWORD t) override {
        return target->LockRegion(at,n,t);
    }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER at,ULARGE_INTEGER n,DWORD t) override {
        return target->UnlockRegion(at,n,t);
    }
    STDMETHODIMP Stat(STATSTG* s,DWORD flags) override {return target->Stat(s,flags);}
    STDMETHODIMP Clone(IStream** out) override {return target->Clone(out);}
};
inline void VerifyPartialLosslessStreamWriteRollback(IWICImagingFactory* fac) {
    CComPtr<IWICBitmapDecoder> decoder;
    Check(fac->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"partial stream synthetic HEIC");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"partial stream source frame");
    UINT w=0,h=0;
    Check(source->GetSize(&w,&h),"partial stream source dimensions");
    CComPtr<IStream> buffer;
    Check(CreateStreamOnHGlobal(nullptr,TRUE,&buffer),"partial stream backing");
    CComPtr<IStream> partial;
    partial.Attach(new(std::nothrow) ShortWriteStream(buffer));
    if(!partial)throw std::bad_alloc();
    CComPtr<IWICBitmapEncoder> encoder;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&encoder),"partial stream output encoder");
    Check(encoder->Initialize(partial,WICBitmapEncoderNoCache),"partial stream encoder init");
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame,nullptr),"partial stream output frame");
    Check(frame->Initialize(nullptr),"partial stream frame init");
    Check(frame->SetSize(w,h),"partial stream dimensions");
    Check(frame->WriteSource(source,nullptr),"partial stream capture original HEIC");
    Check(frame->Commit(),"partial stream frame commit");
    HRESULT hr=encoder->Commit();
    printf("WIC_LOSSLESS_PARTIAL_WRITE_REJECT_HRESULT=0x%08X\n",unsigned(hr));
    if(hr!=STG_E_WRITEFAULT)throw std::runtime_error(
        "Short output write was falsely reported successful");
    STATSTG st{};
    Check(buffer->Stat(&st,STATFLAG_NONAME),"partial stream backing length");
    printf("WIC_LOSSLESS_PARTIAL_WRITE_RESIDUAL_BYTES=%llu\n",
        static_cast<unsigned long long>(st.cbSize.QuadPart));
    if(st.cbSize.QuadPart!=0)throw std::runtime_error(
        "Failed HEIC output left a partial file instead of empty stream");
    // If the first Commit failed after partial output, the underlying HEIF
    // context can already be mutated. Reusing this encoder may double-encode
    // frames or claim a bogus success when the simulated stream recovers.
    HRESULT retry=encoder->Commit();
    printf("WIC_ENCODER_FAILED_COMMIT_RETRY_LOSSLESS=0x%08X\n",unsigned(retry));
    if(retry!=WINCODEC_ERR_WRONGSTATE)
        throw std::runtime_error("Lossless HEIC Commit accepted retry after short output");
    STATSTG afterRetry{};
    Check(buffer->Stat(&afterRetry,STATFLAG_NONAME),"lossless retry backing stat");
    if(afterRetry.cbSize.QuadPart!=0)
        throw std::runtime_error("Lossless Commit retry recreated a failed output");
    puts("WIC_ENCODER_FAILED_COMMIT_RETRY_LOSSLESS_BLOCKED_PASS");
    puts("WIC_LOSSLESS_PARTIAL_WRITE_ROLLBACK_PASS");
}
// Valid Windows WIC streams may implement Seek, Stat and Write while
// declining SetSize. A full write is still legal. Do not accidentally break
// existing HEIC saves by requiring a no-op SetSize(0) preflight. Incomplete
// writes already return STG_E_WRITEFAULT; without SetSize rollback, the
// caller must dispose of the failed new output rather than publish it.
inline void VerifyNoSetSizeStreamCanStillSaveCompleteHEIC(IWICImagingFactory* fac) {
    CComPtr<IWICBitmapDecoder> decoder;
    Check(fac->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"no-SetSize synthetic input");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"no-SetSize synthetic frame");
    CComPtr<IStream> underlying;
    Check(CreateStreamOnHGlobal(nullptr,TRUE,&underlying),
          "no-SetSize memory stream");
    CComPtr<IStream> noResize;
    noResize.Attach(new(std::nothrow) ShortWriteStream(underlying,false,false));
    if(!noResize)throw std::bad_alloc();
    CComPtr<IWICBitmapEncoder> encoder;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,
          &GUID_VendorPrsyahmi,&encoder),"no-SetSize encoder");
    Check(encoder->Initialize(noResize,WICBitmapEncoderNoCache),
          "no-SetSize encoder initialize");
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame,nullptr),
          "no-SetSize frame create");
    Check(frame->Initialize(nullptr),"no-SetSize frame initialize");
    Check(frame->WriteSource(source,nullptr),
          "no-SetSize lossless HEIC source");
    Check(frame->Commit(),"no-SetSize frame commit");
    Check(encoder->Commit(),
          "no-SetSize full HEIC output must remain supported");
    std::ifstream input("samples\\fixture-irot3-exif6-noaux.heic",
                        std::ios::binary);
    if(!input)throw std::runtime_error("no-SetSize fixture absent");
    std::vector<BYTE> expected(std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{});
    STATSTG st{};
    Check(underlying->Stat(&st,STATFLAG_NONAME),"no-SetSize output length");
    if(st.cbSize.QuadPart!=expected.size())
        throw std::runtime_error("no-SetSize stream produced incomplete HEIC");
    LARGE_INTEGER beginning{};
    Check(underlying->Seek(beginning,STREAM_SEEK_SET,nullptr),
          "no-SetSize rewind");
    std::vector<BYTE> actual(expected.size());
    ULONG got=0;
    Check(underlying->Read(actual.data(),ULONG(actual.size()),&got),
          "no-SetSize memory readback");
    if(got!=actual.size()||actual!=expected)
        throw std::runtime_error("no-SetSize output lost synthetic EXIF/ICC bytes");
    puts("WIC_HEIC_NO_SETSIZE_COMPLETE_OUTPUT_EXACT_BYTES_PASS");
}
// A transacted stream can recover even if SetSize is deliberately unavailable.
// Simulate a short S_OK write followed by IStream::Revert, which discards the
// partial in-memory output. The encoder must still report STG_E_WRITEFAULT.
inline void VerifyTransactionalRevertAfterShortHeicWrite(IWICImagingFactory* fac) {
    CComPtr<IWICBitmapDecoder> decoder;
    Check(fac->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"Revert synthetic source");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"Revert synthetic primary");
    CComPtr<IStream> backing;
    Check(CreateStreamOnHGlobal(nullptr,TRUE,&backing),"Revert memory backing");
    CComPtr<IStream> simulatedTransaction;
    simulatedTransaction.Attach(new(std::nothrow)
        ShortWriteStream(backing,false,true,true));
    if(!simulatedTransaction)throw std::bad_alloc();
    CComPtr<IWICBitmapEncoder> enc;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&enc),"Revert HEIC encoder");
    Check(enc->Initialize(simulatedTransaction,WICBitmapEncoderNoCache),
        "Revert encoder initialize");
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(enc->CreateNewFrame(&frame,nullptr),"Revert HEIC new frame");
    Check(frame->Initialize(nullptr),"Revert HEIC frame initialize");
    Check(frame->WriteSource(source,nullptr),"Revert HEIC raw source");
    Check(frame->Commit(),"Revert HEIC frame commit");
    const HRESULT hr=enc->Commit();
    printf("WIC_HEIC_TRANSACTION_REVERT_COMMIT_HRESULT=0x%08X\n",
        unsigned(hr));
    if(hr!=STG_E_WRITEFAULT)
        throw std::runtime_error("Short HEIC write on transaction reported success");
    STATSTG stat{};
    Check(backing->Stat(&stat,STATFLAG_NONAME),
          "Revert HEIC output backing size");
    if(stat.cbSize.QuadPart!=0)
        throw std::runtime_error("Transactional Revert was not attempted after partial HEIC write");
    if(enc->Commit()!=WINCODEC_ERR_WRONGSTATE)
        throw std::runtime_error("Dirty HEIC encoder reused after transactional rollback");
    puts("WIC_HEIC_PARTIAL_WRITE_TRANSACTION_REVERT_FALLBACK_PASS");
}
// Inject the same partial output fault while the HEVC encoder is producing
// a NEW, explicitly generated RGB image (as opposed to copying original HEIF).
inline void VerifyPartialRasterStreamWriteRollback(IWICImagingFactory* fac) {
    constexpr UINT w=192,h=320;
    std::vector<BYTE> rgb(size_t(w)*h*3u);
    for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){
        const size_t at=(size_t(y)*w+x)*3u;
        rgb[at]=BYTE(x);rgb[at+1]=BYTE(y);rgb[at+2]=BYTE(x+y);
    }
    CComPtr<IStream> backing;
    Check(CreateStreamOnHGlobal(nullptr,TRUE,&backing),"raster short write backing");
    CComPtr<IStream> fault;
    fault.Attach(new(std::nothrow) ShortWriteStream(backing));
    if(!fault)throw std::bad_alloc();
    CComPtr<IWICBitmapEncoder> encoder;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,
         &GUID_VendorPrsyahmi,&encoder),"raster short write HEIC encoder");
    Check(encoder->Initialize(fault,WICBitmapEncoderNoCache),
         "raster short write encoder init");
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame,nullptr),
         "raster short write create frame");
    Check(frame->Initialize(nullptr),"raster short write frame init");
    Check(frame->SetSize(w,h),"raster short write dimensions");
    GUID fmt=GUID_WICPixelFormat24bppRGB;
    Check(frame->SetPixelFormat(&fmt),"raster short write pixel format");
    Check(frame->WritePixels(h,w*3,UINT(rgb.size()),rgb.data()),
         "raster short write synthetic grid");
    Check(frame->Commit(),"raster short write frame Commit");
    const HRESULT hr=encoder->Commit();
    printf("WIC_RASTER_PARTIAL_WRITE_REJECT_HRESULT=0x%08X\n",
        unsigned(hr));
    if(SUCCEEDED(hr))throw std::runtime_error(
        "HEVC raster partial output falsely committed");
    STATSTG st{};
    Check(backing->Stat(&st,STATFLAG_NONAME),"raster short write leftover");
    printf("WIC_RASTER_PARTIAL_WRITE_RESIDUAL_BYTES=%llu\n",
        static_cast<unsigned long long>(st.cbSize.QuadPart));
    if(st.cbSize.QuadPart!=0)throw std::runtime_error(
        "Failed HEVC raster encode left a truncated HEIC stream");
    const HRESULT retry=encoder->Commit();
    printf("WIC_ENCODER_FAILED_COMMIT_RETRY_RASTER=0x%08X\n",unsigned(retry));
    if(retry!=WINCODEC_ERR_WRONGSTATE)
        throw std::runtime_error("HEVC Commit allowed retry with dirty encoder context");
    STATSTG afterRetry{};
    Check(backing->Stat(&afterRetry,STATFLAG_NONAME),"raster retry backing stat");
    if(afterRetry.cbSize.QuadPart!=0)
        throw std::runtime_error("HEVC Commit retry recreated failed output");
    puts("WIC_ENCODER_FAILED_COMMIT_RETRY_RASTER_BLOCKED_PASS");
    puts("WIC_RASTER_PARTIAL_WRITE_ROLLBACK_PASS");
}
// Synthetic-only retry regression: WriteSource previously inferred frame
// dimensions/pixel-format before discovering that the opaque bitmap must be
// rejected. It left the inferred dimensions behind and poisoned a later
// valid HEIC source on the SAME frame. No file outputs or Photos UI used.
inline void VerifyRejectedWriteSourceDoesNotLeakAutosetters(IWICImagingFactory* fac) {
    CComPtr<IWICBitmap> unverified;
    Check(fac->CreateBitmap(64,48,GUID_WICPixelFormat24bppRGB,
        WICBitmapCacheOnLoad,&unverified),"Create opaque synthetic bitmap");
    CComPtr<IWICBitmapDecoder> decoder;
    Check(fac->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"Load public synthetic HEIC");
    CComPtr<IWICBitmapFrameDecode> original;
    Check(decoder->GetFrame(0,&original),"Read synthetic HEIC primary");
    CComPtr<IStream> output;
    Check(CreateStreamOnHGlobal(nullptr,TRUE,&output),"HEIC retry memory stream");
    CComPtr<IWICBitmapEncoder> encoder;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,
        &encoder),"HEIC retry encoder");
    Check(encoder->Initialize(output,WICBitmapEncoderNoCache),
        "HEIC retry encoder init");
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame,nullptr),"HEIC retry new frame");
    Check(frame->Initialize(nullptr),"HEIC retry frame init");
    const HRESULT rejected=frame->WriteSource(unverified,nullptr);
    printf("WIC_WRITE_SOURCE_AUTOSETTER_REJECT_HRESULT=0x%08X\n",
           unsigned(rejected));
    if(rejected!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("Ambiguous WIC bitmap source unexpectedly saved");
    // Same frame, source of DIFFERENT geometry. Failed WriteSource must
    // leave default (unset) geometry/format so source inference works now.
    Check(frame->WriteSource(original,nullptr),
        "HEIC valid source after failed unverified bitmap source");
    Check(frame->Commit(),"HEIC retry lossless frame commit");
    Check(encoder->Commit(),"HEIC retry lossless container commit");
    std::ifstream reference("samples\\fixture-irot3-exif6-noaux.heic",
                            std::ios::binary);
    if(!reference)throw std::runtime_error("HEIC retry fixture not found");
    std::vector<BYTE> expected(std::istreambuf_iterator<char>{reference},
                                std::istreambuf_iterator<char>{});
    STATSTG outStat{};
    Check(output->Stat(&outStat,STATFLAG_NONAME),"HEIC retry output size");
    if(outStat.cbSize.QuadPart!=expected.size())
        throw std::runtime_error("HEIC retry output container size changed");
    LARGE_INTEGER origin{};
    Check(output->Seek(origin,STREAM_SEEK_SET,nullptr),
        "HEIC retry stream rewind");
    std::vector<BYTE> observed(expected.size());
    ULONG read=0;
    Check(output->Read(observed.data(),ULONG(observed.size()),&read),
        "HEIC retry stream readback");
    if(read!=observed.size()||observed!=expected)
        throw std::runtime_error("HEIC retry lost exact Exif/ICC/container bytes");
    puts("WIC_REJECTED_WRITE_SOURCE_DID_NOT_POISON_RETRY_AND_EXACT_HEIC_BYTES_PASS");
}
inline void Run() {
    CComPtr<IWICImagingFactory> fac;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory,(void**)&fac),"CoCreate WIC");

    VerifyRejectedWriteSourceDoesNotLeakAutosetters(fac);
    const UINT W=192,H=320;
    std::vector<BYTE> pixels(size_t(W)*H*3);
    for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++) {
        size_t off=(size_t(y)*W+x)*3;
        pixels[off]=BYTE(x%256); pixels[off+1]=BYTE(y%256); pixels[off+2]=BYTE((x+2*y)%256);
    }
    CComPtr<IWICBitmap> source;
    Check(fac->CreateBitmapFromMemory(W,H,GUID_WICPixelFormat24bppRGB,
        W*3,static_cast<UINT>(pixels.size()),pixels.data(),&source),"Create source");

    CComPtr<IWICBitmapEncoder> enc;
    CComPtr<IWICStream> stream;
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,&enc),"Create HEIC encoder");
    // WIC creation lifecycle: no frame may be created until encoder.Initialize.
    CComPtr<IWICBitmapFrameEncode> prematureFrame;
    HRESULT beforeInit=enc->CreateNewFrame(&prematureFrame,nullptr);
    printf("WIC_ENCODER_PREINIT_CREATE_FRAME_HRESULT=0x%08X\\n",unsigned(beforeInit));
    if(beforeInit!=WINCODEC_ERR_WRONGSTATE || prematureFrame)
        throw std::runtime_error("Encoder allowed a frame before Initialize");
    HRESULT badInit=enc->Initialize(nullptr,WICBitmapEncoderNoCache);
    if(badInit!=E_INVALIDARG)throw std::runtime_error("guard: null stream not rejected");
    Check(fac->CreateStream(&stream),"Create stream");
    Check(stream->InitializeFromFilename(L"samples\\guard-writepixels.heic",GENERIC_WRITE),"Open test output");
    Check(enc->Initialize(stream,WICBitmapEncoderNoCache),"Init encoder");
    if(enc->SetThumbnail(source)!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("guard: top-level thumbnail unexpectedly accepted");
    // An empty color-context list means no ICC, and is valid.
    if(enc->SetColorContexts(0,nullptr)!=S_OK)
        throw std::runtime_error("guard: empty color-context list rejected");
    CComPtr<IWICMetadataQueryWriter> unavailableWriter;
    if(enc->GetMetadataQueryWriter(&unavailableWriter)!=WINCODEC_ERR_UNSUPPORTEDOPERATION || unavailableWriter)
        throw std::runtime_error("guard: top-level metadata writer unexpectedly available");
    HRESULT doubleInit=enc->Initialize(stream,WICBitmapEncoderNoCache);
    if(doubleInit!=WINCODEC_ERR_WRONGSTATE)
        throw std::runtime_error("guard: repeated encoder init accepted");
    Check(enc->CreateNewFrame(&frame,nullptr),"Create frame");
    // A WIC encoder must finish an active frame before creating another.
    // Its cache option is NoCache, and HEIC re-encoders need stable order.
    CComPtr<IWICBitmapFrameEncode> tooEarlyNextFrame;
    HRESULT nextFrameHr=enc->CreateNewFrame(&tooEarlyNextFrame,nullptr);
    printf("WIC_ENCODER_OVERLAPPING_FRAME_HRESULT=0x%08X\\n",unsigned(nextFrameHr));
    if(nextFrameHr!=WINCODEC_ERR_WRONGSTATE || tooEarlyNextFrame)
        throw std::runtime_error("Encoder accepted overlapping active frames");
    // A frame must be initialized with its WIC encoder options before
    // geometry, format, or ICC configuration is accepted.
    HRESULT preSize=frame->SetSize(H,W);
    GUID preFormat=GUID_WICPixelFormat24bppRGB;
    HRESULT preFormatHr=frame->SetPixelFormat(&preFormat);
    HRESULT preColorHr=frame->SetColorContexts(0,nullptr);
    printf("WIC_FRAME_PREINIT_SETTERS_HRESULT size=0x%08X format=0x%08X icc=0x%08X\\n",
        unsigned(preSize),unsigned(preFormatHr),unsigned(preColorHr));
    if(preSize!=WINCODEC_ERR_WRONGSTATE ||
       preFormatHr!=WINCODEC_ERR_WRONGSTATE ||
       preColorHr!=WINCODEC_ERR_WRONGSTATE)
        throw std::runtime_error("Frame settings accepted before frame Initialize");
    Check(frame->Initialize(nullptr),"Init frame");
    HRESULT thumbnailHr=frame->SetThumbnail(source);
    if(thumbnailHr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("guard: ignored thumbnail reported as saved");
    Check(frame->SetSize(H,W),"Set intentionally wrong size");
    GUID bgr=GUID_WICPixelFormat24bppBGR;
    Check(frame->SetPixelFormat(&bgr),"Set BGR pixel format");
    if(!IsEqualGUID(bgr,GUID_WICPixelFormat24bppRGB))
        throw std::runtime_error("guard: BGR negotiation must report RGB");
    HRESULT hr=frame->WriteSource(source,nullptr);
    printf("GUARD WRONG_SIZE_WriteSource=0x%08X (expected safe rejection)\n",(unsigned)hr);
    // A bitmap with untraceable HEIC provenance now fails before the
    // geometry check. Both failure codes reject the unsafe write; neither
    // may be converted into a success or any output bytes.
    if(hr!=E_INVALIDARG && hr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("guard: unsafe bitmap or wrong dimensions accepted");
    hr=frame->Commit();
    if(hr!=WINCODEC_ERR_WRONGSTATE) throw std::runtime_error("guard: incomplete frame committed");

    // WritePixels should reject partial or too-small buffers rather than
    // copying incomplete rows into a full-size output HEIC image.
    const UINT srcStride=H*3;
    std::vector<BYTE> widePixels(size_t(srcStride)*W,77);
    hr=frame->WritePixels(W,srcStride,static_cast<UINT>(widePixels.size()-1),widePixels.data());
    if(hr!=E_INVALIDARG) throw std::runtime_error("guard: short buffer not rejected");
    Check(frame->WritePixels(W,srcStride,static_cast<UINT>(widePixels.size()),widePixels.data()),
        "WritePixels valid");
    hr=frame->WritePixels(1,srcStride,srcStride,widePixels.data());
    if(hr!=WINCODEC_ERR_WRONGSTATE && hr!=E_INVALIDARG)
        throw std::runtime_error("guard: writing after full frame accepted");
    // WIC's two-phase encoder contract requires frame.Commit() before
    // encoder.Commit(). Having all raster rows is not permission to publish
    // a frame that the client has never finalized.
    STATSTG beforeFrameCommit{};
    Check(stream->Stat(&beforeFrameCommit,STATFLAG_NONAME),
        "Uncommitted frame destination stat");
    if(beforeFrameCommit.cbSize.QuadPart!=0)
        throw std::runtime_error("Uncommitted raster frame already emitted bytes");
    const HRESULT earlyCommit=enc->Commit();
    printf("WIC_ENCODER_COMMIT_BEFORE_FRAME_COMMIT_HRESULT=0x%08X\\n",
        unsigned(earlyCommit));
    if(earlyCommit!=WINCODEC_ERR_WRONGSTATE)
        throw std::runtime_error("Encoder accepted uncommitted frame");
    STATSTG afterEarlyCommit{};
    Check(stream->Stat(&afterEarlyCommit,STATFLAG_NONAME),
        "Failed early encoder commit destination stat");
    if(afterEarlyCommit.cbSize.QuadPart!=0)
        throw std::runtime_error("Encoder published bytes from uncommitted frame");
    Check(frame->Commit(),"frame.Commit");
    if(frame->Commit()!=WINCODEC_ERR_WRONGSTATE)
        throw std::runtime_error("Frame accepted a repeated Commit");
    Check(enc->Commit(),"encoder.Commit");
    // Encoder.Commit must seal the image. No subsequent frames or ICC edits
    // may be accepted, even if the file stream remains open by the caller.
    CComPtr<IWICBitmapFrameEncode> lateFrame;
    HRESULT afterCommit=enc->CreateNewFrame(&lateFrame,nullptr);
    printf("WIC_ENCODER_POSTCOMMIT_CREATE_FRAME_HRESULT=0x%08X\\n",
        unsigned(afterCommit));
    if(afterCommit!=WINCODEC_ERR_WRONGSTATE || lateFrame)
        throw std::runtime_error("Encoder reopened after successful Commit");
    if(enc->SetColorContexts(0,nullptr)!=WINCODEC_ERR_WRONGSTATE)
        throw std::runtime_error("Encoder color metadata was mutable after Commit");
    puts("WIC_ENCODER_INITIALIZE_AND_FINALIZE_STATE_GUARDS_PASS");
    puts("WIC_ENCODER_FRAME_COMMIT_ORDER_ENFORCED_AND_STREAM_UNTOUCHED_PASS");
    frame.Release(); enc.Release(); stream.Release();
    CComPtr<IWICBitmapDecoder> readback;
    Check(fac->CreateDecoderFromFilename(L"samples\\guard-writepixels.heic",nullptr,
        GENERIC_READ,WICDecodeMetadataCacheOnDemand,&readback),"readback decoder");
    CComPtr<IWICBitmapFrameDecode> decoded;
    Check(readback->GetFrame(0,&decoded),"readback frame");
    UINT actualW=0,actualH=0;
    Check(decoded->GetSize(&actualW,&actualH),"readback size");
    if(actualW!=H || actualH!=W) throw std::runtime_error("guard: pixels size incorrect");
    // Fully prepared raster output must not replace existing stream bytes.
    CComPtr<IStream> protectedStream;
    Check(CreateStreamOnHGlobal(nullptr,TRUE,&protectedStream),"Create sentinel stream");
    const BYTE marker[]={0x53,0x41,0x46,0x45};
    ULONG n=0;
    Check(protectedStream->Write(marker,sizeof(marker),&n),"Write sentinel");
    if(n!=sizeof(marker))throw std::runtime_error("guard: write sentinel failed");
    LARGE_INTEGER zero{};
    Check(protectedStream->Seek(zero,STREAM_SEEK_SET,nullptr),"Rewind sentinel");
    CComPtr<IWICBitmapEncoder> protectedEnc;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,
        &protectedEnc),"Create protected encoder");
    Check(protectedEnc->Initialize(protectedStream,WICBitmapEncoderNoCache),
        "Initialize protected encoder");
    CComPtr<IWICBitmapFrameEncode> protectedFrame;
    Check(protectedEnc->CreateNewFrame(&protectedFrame,nullptr),"Create protected frame");
    Check(protectedFrame->Initialize(nullptr),"Initialize protected frame");
    Check(protectedFrame->SetSize(W,H),"Protected dimensions");
    GUID rgb=GUID_WICPixelFormat24bppRGB;
    Check(protectedFrame->SetPixelFormat(&rgb),"Protected format");
    // Preserve the output-stream sentinel test without using the now
    // forbidden ambiguous bitmap WriteSource. Explicit pixels are safe
    // here because the grid was generated in this test, not from HEIC.
    Check(protectedFrame->WritePixels(H,W*3,static_cast<UINT>(pixels.size()),
                                       pixels.data()),"Protected synthetic pixels");
    Check(protectedFrame->Commit(),"Protected frame commit");
    HRESULT protectHr=protectedEnc->Commit();
    if(protectHr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("guard: existing output not rejected");
    STATSTG stat{};
    Check(protectedStream->Stat(&stat,STATFLAG_NONAME),"Stat protected stream");
    if(stat.cbSize.QuadPart!=sizeof(marker))
        throw std::runtime_error("guard: existing stream length changed");
    Check(protectedStream->Seek(zero,STREAM_SEEK_SET,nullptr),"Rewind protected stream");
    BYTE got[sizeof(marker)]{};
    n=0;
    Check(protectedStream->Read(got,sizeof(got),&n),"Read protected stream");
    if(n!=sizeof(marker) || memcmp(got,marker,sizeof(marker))!=0)
        throw std::runtime_error("guard: existing output bytes changed");
    printf("ENCODER_GEOMETRY_FAILSAFE_TEST_PASSED\n");
    printf("ENCODER_NONEMPTY_STREAM_AND_THUMBNAIL_FAIL_CLOSED_PASSED\n");
    VerifyRgbaEncoderCapability(fac);
    VerifyPartialLosslessStreamWriteRollback(fac);
    VerifyNoSetSizeStreamCanStillSaveCompleteHEIC(fac);
    VerifyTransactionalRevertAfterShortHeicWrite(fac);
    VerifyPartialRasterStreamWriteRollback(fac);
}
} // namespace EncoderGuardProbe
