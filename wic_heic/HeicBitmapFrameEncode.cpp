#include "stdafx.h"
#include "wic_heic.h"
#include "HeicBitmapFrameEncode.h"
#include "HeicBitmapEncoder.h"
#include "HeicSourceAccess.h"
#include "HeicLosslessNative.h"
#include "PhotosWicTrace.h"
#include "HeicColorProfileWic.h"
#include <stdexcept>

// Convert a row of 16-bit WIC channels (0..65535) to HEIF 10-bit sample
// values (0..1023). Round-to-nearest minimizes quantization error.
static void Wic16ToHeic10(uint8_t* row, size_t rowBytes)
{
	for (size_t i = 0; i < rowBytes; i += 2) {
		const uint32_t sample = uint32_t(row[i]) | (uint32_t(row[i+1]) << 8);
		const uint32_t encoded = (sample * 1023u + 32767u) / 65535u;
		row[i] = BYTE(encoded & 255);
		row[i+1] = BYTE(encoded >> 8);
	}
}



CHeicBitmapFrameEncode::CHeicBitmapFrameEncode()
	: m_Count(1)
	, m_Width(0)
	, m_Height(0)
	, m_Chroma(heif_chroma_undefined)
	, m_Colorspace(heif_colorspace_RGB)
	, m_Created(false)
	, m_HasPixels(false)
	, m_WrittenLines(0)
	, m_BitmapTransform(0)
	, m_HeifCompressionMethod(0)
	, m_OptionsInitialized(false)
	, m_LosslessReady(false)
	, m_IccExplicitlySet(false)
	, m_PixelFormat(GUID_WICPixelFormat24bppRGB)
	, m_PixelFormatExplicitlySet(false)
	, m_BitDepthPerChannel(8)
{
	// The encoder owns each frame; never AddRef the parent here.
}


