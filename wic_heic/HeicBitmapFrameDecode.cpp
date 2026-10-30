#include "stdafx.h"
#include "wic_heic.h"
#include "HeifStreamReader.h"
#include "HeicBitmapDecoder.h"
#include "HeicBitmapFrameDecode.h"
#include "HeicMetadataBlockReader.h"
#include "PhotosWicTrace.h"
#include "ExifOrientationFix.h"
#include "HeicLosslessNative.h"
#include "HeicThumbnailWorkerPolicy.h"
#include "PhotosPureRotationBridge.h"

CHeicBitmapFrameDecode::CHeicBitmapFrameDecode(CHeicBitmapDecoder* decoder, heif::ImageHandle handle,UINT index,UINT count)
	: m_Count(1)
	, m_pDecoder(decoder)
	, m_Handle(handle)
	, m_TopLevelIndex(index)
	, m_TopLevelCount(count)
	, m_Bpp(0)
	, m_PlaneInterleaved(nullptr)
	, m_Stride(0)
	, m_Decoded(false)
{
	DbgLog("%s", __FUNCTION__);

	if (m_pDecoder) {
		m_pDecoder->AddRef();
	}

	int bpc = handle.get_chroma_bits_per_pixel();
	if (bpc == -1) {
		bpc = handle.get_luma_bits_per_pixel();
	}

	m_chroma = heif_chroma_interleaved_RGB;
	m_PixelFormat = GUID_WICPixelFormat24bppRGB;
	m_Bpp = 24;

	if (bpc == 8 && !m_Handle.has_alpha_channel())
	{
		m_PixelFormat = GUID_WICPixelFormat24bppRGB;
		m_chroma = heif_chroma_interleaved_RGB;
		m_Bpp = 24;
	}
	else if (bpc == 8 && m_Handle.has_alpha_channel())
	{
		m_PixelFormat = GUID_WICPixelFormat32bppRGBA;
		m_chroma = heif_chroma_interleaved_RGBA;
		m_Bpp = 32;
	}
	else if (bpc == 10 && !m_Handle.has_alpha_channel())
	{
		m_PixelFormat = GUID_WICPixelFormat48bppRGB;
		m_chroma = heif_chroma_interleaved_RRGGBB_BE;
		m_Bpp = 32;
	}
	else if (bpc == 10 && m_Handle.has_alpha_channel())
	{
		m_PixelFormat = GUID_WICPixelFormat64bppRGBA;
		m_chroma = heif_chroma_interleaved_RRGGBBAA_BE;
		m_Bpp = 64;
	}
	else if (bpc == 12 && !m_Handle.has_alpha_channel())
	{
		m_PixelFormat = GUID_WICPixelFormat48bppRGB;
		m_chroma = heif_chroma_interleaved_RRGGBB_BE;
		m_Bpp = 48;
	}
	else if (bpc == 12 && m_Handle.has_alpha_channel())
	{
		m_PixelFormat = GUID_WICPixelFormat64bppRGBA;
		m_chroma = heif_chroma_interleaved_RRGGBBAA_BE;
		m_Bpp = 64;
	}

	// No HDR support for now
	m_chroma = heif_chroma_interleaved_RGB;
	m_PixelFormat = GUID_WICPixelFormat24bppRGB;
	m_Bpp = 24;

	DbgLog("CHeicBitmapFrameDecode: (chroma=%d, bpp=%d, bpc=%d, w=%d, h=%d)",
		m_chroma, m_Bpp, bpc, handle.get_width(), handle.get_height());
}


