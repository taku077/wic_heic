#include "stdafx.h"
#include "wic_heic.h"
#include "HeifStreamReader.h"
#include "HeicBitmapDecoder.h"
#include "HeicBitmapFrameDecode.h"
#include "HeicMetadataBlockReader.h"
#include "PhotosWicTrace.h"
#include "ExifOrientationFix.h"
#include "HeicLosslessNative.h"

CHeicBitmapFrameDecode::CHeicBitmapFrameDecode(CHeicBitmapDecoder* decoder, heif::ImageHandle handle)
	: m_Count(1)
	, m_pDecoder(decoder)
	, m_Handle(handle)
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

		if (!ppIThumbnail) {
			return E_POINTER;
		}

		std::vector<heif_item_id> ids = m_Handle.get_list_of_thumbnail_IDs();
		if (!ids.size()) {
			return WINCODEC_ERR_CODECNOTHUMBNAIL;
		}

		heif::ImageHandle thumbHandle = m_Handle.get_thumbnail(ids[index]);
		if (thumbHandle.empty()) {
			return WINCODEC_ERR_CODECNOTHUMBNAIL;
		}

		DbgLog("GetThumbnail@handle: hasAlpha=%d, chromaBpp=%d, lumaBpp=%d, width=%d, height=%d, primary=%d",
			thumbHandle.has_alpha_channel(),
			thumbHandle.get_chroma_bits_per_pixel(),
			thumbHandle.get_luma_bits_per_pixel(),
			thumbHandle.get_width(),
			thumbHandle.get_height(),
			thumbHandle.is_primary_image()
		);

		CHeicBitmapFrameDecode* decoder = new(std::nothrow) CHeicBitmapFrameDecode(m_pDecoder, thumbHandle);
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
	DbgLog("%s", __FUNCTION__);

	if (m_Decoded) return;

	if (m_Handle.empty()) {
		throw std::exception("Bad image handle");
	}

	// Set both to undefined to use original one
	m_Image = m_Handle.decode_image(heif_colorspace_RGB, m_chroma);
	m_PlaneInterleaved = m_Image.get_plane2(heif_channel_interleaved, &m_Stride);

	DbgLog("Decode: (chroma=%d, bpp=%d, stride=%d)",
		m_chroma, m_Bpp, m_Stride);

	if (!m_PlaneInterleaved || !m_Stride) {
		throw std::exception("Unable to get plane");
	}

	m_Decoded = true;
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