CHeicBitmapFrameEncode::~CHeicBitmapFrameEncode()
{
	// No parent reference to release (prevents a COM reference cycle).
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::QueryInterface(REFIID riid, void **ppvObject)
{
	HRESULT hr = S_OK;

	if (!ppvObject) {
		return E_POINTER;
	}

	*ppvObject = nullptr;
	if (IsEqualGUID(riid, IID_IUnknown)) {
		*ppvObject = static_cast<IUnknown*>(this);
	} else if (IsEqualGUID(riid, IID_IWICBitmapFrameEncode)) {
		*ppvObject = static_cast<IWICBitmapFrameEncode*>(this);
	} else {
		hr = E_NOINTERFACE;
	}

	if (SUCCEEDED(hr)) {
		AddRef();
	}

	return hr;
}

ULONG STDMETHODCALLTYPE CHeicBitmapFrameEncode::AddRef(void)
{
	return ++m_Count;
}

ULONG STDMETHODCALLTYPE CHeicBitmapFrameEncode::Release(void)
{
	uint32_t n = --m_Count;
	if (m_Count == 0) {
		delete this;
	}

	return n;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::Initialize(__RPC__in_opt IPropertyBag2 *pIEncoderOptions)
{
	WicHeicDiagnostic::Emit("FrameInitializeEnter",0,0,0,0,pIEncoderOptions?"options":"no-options");
	if (m_OptionsInitialized) return WINCODEC_ERR_WRONGSTATE;
	BYTE transform=0, compression=0;
	if (pIEncoderOptions) {
		PROPBAG2 props[2]{};
		props[0].pstrName=const_cast<LPOLESTR>(L"BitmapTransform");
		props[1].pstrName=const_cast<LPOLESTR>(L"HeifCompressionMethod");
		VARIANT values[2]{};
		HRESULT errors[2]{};
		HRESULT hr=pIEncoderOptions->Read(2,props,nullptr,values,errors);
		if (FAILED(hr)) return hr;
		for (int i=0;i<2;i++) {
			if (SUCCEEDED(errors[i]) && values[i].vt==VT_UI1) {
				if (i==0) transform=values[i].bVal;
				else compression=values[i].bVal;
			} else if (SUCCEEDED(errors[i]) && values[i].vt!=VT_EMPTY) {
				for(auto& v:values) VariantClear(&v);
				return DISP_E_TYPEMISMATCH;
			}
		}
		for(auto& v:values) VariantClear(&v);
	}
	if (transform>3 || compression>2)
		return WINCODEC_ERR_UNSUPPORTEDOPERATION;
	m_BitmapTransform=transform;
	m_HeifCompressionMethod=compression;
	m_OptionsInitialized=true;
	WicHeicDiagnostic::Emit("FrameInitializeOptions",0,0,transform,compression,pIEncoderOptions?"options":"default");
	DbgLog("HEIC_WIC_OPTIONS transform=%u compression=%u", unsigned(transform),unsigned(compression));
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::SetSize(UINT uiWidth, UINT uiHeight)
{
	if (m_Created || m_HasPixels || m_LosslessReady) return WINCODEC_ERR_WRONGSTATE;
	if (!uiWidth || !uiHeight || uiWidth > 20000 || uiHeight > 20000)
		return E_INVALIDARG;
	m_Width = uiWidth;
	m_Height = uiHeight;
	WicHeicDiagnostic::Emit("SetSize",uiWidth,uiHeight,m_BitmapTransform,m_HeifCompressionMethod);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::SetResolution(double dpiX, double dpiY)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::SetPixelFormat(__RPC__inout WICPixelFormatGUID *pPixelFormat)
{
	if (!pPixelFormat) {
		return E_INVALIDARG;
	}
	if (m_Created || m_HasPixels || m_LosslessReady) return WINCODEC_ERR_WRONGSTATE;

	if (IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat24bppRGB)) {
		m_PixelFormat = *pPixelFormat;
		m_Chroma = heif_chroma_interleaved_RGB;
		m_BitDepthPerChannel = 8;
	}
	else if (IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat24bppBGR)) {
		m_PixelFormat = GUID_WICPixelFormat24bppRGB;
		m_Chroma = heif_chroma_interleaved_RGB;
		m_BitDepthPerChannel = 8;
	}
	else if (IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat32bppRGBA)) {
		m_PixelFormat = *pPixelFormat;
		m_Chroma = heif_chroma_interleaved_RGBA;
		m_BitDepthPerChannel = 8;
	}
	else if (IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat32bppBGRA)) {
		m_PixelFormat = GUID_WICPixelFormat32bppRGBA;
		m_Chroma = heif_chroma_interleaved_RGBA;
		m_BitDepthPerChannel = 8;
	}
	// WIC 48/64-bit channels use 16-bit integers; the HEIF encoder
	// preserves 10-bit output precision by scaling at the pixel boundary.
	else if (IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat48bppRGB) ||
	         IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat48bppBGR)) {
		m_PixelFormat = GUID_WICPixelFormat48bppRGB;
		m_Chroma = heif_chroma_interleaved_RRGGBB_LE;
		m_BitDepthPerChannel = 10;
	}
	else if (IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat64bppRGBA) ||
	         IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat64bppBGRA)) {
		m_PixelFormat = GUID_WICPixelFormat64bppRGBA;
		m_Chroma = heif_chroma_interleaved_RRGGBBAA_LE;
		m_BitDepthPerChannel = 10;
	}
	else {
		*pPixelFormat = GUID_WICPixelFormat24bppRGB;
		m_PixelFormat = *pPixelFormat;
		m_Chroma = heif_chroma_interleaved_RGB;
		m_BitDepthPerChannel = 8;
	}

	// Report the actual accepted pixel format back to the caller.
	// Otherwise Photos may write BGR bytes into an RGB frame or vice versa.
	*pPixelFormat = m_PixelFormat;
	m_PixelFormatExplicitlySet=true;
	WicHeicDiagnostic::Emit("SetPixelFormat",m_Width,m_Height,m_BitmapTransform,m_HeifCompressionMethod);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::SetColorContexts(UINT cCount, __RPC__in_ecount_full(cCount) IWICColorContext **ppIColorContext)
{
	if (m_Created || m_HasPixels || m_LosslessReady) return WINCODEC_ERR_WRONGSTATE;
	std::vector<uint8_t> profile;
	HRESULT hr = HeicColorProfileWic::ReadArray(cCount, ppIColorContext, &profile);
	if (FAILED(hr)) {
		WicHeicDiagnostic::Emit("SetColorContexts",cCount,0,m_BitmapTransform,m_HeifCompressionMethod,"icc-rejected",hr);
		return hr;
	}
	m_IccProfile = std::move(profile);
	m_IccExplicitlySet = true;
	WicHeicDiagnostic::Emit("SetColorContexts",cCount,0,m_BitmapTransform,m_HeifCompressionMethod,"icc-accepted");
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::SetPalette(__RPC__in_opt IWICPalette *pIPalette)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::SetThumbnail(__RPC__in_opt IWICBitmapSource *pIThumbnail)
{
	// Never silently accept a supplied embedded thumbnail without storing it.
	WicHeicDiagnostic::Emit("SetThumbnail",0,0,m_BitmapTransform,m_HeifCompressionMethod,
		pIThumbnail?"thumbnail-rejected":"none",WINCODEC_ERR_UNSUPPORTEDOPERATION);
	return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::WritePixels(UINT lineCount, UINT cbStride, UINT cbBufferSize, __RPC__in_ecount_full(cbBufferSize) BYTE *pbPixels)
{
	WicHeicDiagnostic::Emit("WritePixelsEnter",m_Width,lineCount,m_BitmapTransform,m_HeifCompressionMethod,"raster");
	if (m_HeifCompressionMethod==1 || m_BitmapTransform!=0) {
		DbgLog("HEIC_WIC_LOSSLESS_UNAVAILABLE WritePixels transform=%u compression=%u",unsigned(m_BitmapTransform),unsigned(m_HeifCompressionMethod));
		return WINCODEC_ERR_UNSUPPORTEDOPERATION;
	}
	if (!pbPixels) return E_POINTER;
	if (!m_Width || !m_Height || !lineCount || m_HasPixels || m_LosslessReady) return WINCODEC_ERR_WRONGSTATE;
	if (lineCount > m_Height - m_WrittenLines) return E_INVALIDARG;

	// Only the explicitly supported 8-bit interleaved formats may be written
	// through this path; never silently truncate 10/12/16-bit samples.
	UINT bytesPerPixel = 0;
	if (IsEqualGUID(m_PixelFormat, GUID_WICPixelFormat24bppRGB)) bytesPerPixel = 3;
	else if (IsEqualGUID(m_PixelFormat, GUID_WICPixelFormat32bppRGBA)) bytesPerPixel = 4;
	else if (IsEqualGUID(m_PixelFormat, GUID_WICPixelFormat48bppRGB)) bytesPerPixel = 6;
	else if (IsEqualGUID(m_PixelFormat, GUID_WICPixelFormat64bppRGBA)) bytesPerPixel = 8;
	else return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;

	const uint64_t rowBytes = uint64_t(m_Width) * bytesPerPixel;
	const uint64_t need = (uint64_t(lineCount) - 1) * cbStride + rowBytes;
	if (rowBytes > cbStride || need > cbBufferSize) return E_INVALIDARG;

	try {
		CreateFrame();
		int planeStride = 0;
		uint8_t* plane = m_Frame.get_plane(heif_channel_interleaved, &planeStride);
		if (!plane || planeStride <= 0 || uint64_t(planeStride) < rowBytes)
			return WINCODEC_ERR_BADIMAGE;
		plane += size_t(planeStride) * m_WrittenLines;
		for (UINT y = 0; y < lineCount; ++y) {
			memcpy_s(plane, size_t(planeStride), pbPixels, size_t(rowBytes));
			if (m_BitDepthPerChannel == 10) Wic16ToHeic10(plane, size_t(rowBytes));
			plane += planeStride;
			pbPixels += cbStride;
		}
		m_WrittenLines += lineCount;
		if (m_WrittenLines == m_Height) m_HasPixels = true;
		return S_OK;
	}
	catch (const std::exception& ex) {
		Log("WritePixels failed: %s", ex.what());
		return WINCODEC_ERR_BADIMAGE;
	}
	catch (...) { return WINCODEC_ERR_BADIMAGE; }
}

// A lossless HEIC transfer is only valid when it honors an explicit color
// context request. SetColorContexts(0) is NOT the same as no request.
// Query only the exact COM source frame passed to WriteSource, never a
// previously viewed image or process-global state.
// Never report a bit-depth/channel-format conversion as completed merely
// because the HEVC source was copied without decoding. An omitted caller
// SetPixelFormat is different: the lossless original may be retained.
static bool RequestedPixelFormatMatchesSource(IWICBitmapSource* source,
    bool explicitlySet, const WICPixelFormatGUID& requested)
{
    if(!explicitlySet) return true;
    if(!source) return false;
    WICPixelFormatGUID actual{};
    return SUCCEEDED(source->GetPixelFormat(&actual)) &&
           IsEqualGUID(actual,requested);
}

static bool RequestedICCMatchesSource(IWICBitmapSource* bitmap,
    bool explicitlySet, const std::vector<uint8_t>& requested)
{
    if(!explicitlySet) return true;
    if(!bitmap) return false;
    IWICBitmapFrameDecode* frame=nullptr;
    HRESULT hr=bitmap->QueryInterface(IID_IWICBitmapFrameDecode,
        reinterpret_cast<void**>(&frame));
    if(FAILED(hr) || !frame) return false;
    UINT count=0;
    hr=frame->GetColorContexts(0,nullptr,&count);
    if(FAILED(hr)) {frame->Release();return false;}
    if(count==0) {frame->Release();return requested.empty();}
    if(count!=1 || requested.empty()) {frame->Release();return false;}
    IWICColorContext* context=nullptr;
    UINT actual=0;
    hr=frame->GetColorContexts(1,&context,&actual);
    frame->Release();
    if(FAILED(hr) || !context || actual!=1) {
        if(context) context->Release();
        return false;
    }
    std::vector<uint8_t> raw;
    hr=HeicColorProfileWic::Read(context,&raw);
    context->Release();
    return SUCCEEDED(hr) && raw==requested;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::WriteSource(__RPC__in_opt IWICBitmapSource *pIBitmapSource, __RPC__in_opt WICRect *prc)
{
	WicHeicDiagnostic::Emit("WriteSourceEnter",m_Width,m_Height,m_BitmapTransform,m_HeifCompressionMethod,prc?"cropped":"full");
	// A captured lossless container is immutable. A second source write must
	// never be accepted and then silently ignored by EncoderCommit.
	if (m_LosslessReady) return WINCODEC_ERR_WRONGSTATE;
	if (!pIBitmapSource) return E_POINTER;
	// IWICBitmapFrameEncode::WriteSource explicitly allows callers to omit
	// SetSize and SetPixelFormat. Windows' canonical HEIF no-recompression
	// example does exactly that. Infer absent settings only from this source,
	// never from a filename, previously viewed photo, global state or cache.
	// Source rotation is reflected in the *output* dimensions.
	if ((!m_Width) != (!m_Height)) return WINCODEC_ERR_WRONGSTATE;
	if (!m_Width) {
		UINT sourceW=0,sourceH=0;
		HRESULT hr=pIBitmapSource->GetSize(&sourceW,&sourceH);
		if (FAILED(hr)) return hr;
		if (!sourceW || !sourceH) return E_INVALIDARG;
		if (prc) {
			if (prc->X<0 || prc->Y<0 || prc->Width<=0 || prc->Height<=0 ||
			    uint64_t(prc->X)+UINT(prc->Width)>sourceW ||
			    uint64_t(prc->Y)+UINT(prc->Height)>sourceH)
				return E_INVALIDARG;
			sourceW=UINT(prc->Width);
			sourceH=UINT(prc->Height);
		}
		HRESULT set=SetSize((m_BitmapTransform&1)?sourceH:sourceW,
		                    (m_BitmapTransform&1)?sourceW:sourceH);
		if (FAILED(set)) return set;
		WicHeicDiagnostic::Emit("WriteSourceAutoSize",m_Width,m_Height,
			m_BitmapTransform,m_HeifCompressionMethod,"source-geometry");
	}
	if (m_Chroma==heif_chroma_undefined) {
		WICPixelFormatGUID sourceFormat{};
		HRESULT hr=pIBitmapSource->GetPixelFormat(&sourceFormat);
		if (FAILED(hr)) return hr;
		hr=SetPixelFormat(&sourceFormat);
		if (FAILED(hr)) return hr;
		// The automatic negotiation is NOT an explicit caller request.
		m_PixelFormatExplicitlySet=false;
		WicHeicDiagnostic::Emit("WriteSourceAutoPixelFormat",m_Width,m_Height,
			m_BitmapTransform,m_HeifCompressionMethod,"source-format");
	}
	// No pixel or metadata edits: preserve the HEIF container byte-for-byte.
	// Previously the default encoder option (DontCare) decoded/recompressed
	// our own source frame and silently discarded EXIF/XMP/MakerNote/HDR.
	// Only the actual primary decoder frame has IHeicRawSource; a Photos
	// bitmap/flip-rotator never gains that capability by guessing provenance.
	// Explicit HEVC compression (2), supplied ICC, cropping or geometry
	// changes prohibit this fast path.
	if (pIBitmapSource && !prc && m_BitmapTransform == 0 &&
	    m_HeifCompressionMethod != 2 &&
	    !m_LosslessReady && !m_HasPixels && !m_WrittenLines && !m_Created) {
		IHeicRawSource* originalSource = nullptr;
		if (SUCCEEDED(pIBitmapSource->QueryInterface(
		        IID_IHeicRawSource, reinterpret_cast<void**>(&originalSource))) &&
		    originalSource) {
			// Never ignore an explicit ICC removal or replacement.
			// No explicit color request retains the entire source container.
			const bool sameIcc=RequestedICCMatchesSource(pIBitmapSource,
				m_IccExplicitlySet,m_IccProfile);
			const bool sameFormat=RequestedPixelFormatMatchesSource(
				pIBitmapSource,m_PixelFormatExplicitlySet,m_PixelFormat);
			UINT w=0,h=0;
			HRESULT hr = pIBitmapSource->GetSize(&w,&h);
			if (SUCCEEDED(hr) && sameIcc && sameFormat && m_Width == w && m_Height == h) {
				std::vector<uint8_t> originalBytes;
				hr = originalSource->ReadFullContent(&originalBytes);
				originalSource->Release();
				if (FAILED(hr)) return hr;
				if (originalBytes.empty()) return WINCODEC_ERR_BADIMAGE;
				m_LosslessBytes = std::move(originalBytes);
				m_LosslessReady = true;
				WicHeicDiagnostic::Emit("WriteSourceLossless",
					m_Width,m_Height,0,m_HeifCompressionMethod,"exact-source-container");
				return S_OK;
			}
			originalSource->Release();
			// Never fall through to RGB re-encoding of our own HEIC after a
			// rejected metadata-preserving copy: this silently strips EXIF,
			// XMP, HDR gain maps and embedded thumbnails.
			WicHeicDiagnostic::Emit("WriteSourceRejected",m_Width,m_Height,0,
				m_HeifCompressionMethod,"unsafe-source-reencode",
				WINCODEC_ERR_UNSUPPORTEDOPERATION);
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}
	}
	if (m_HeifCompressionMethod==1 || m_BitmapTransform!=0) {
		// A decoded RGB bitmap or a WIC flip-rotator has no HEVC bitstream to
		// preserve. Only source frames from OUR decoder may take this path.
		if(!pIBitmapSource || prc || m_LosslessReady || m_HasPixels)
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		IHeicRawSource* raw=nullptr;
		HRESULT hr=pIBitmapSource->QueryInterface(
			IID_IHeicRawSource,reinterpret_cast<void**>(&raw));
		if(FAILED(hr)||!raw){
			WicHeicDiagnostic::Emit("WriteSourceRejected",m_Width,m_Height,m_BitmapTransform,m_HeifCompressionMethod,"raster-only",WINCODEC_ERR_UNSUPPORTEDOPERATION);
			DbgLog("HEIC_WIC_LOSSLESS_UNAVAILABLE source is not an original HEIC frame");
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}
		// Replacing ICC while changing irot would silently mislabel color.
		if(!RequestedICCMatchesSource(pIBitmapSource,
		       m_IccExplicitlySet,m_IccProfile)) {
			raw->Release();
			WicHeicDiagnostic::Emit("WriteSourceRejected",m_Width,m_Height,
				m_BitmapTransform,m_HeifCompressionMethod,
				"lossless-rotation-icc-edit-not-supported",
				WINCODEC_ERR_UNSUPPORTEDOPERATION);
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}
		if(!RequestedPixelFormatMatchesSource(pIBitmapSource,
		        m_PixelFormatExplicitlySet,m_PixelFormat)) {
			raw->Release();
			WicHeicDiagnostic::Emit("WriteSourceRejected",m_Width,m_Height,
				m_BitmapTransform,m_HeifCompressionMethod,
				"lossless-rotation-pixel-format-change-not-supported",
				WINCODEC_ERR_UNSUPPORTEDOPERATION);
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}
		std::vector<uint8_t> original;
		hr=raw->ReadFullContent(&original);
		raw->Release();
		if(FAILED(hr))return hr;
		UINT w=0,h=0;
		hr=pIBitmapSource->GetSize(&w,&h);
		if(FAILED(hr))return hr;
		UINT expectedW=(m_BitmapTransform&1)?h:w;
		UINT expectedH=(m_BitmapTransform&1)?w:h;
		if((m_Width && m_Width!=expectedW) ||
		   (m_Height && m_Height!=expectedH))
			return E_INVALIDARG;
		try {
			m_LosslessBytes=HeicLosslessNative::Transform(
				original,m_BitmapTransform);
			m_Width=expectedW;m_Height=expectedH;
			m_LosslessReady=true;
			WicHeicDiagnostic::Emit("WriteSourceLossless",m_Width,m_Height,m_BitmapTransform,m_HeifCompressionMethod,"raw-heif");
			DbgLog("HEIC_WIC_LOSSLESS_READY transform=%u dimensions=%ux%u bytecount=%zu",
				unsigned(m_BitmapTransform),expectedW,expectedH,m_LosslessBytes.size());
			return S_OK;
		} catch(const std::exception& ex) {
			WicHeicDiagnostic::Emit("WriteSourceRejected",m_Width,m_Height,m_BitmapTransform,m_HeifCompressionMethod,"unsupported-heif",WINCODEC_ERR_UNSUPPORTEDOPERATION);
			DbgLog("HEIC_WIC_LOSSLESS_UNAVAILABLE reason=%s",ex.what());
			m_LosslessBytes.clear();
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}
	}
	if (!pIBitmapSource) return E_POINTER;
	// A materialized IWICBitmap is ALSO unsafe: CreateBitmapFromSource
	// can flatten a rotated HEIC to bare pixels, discarding EXIF, ICC,
	// auxiliary images and the relationship to its original bitstream.
	// Legitimate new RGB image creation must use the explicit WritePixels
	// encoding API; WriteSource cannot distinguish it from flattened HEIC.
	// WIC raster wrappers may hide a HEIC source's provenance. A flip-rotator
	// alone is not sufficient to guard this: wrapping it in a format converter,
	// scaler or clipper hides the IWICBitmapFlipRotator interface from the HEIC
	// encoder, while exposing no HEVC stream, EXIF, ICC, gain maps or auxiliary
	// metadata. Deny these lossy/ambiguous sources under both default and
	// explicitly requested HEVC compression. An original decoder frame with
	// IHeicRawSource was already handled above by the verified lossless route.
	// Do not infer the source from dimensions, pixels, filename, or a cache.
	const IID* const unsafeWicRasterInterfaces[] = {
		&IID_IWICBitmapFlipRotator,
		&IID_IWICFormatConverter,
		&IID_IWICBitmapScaler,
		&IID_IWICBitmapClipper,
		&IID_IWICBitmap
	};
	const char* const unsafeWicRasterLabels[] = {
		"wic-fliprotator-without-heic-provenance",
		"wic-formatconverter-without-heic-provenance",
		"wic-scaler-without-heic-provenance",
		"wic-clipper-without-heic-provenance",
		"wic-bitmap-materialized-without-heic-provenance"
	};
	for (size_t i=0; i<sizeof(unsafeWicRasterInterfaces)/sizeof(unsafeWicRasterInterfaces[0]); ++i) {
		IUnknown* wrapped=nullptr;
		const HRESULT queried=pIBitmapSource->QueryInterface(
			*unsafeWicRasterInterfaces[i],reinterpret_cast<void**>(&wrapped));
		if (wrapped) wrapped->Release();
		if (SUCCEEDED(queried)) {
			WicHeicDiagnostic::Emit("WriteSourceRejected",m_Width,m_Height,
				m_BitmapTransform,m_HeifCompressionMethod,
				unsafeWicRasterLabels[i],WINCODEC_ERR_UNSUPPORTEDOPERATION);
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}
	}
	// Even when the caller explicitly asks for HEVC recompression or a crop,
	// a source frame from our HEIC decoder may carry EXIF, MakerNote, ICC,
	// XMP, thumbnails or HDR items. Our raster encoder cannot transfer all
	// of those items. Never convert an identifiable original HEIC frame to
	// RGB behind the caller's back and silently discard its metadata.
	// Transformed IWICBitmapSource wrappers do NOT expose this private IID;
	// they still need a separate provenance-preserving edit pipeline.
	IHeicRawSource* originalForRaster = nullptr;
	if (SUCCEEDED(pIBitmapSource->QueryInterface(
	        IID_IHeicRawSource, reinterpret_cast<void**>(&originalForRaster))) &&
	    originalForRaster) {
		originalForRaster->Release();
		WicHeicDiagnostic::Emit("WriteSourceRejected",m_Width,m_Height,
			m_BitmapTransform,m_HeifCompressionMethod,
			"raw-heic-raster-would-strip-metadata",WINCODEC_ERR_UNSUPPORTEDOPERATION);
		return WINCODEC_ERR_UNSUPPORTEDOPERATION;
	}
	if (!m_Width || !m_Height || m_Created || m_HasPixels || m_WrittenLines)
		return WINCODEC_ERR_WRONGSTATE;

	UINT sourceW = 0, sourceH = 0;
	HRESULT hr = pIBitmapSource->GetSize(&sourceW, &sourceH);
	if (FAILED(hr)) return hr;

	UINT copyW = sourceW, copyH = sourceH;
	if (prc) {
		if (prc->X < 0 || prc->Y < 0 || prc->Width <= 0 || prc->Height <= 0)
			return E_INVALIDARG;
		if (uint64_t(prc->X) + UINT(prc->Width) > sourceW ||
			uint64_t(prc->Y) + UINT(prc->Height) > sourceH)
			return E_INVALIDARG;
		copyW = UINT(prc->Width);
		copyH = UINT(prc->Height);
	}
	// Fail rather than stretch, crop unexpectedly, or leave partially filled
	// planes when Photos passes an inconsistent SetSize/WriteSource pair.
	if (copyW != m_Width || copyH != m_Height) {
		WicHeicDiagnostic::Emit("WriteSourceRejected",m_Width,m_Height,m_BitmapTransform,m_HeifCompressionMethod,"dimension-mismatch",E_INVALIDARG);
		Log("WriteSource SIZE_MISMATCH src=%ux%u copy=%ux%u frame=%ux%u",
			sourceW, sourceH, copyW, copyH, m_Width, m_Height);
		return E_INVALIDARG;
	}

	// Avoid allocating output pixels until inputs and dimensions are validated.
	WICPixelFormatGUID sourceFormat;
	hr = pIBitmapSource->GetPixelFormat(&sourceFormat);
	if (FAILED(hr)) return hr;

	struct AutoReleaseSource {
		IWICBitmapSource* p = nullptr;
		~AutoReleaseSource() { if (p) p->Release(); }
	} converted;
	IWICBitmapSource* selected = pIBitmapSource;
	if (!IsEqualGUID(sourceFormat, m_PixelFormat)) {
		hr = WICConvertBitmapSource(m_PixelFormat, pIBitmapSource, &converted.p);
		if (FAILED(hr)) return hr;
		selected = converted.p;
	}

	try {
		CreateFrame();
		int stride = 0;
		uint8_t* plane = m_Frame.get_plane(heif_channel_interleaved, &stride);
		if (!plane || stride <= 0) return WINCODEC_ERR_BADIMAGE;
		const uint64_t bufferBytes = uint64_t(stride) * m_Height;
		if (bufferBytes > 0xFFFFFFFFULL) return E_INVALIDARG;
		// CRITICAL: propagate CopyPixels HRESULT even after format conversion.
		hr = selected->CopyPixels(prc, UINT(stride), UINT(bufferBytes), plane);
		if (FAILED(hr)) {
			WicHeicDiagnostic::Emit("WriteSourceCopyFailed",m_Width,m_Height,m_BitmapTransform,m_HeifCompressionMethod,"raster",hr);
			Log("WriteSource CopyPixels failed HRESULT=0x%08X", unsigned(hr));
			return hr;
		}
		if (m_BitDepthPerChannel == 10) {
			const size_t rowBytes = size_t(m_Width) *
				(IsEqualGUID(m_PixelFormat, GUID_WICPixelFormat48bppRGB) ? 6u : 8u);
			for (UINT y=0; y<m_Height; ++y)
				Wic16ToHeic10(plane + size_t(stride)*y, rowBytes);
		}
		m_WrittenLines = m_Height;
		m_HasPixels = true;
		WicHeicDiagnostic::Emit("WriteSourceRaster",m_Width,m_Height,m_BitmapTransform,m_HeifCompressionMethod,"raster");
		return S_OK;
	}
	catch (const std::exception& ex) {
		Log("WriteSource failed: %s", ex.what());
		return WINCODEC_ERR_BADIMAGE;
	}
	catch (...) { return WINCODEC_ERR_BADIMAGE; }
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::Commit(void)
{
	WicHeicDiagnostic::Emit("FrameCommit",m_Width,m_Height,m_BitmapTransform,m_HeifCompressionMethod,m_LosslessReady?"lossless":"raster");
	if (m_LosslessReady && !m_LosslessBytes.empty()) return S_OK;
	if (m_HeifCompressionMethod==1 || m_BitmapTransform!=0) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
	if (!m_HasPixels || m_WrittenLines != m_Height) return WINCODEC_ERR_WRONGSTATE;
	try
	{
		CreateFrame();
		return S_OK;
	}
	catch (const heif::Error& ex)
	{
		Log("Call to CHeicBitmapFrameEncode::Commit failed: %s", ex.get_message().c_str());
		return E_INVALIDARG;
	}
	catch (const std::exception& ex)
	{
		Log("Call to CHeicBitmapFrameEncode::Commit failed: %s", ex.what());
		return E_INVALIDARG;
	}
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::GetMetadataQueryWriter(__RPC__deref_out_opt IWICMetadataQueryWriter **ppIMetadataQueryWriter)
{
	if (!ppIMetadataQueryWriter) return E_POINTER;
	*ppIMetadataQueryWriter = nullptr;
	WicHeicDiagnostic::Emit("MetadataWriterUnavailable",0,0,m_BitmapTransform,m_HeifCompressionMethod,"metadata-rejected",E_NOTIMPL);
	return E_NOTIMPL;
}

void CHeicBitmapFrameEncode::CreateFrame()
{
	if (m_Created) return;
	if (!m_Width || !m_Height || m_Chroma == heif_chroma_undefined)
		throw std::runtime_error("Frame dimension or pixel format not initialized");
	m_Frame.create(m_Width, m_Height, m_Colorspace, m_Chroma);
	m_Frame.add_plane(heif_channel_interleaved, m_Width, m_Height, m_BitDepthPerChannel);
	if (!m_IccProfile.empty())
		m_Frame.set_raw_color_profile(heif_color_profile_type_prof, m_IccProfile);

	m_Created = true;
}