CHeicBitmapFrameDecode::~CHeicBitmapFrameDecode()
{
	DbgLog("%s", __FUNCTION__);

	if (m_pDecoder) {
		m_pDecoder->Release();
	}
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::QueryInterface(REFIID riid, void **ppvObject)
{
	DbgLog("%s", __FUNCTION__);

	HRESULT hr = S_OK;

	if (!ppvObject) {
		return E_POINTER;
	}

	*ppvObject = nullptr;
	if (IsEqualGUID(riid, IID_IUnknown)) {
		*ppvObject = static_cast<IUnknown*>(static_cast<IWICBitmapFrameDecode*>(this));
	} else if (IsEqualGUID(riid, IID_IHeicRawSource)) {
		*ppvObject = static_cast<IHeicRawSource*>(this);
	} else if (IsEqualGUID(riid, IID_IHeicAuxiliaryContainerSource) &&
	           !m_Handle.is_primary_image() && m_TopLevelIndex>0 &&
	           m_TopLevelIndex<m_TopLevelCount) {
		*ppvObject = static_cast<IHeicAuxiliaryContainerSource*>(this);
	} else if (IsEqualGUID(riid, IID_IWICBitmapSource)) {
		*ppvObject = static_cast<IWICBitmapSource*>(this);
	} else if (IsEqualGUID(riid, IID_IWICBitmapFrameDecode)) {
		*ppvObject = static_cast<IWICBitmapFrameDecode*>(this);
	} else {
		hr = E_NOINTERFACE;
	}

	if (SUCCEEDED(hr)) {
		AddRef();
	}

	return hr;
}

ULONG STDMETHODCALLTYPE CHeicBitmapFrameDecode::AddRef(void)
{
	DbgLog("%s", __FUNCTION__);

	return ++m_Count;
}

ULONG STDMETHODCALLTYPE CHeicBitmapFrameDecode::Release(void)
{
	DbgLog("%s", __FUNCTION__);

	uint32_t n = --m_Count;
	if (m_Count == 0) {
		delete this;
	}

	return n;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::GetSize(__RPC__out UINT *puiWidth, __RPC__out UINT *puiHeight)
{
	DbgLog("%s", __FUNCTION__);

	if (!puiWidth || !puiHeight) {
		return E_POINTER;
	}

	*puiWidth = static_cast<UINT>(m_Handle.get_width());
	*puiHeight = static_cast<UINT>(m_Handle.get_height());
	WicHeicDiagnostic::Emit("DecoderFrameGetSize",*puiWidth,*puiHeight,0,0,m_Handle.is_primary_image()?"primary":"auxiliary");

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::GetPixelFormat(__RPC__out WICPixelFormatGUID *pPixelFormat)
{
	DbgLog("%s", __FUNCTION__);

	if (!pPixelFormat) {
		return E_POINTER;
	}

	*pPixelFormat = m_PixelFormat;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::GetResolution(__RPC__out double *pDpiX, __RPC__out double *pDpiY)
{
	DbgLog("%s", __FUNCTION__);

	if (pDpiX == nullptr || pDpiY == nullptr) {
		return E_POINTER;
	}

	*pDpiX = 96;
	*pDpiY = 96;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::CopyPalette(__RPC__in_opt IWICPalette *pIPalette)
{
	DbgLog("%s", __FUNCTION__);

	return WINCODEC_ERR_PALETTEUNAVAILABLE;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::CopyPixels(
    __RPC__in_opt const WICRect *prc, UINT cbStride, UINT cbBufferSize,
    __RPC__out_ecount_full(cbBufferSize) BYTE *pbBuffer)
{
    // Validate all caller-controlled geometry BEFORE decoding and pointer
    // arithmetic. A malformed WICRect used to read before/beyond the plane.
    if (!pbBuffer) return E_POINTER;
    const int imageW=m_Handle.get_width(), imageH=m_Handle.get_height();
    if(imageW<=0 || imageH<=0 || imageW>20000 || imageH>20000)
        return WINCODEC_ERR_BADIMAGE;
    const WICRect full{0,0,imageW,imageH};
    const WICRect& rc=prc?*prc:full;
    if(rc.X<0 || rc.Y<0 || rc.Width<=0 || rc.Height<=0 ||
       static_cast<uint64_t>(rc.X)+static_cast<uint64_t>(rc.Width)>static_cast<uint64_t>(imageW) ||
       static_cast<uint64_t>(rc.Y)+static_cast<uint64_t>(rc.Height)>static_cast<uint64_t>(imageH)){
        WicHeicDiagnostic::Emit("DecoderCopyPixelsRejected",0,0,0,0,"rectangle",E_INVALIDARG);
        return E_INVALIDARG;
    }
    if(m_Bpp<=0 || (m_Bpp%8)!=0)return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    const uint64_t bpp=static_cast<uint64_t>(m_Bpp/8);
    const uint64_t rowBytes=static_cast<uint64_t>(rc.Width)*bpp;
    // The final row needs only rowBytes, rather than a full padding stride.
    const uint64_t needed=(static_cast<uint64_t>(rc.Height)-1)*cbStride+rowBytes;
    if(cbStride<rowBytes || needed>cbBufferSize){
        WicHeicDiagnostic::Emit("DecoderCopyPixelsRejected",
            static_cast<UINT>(rc.Width),static_cast<UINT>(rc.Height),0,0,
            "destination-stride",E_INVALIDARG);
        return E_INVALIDARG;
    }
    try {
        DecodeImage();
    } catch (...) {
        return WINCODEC_ERR_BADIMAGE;
    }
    // In HEIF, irot/grid transformations can change the visible dimensions.
    // The plane we read must match the WIC-visible rectangle, NOT the raw
    // HEVC tile dimensions. Never perform a speculative stride-based copy.
    const int decodedW=m_Image.get_width(heif_channel_interleaved);
    const int decodedH=m_Image.get_height(heif_channel_interleaved);
    if(!m_PlaneInterleaved || decodedW!=imageW || decodedH!=imageH ||
       m_Stride<static_cast<uint64_t>(imageW)*bpp){
        WicHeicDiagnostic::Emit("DecoderCopyPixelsRejected",
            static_cast<UINT>(imageW),static_cast<UINT>(imageH),0,0,
            "decoded-geometry",WINCODEC_ERR_BADIMAGE);
        return WINCODEC_ERR_BADIMAGE;
    }
    WicHeicDiagnostic::Emit("DecoderCopyPixels",
        static_cast<UINT>(rc.Width),static_cast<UINT>(rc.Height),0,0,
        prc?"region":"full");
    const uint8_t* plane=m_PlaneInterleaved+
        static_cast<size_t>(rc.Y)*m_Stride+
        static_cast<size_t>(rc.X)*static_cast<size_t>(bpp);
    // Direct full-width contiguous RGB planes can be copied in one bulk
    // memcpy rather than thousands of tiny row copies. Only use this route
    // when both source and destination have *exactly* the visible row stride
    // and the rectangle spans the full width, so no padding or neighboring
    // pixels can be copied accidentally. Crops/padded strides use the
    // existing well-tested row-by-row path.
    if(rc.X==0 && rc.Width==imageW &&
       static_cast<uint64_t>(m_Stride)==rowBytes &&
       static_cast<uint64_t>(cbStride)==rowBytes){
        memcpy(pbBuffer,plane,static_cast<size_t>(rowBytes)*
                               static_cast<size_t>(rc.Height));
        return S_OK;
    }
    BYTE* out=pbBuffer;
    for(int y=0;y<rc.Height;y++){
        memcpy(out,plane,static_cast<size_t>(rowBytes));
        out+=cbStride;
        plane+=m_Stride;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::GetMetadataQueryReader(__RPC__deref_out_opt IWICMetadataQueryReader **ppIMetadataQueryReader)
{
	WicHeicDiagnostic::Emit("DecoderMetadataQuery");
	DbgLog("%s", __FUNCTION__);

	if (!ppIMetadataQueryReader) {
		return E_POINTER;
	}

	// Before exposing a duplicate 180-degree Exif orientation, authenticate
    // the compressed PRIMARY HEIF item's actual irot=2 (half-turn). Checking
    // visible width/height alone is invalid: 180° swaps neither dimension.
    // In the common case without Exif Orientation=3, DO NOT reread the HEIC.
    bool primaryIrot180Verified=false;
    if(m_pDecoder && m_Handle.is_primary_image()){
        try {
            const auto ids=m_Handle.get_list_of_metadata_block_IDs();
            bool hasExif3=false;
            for(heif_item_id id:ids){
                if(m_Handle.get_metadata_type(id)!="Exif")continue;
                auto metadata=m_Handle.get_metadata(id);
                if(WicHeicOrientation::NormalizeExifForRotatedPixels(
                        metadata,false,true)){
                    hasExif3=true;
                    break;
                }
            }
            if(hasExif3){
                std::vector<uint8_t> raw;
                if(SUCCEEDED(m_pDecoder->CopyOriginalBytes(raw)) &&
                   raw.size()<=64u*1024u*1024u)
                    primaryIrot180Verified=HeicLosslessNative::PrimaryHasIrot180(raw);
            }
        } catch(...) {
            // WIC query continues unchanged on malformed HEIF/Exif. Never
            // pretend that 180-degree container rotation was authenticated.
            primaryIrot180Verified=false;
        }
    }
    CHeicMetadataBlockReader* pMetadataBlockReader =
        new(std::nothrow) CHeicMetadataBlockReader(
            this->m_Handle,primaryIrot180Verified);
	if (!pMetadataBlockReader) {
		return E_OUTOFMEMORY;
	}

	HRESULT hr = pMetadataBlockReader->CreateQueryReader(ppIMetadataQueryReader);
	// CreateQueryReaderFromBlockReader retains its own COM reference on
	// success. Release our initial reference on both success and failure;
	// otherwise every Photos metadata query leaks a block reader and handle.
	pMetadataBlockReader->Release();
	return hr;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::GetColorContexts(UINT cCount, __RPC__inout_ecount_full_opt(cCount) IWICColorContext **ppIColorContexts, __RPC__out UINT *pcActualCount)
{
	// Photos and other WIC clients query this before editing. The historical
	// decoder always returned UNSUPPORTEDOPERATION, even if HEIF contained a
	// genuine ICC profile. The encoder already preserves an explicitly supplied
	// WIC ICC, so make that source profile discoverable without changing decode
	// selection, pixel formats, image content or codec registration.
	if (!pcActualCount) return E_POINTER;
	*pcActualCount=0;
	if (cCount && !ppIColorContexts) return E_INVALIDARG;
	try {
		if (m_Handle.empty()) return WINCODEC_ERR_BADIMAGE;
		auto* handle=m_Handle.get_raw_image_handle();
		const size_t profileSize=heif_image_handle_get_raw_color_profile_size(handle);
		if (!profileSize) return S_OK;
		if (profileSize < 132 || profileSize > (4u << 20))
			return WINCODEC_ERR_BADIMAGE;
		*pcActualCount=1;
		// WIC permits a count-only query with no allocated color contexts.
		if (!cCount) return S_OK;
		if (!ppIColorContexts[0]) return E_INVALIDARG;
		std::vector<uint8_t> icc(profileSize);
		auto error=heif_image_handle_get_raw_color_profile(handle,icc.data());
		if (error.code!=heif_error_Ok || memcmp(icc.data()+36,"acsp",4)!=0)
			return WINCODEC_ERR_BADIMAGE;
		const HRESULT hr=ppIColorContexts[0]->InitializeFromMemory(
			icc.data(),static_cast<UINT>(icc.size()));
		if (SUCCEEDED(hr))
			WicHeicDiagnostic::Emit("DecoderICCProvided",0,0,0,0,"icc-for-wic");
		return hr;
	}
	catch(const std::bad_alloc&) { return E_OUTOFMEMORY; }
	catch(...) { return WINCODEC_ERR_BADIMAGE; }
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::GetThumbnail(__RPC__deref_out_opt IWICBitmapSource **ppIThumbnail)
{
	try
	{
		DbgLog("Frame::GetThumbnail@1");
		int index = 0;

		if (!ppIThumbnail) return E_POINTER;
		*ppIThumbnail = nullptr;

		std::vector<heif_item_id> ids = m_Handle.get_list_of_thumbnail_IDs();
		if (!ids.size()) {
			WicHeicDiagnostic::Emit("FrameThumbnailUnavailable",0,0,0,0,
				"no-embedded-thumbnail",WINCODEC_ERR_CODECNOTHUMBNAIL);
			return WINCODEC_ERR_CODECNOTHUMBNAIL;
		}

		heif::ImageHandle thumbHandle = m_Handle.get_thumbnail(ids[index]);
		if (thumbHandle.empty()) {
			return WINCODEC_ERR_CODECNOTHUMBNAIL;
		}

		WicHeicDiagnostic::Emit("FrameThumbnailEmbedded",
			static_cast<UINT>(thumbHandle.get_width()),
			static_cast<UINT>(thumbHandle.get_height()),0,0,"embedded");
		DbgLog("GetThumbnail@handle: hasAlpha=%d, chromaBpp=%d, lumaBpp=%d, width=%d, height=%d, primary=%d",
			thumbHandle.has_alpha_channel(),
			thumbHandle.get_chroma_bits_per_pixel(),
			thumbHandle.get_luma_bits_per_pixel(),
			thumbHandle.get_width(),
			thumbHandle.get_height(),
			thumbHandle.is_primary_image()
		);

		CHeicBitmapFrameDecode* decoder = new(std::nothrow) CHeicBitmapFrameDecode(m_pDecoder, thumbHandle,0,1);
		if (!decoder) {
			return E_OUTOFMEMORY;
		}

		*ppIThumbnail = decoder;
	}
	catch (const heif::Error& ex)
	{
		Log("GetThumbnail@Exception: %s", ex.get_message().c_str());
		return WINCODEC_ERR_CODECNOTHUMBNAIL;
	}
	catch (const std::exception& ex)
	{
		Log("GetThumbnail@Exception: %s", ex.what());
		return WINCODEC_ERR_CODECNOTHUMBNAIL;
	}
	catch (...)
	{
		Log("GetThumbnail@Exception: Unknown");
		return WINCODEC_ERR_CODECNOTHUMBNAIL;
	}

	return S_OK;
}

void CHeicBitmapFrameDecode::DecodeImage()
{
    // Warm path: published RGB plane is immutable and does not need a mutex.
    // This matters when FlipRotator asks for thousands of pixel rows.
    if (m_Decoded.load(std::memory_order_acquire)) return;
    // Cold path: only one worker runs libheif for this COM frame.
    std::lock_guard<std::mutex> guard(m_DecodeMutex);
    if (m_Decoded.load(std::memory_order_relaxed)) return;
	DbgLog("%s", __FUNCTION__);

	if (m_Handle.empty()) {
		throw std::exception("Bad image handle");
	}

    // One full HEVC/libheif decode for this frame. Do NOT count the
    // following warm CopyPixels row copies as fresh decompressions.
    // All measurements are opt-in: the production path never writes a log
    // unless the existing privacy-limited diagnostic was explicitly enabled.
    const ULONGLONG decodeStarted=GetTickCount64();
    // Lab-only opt-in; no effect in the production WIC 1.1.28 build.
    // "auto" applies a conservative eligibility/concurrency policy. The
    // previous 2/4 force modes remain as the A/B experimental controls.
#if defined(WIC_HEIC_THUMB_WORKERS_LAB) || defined(WIC_HEIC_THUMB_ADAPTIVE_PILOT)
    wchar_t labEnabled[12]{};
    wchar_t workerSetting[12]{};
    const DWORD enabledLength=GetEnvironmentVariableW(
        L"GITHUB_ACTIONS",labEnabled,12);
    const DWORD settingLength=GetEnvironmentVariableW(
        L"WIC_HEIC_THUMB_LAB_WORKERS",workerSetting,12);
    if(enabledLength==4 && wcscmp(labEnabled,L"true")==0 &&
       settingLength==1 &&
       (workerSetting[0]==L'0' || workerSetting[0]==L'1' ||
        workerSetting[0]==L'2' || workerSetting[0]==L'4' ||
        workerSetting[0]==L'a')) {
        static std::atomic<unsigned> activeDecodeCount{0};
        // Never leave the counter elevated after libheif exceptions.
        const unsigned otherDecodes=activeDecodeCount.fetch_add(
            1u,std::memory_order_acq_rel);
        struct LabDecodeGuard {
            std::atomic<unsigned>& count;
            ~LabDecodeGuard() { count.fetch_sub(1u,std::memory_order_acq_rel); }
        } activeGuard{activeDecodeCount};
        unsigned workers=0;
        if(workerSetting[0]==L'a') {
            SYSTEM_INFO sys{};
            GetSystemInfo(&sys);
            workers=HeicThumbnailWorkerPolicy::Select(
                static_cast<unsigned>(m_Handle.get_width()),
                static_cast<unsigned>(m_Handle.get_height()),
                m_Handle.get_luma_bits_per_pixel(),
                m_Handle.get_chroma_bits_per_pixel(),
                m_Handle.has_alpha_channel(),
                sys.dwNumberOfProcessors,otherDecodes);
        } else if(workerSetting[0]==L'1' || workerSetting[0]==L'2' ||
                  workerSetting[0]==L'4') {
            workers=workerSetting[0]-L'0';
        }
        printf("THUMB_WORKERS_POLICY_DECISION requested=%c selected=%u "
               "width=%d height=%d luma_bits=%d chroma_bits=%d alpha=%d "
               "concurrent_before=%u\n",
            static_cast<char>(workerSetting[0]),workers,
            m_Handle.get_width(),m_Handle.get_height(),
            m_Handle.get_luma_bits_per_pixel(),
            m_Handle.get_chroma_bits_per_pixel(),
            m_Handle.has_alpha_channel()?1:0,otherDecodes);
        if(workers!=0) {
            heif_decoding_options* opt=heif_decoding_options_alloc();
            if(!opt) throw std::bad_alloc();
            opt->num_codec_threads=static_cast<int>(workers);
            heif_image* native=nullptr;
            const heif_error error=heif_decode_image(
                m_Handle.get_raw_image_handle(),&native,
                heif_colorspace_RGB,m_chroma,opt);
            heif_decoding_options_free(opt);
            if(error.code!=heif_error_Ok) {
                if(native)heif_image_release(native);
                throw heif::Error(error);
            }
            if(!native)throw std::runtime_error("lab decode returned no image");
            m_Image=heif::Image(native);
        } else {
            m_Image=m_Handle.decode_image(heif_colorspace_RGB,m_chroma);
        }
    } else {
#if defined(WIC_HEIC_THUMB_ADAPTIVE_PILOT)
        // Production pilot: never depend on CI-only environment variables.
        // Use 4 codec workers on one eligible 8-bit opaque >=6MP image,
        // 2 when another eligible frame is decoding, otherwise default.
        static std::atomic<unsigned> activePilotDecodes{0};
        const unsigned already=activePilotDecodes.fetch_add(
            1u,std::memory_order_acq_rel);
        struct PilotDecodeGuard {
            std::atomic<unsigned>& count;
            ~PilotDecodeGuard(){ count.fetch_sub(1u,std::memory_order_acq_rel); }
        } guard{activePilotDecodes};
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        // A real Photos process uses a lower codec-thread budget than
        // Explorer; only the synthetic CI harness can simulate this host.
        wchar_t photoTest[4]{};
        const bool simulatedPhotos=(enabledLength==4 &&
            wcscmp(labEnabled,L"true")==0 &&
            GetEnvironmentVariableW(L"WIC_HEIC_THUMB_TEST_PHOTOS",
                                    photoTest,4)==1 && photoTest[0]==L'1');
        const bool photos=PhotosPureRotationBridge::IsWindowsPhotosHost() ||
                          simulatedPhotos;
        const unsigned workers=photos
          ? HeicThumbnailWorkerPolicy::SelectForPhotos(
              static_cast<unsigned>(m_Handle.get_width()),
              static_cast<unsigned>(m_Handle.get_height()),
              m_Handle.get_luma_bits_per_pixel(),
              m_Handle.get_chroma_bits_per_pixel(),
              m_Handle.has_alpha_channel(),info.dwNumberOfProcessors,already)
          : HeicThumbnailWorkerPolicy::Select(
              static_cast<unsigned>(m_Handle.get_width()),
              static_cast<unsigned>(m_Handle.get_height()),
              m_Handle.get_luma_bits_per_pixel(),
              m_Handle.get_chroma_bits_per_pixel(),
              m_Handle.has_alpha_channel(),info.dwNumberOfProcessors,already);
        // This test-only marker is visible on Actions. No stdout is needed
        // by Explorer/Photos on users' PCs.
        if(enabledLength==4 && wcscmp(labEnabled,L"true")==0) {
            printf("THUMB_PILOT_PRODUCTION_AUTO_SELECTED workers=%u "
                   "width=%d height=%d alpha=%d luma_bits=%d "
                   "concurrent_before=%u photos=%u\n",
                   workers,m_Handle.get_width(),m_Handle.get_height(),
                   m_Handle.has_alpha_channel()?1:0,
                   m_Handle.get_luma_bits_per_pixel(),already,photos?1u:0u);
        }
        if(workers==0) {
            m_Image=m_Handle.decode_image(heif_colorspace_RGB,m_chroma);
        } else {
            heif_decoding_options* opt=heif_decoding_options_alloc();
            if(!opt)throw std::bad_alloc();
            struct OptionsGuard {
                heif_decoding_options* value;
                ~OptionsGuard(){heif_decoding_options_free(value);}
            } options{opt};
            opt->num_codec_threads=static_cast<int>(workers);
            heif_image* native=nullptr;
            const heif_error error=heif_decode_image(
                m_Handle.get_raw_image_handle(),&native,
                heif_colorspace_RGB,m_chroma,opt);
            if(error.code!=heif_error_Ok) {
                if(native)heif_image_release(native);
                throw heif::Error(error);
            }
            if(!native)throw std::runtime_error("pilot decoded HEIC missing");
            m_Image=heif::Image(native);
        }
#else
        m_Image=m_Handle.decode_image(heif_colorspace_RGB,m_chroma);
#endif
    }
#else
    m_Image=m_Handle.decode_image(heif_colorspace_RGB,m_chroma);
#endif
	m_PlaneInterleaved = m_Image.get_plane2(heif_channel_interleaved, &m_Stride);

	DbgLog("Decode: (chroma=%d, bpp=%d, stride=%d)",
		m_chroma, m_Bpp, m_Stride);

	if (!m_PlaneInterleaved || !m_Stride) {
		throw std::exception("Unable to get plane");
	}

    // Timing is emitted only after a valid decoded RGB plane is published.
    // All fields are numeric or fixed labels: never paths, EXIF or pixels.
    const ULONGLONG decodeElapsed=GetTickCount64()-decodeStarted;
	m_Decoded.store(true,std::memory_order_release);
    WicHeicDiagnostic::Emit("DecoderFullDecode",
        static_cast<UINT>(m_Handle.get_width()),
        static_cast<UINT>(m_Handle.get_height()),0,0,
        "libheif-cold",S_OK,0,decodeElapsed);
}


HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::ReadFullContent(
    std::vector<uint8_t>* content)
{
	WicHeicDiagnostic::Emit("DecoderRawSourceRequested");
    if(!content)return E_POINTER;
    // Reading an auxiliary frame must never re-encode/rotate the primary item.
    // Only the actual primary image can expose the original full HEIF bytes.
    if(!m_pDecoder || m_Handle.empty())return WINCODEC_ERR_WRONGSTATE;
    if(!m_Handle.is_primary_image())return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    return m_pDecoder->CopyOriginalBytes(*content);
}


HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::ReadContainerAndIndex(
    std::vector<uint8_t>* content,UINT* index,UINT* count)
{
    WicHeicDiagnostic::Emit("DecoderAuxContainerRequested");
    if(!content || !index || !count)return E_POINTER;
    if(!m_pDecoder || m_Handle.empty() || m_Handle.is_primary_image() ||
       !m_TopLevelIndex || m_TopLevelIndex>=m_TopLevelCount ||
       m_TopLevelCount>8)return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    // Only return the original verified HEIF bytes, no decoded pixels or paths.
    const HRESULT hr=m_pDecoder->CopyOriginalBytes(*content);
    if(FAILED(hr))return hr;
    *index=m_TopLevelIndex;*count=m_TopLevelCount;
    return S_OK;
}
