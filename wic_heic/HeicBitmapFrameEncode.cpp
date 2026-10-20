#include "stdafx.h"
#include "wic_heic.h"
#include "HeicBitmapFrameEncode.h"
#include "HeicBitmapEncoder.h"
#include <stdexcept>


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
	, m_PixelFormat(GUID_WICPixelFormat24bppRGB)
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
	DbgLog("HEIC_WIC_OPTIONS transform=%u compression=%u", unsigned(transform),unsigned(compression));
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::SetSize(UINT uiWidth, UINT uiHeight)
{
	if (m_Created || m_HasPixels) return WINCODEC_ERR_WRONGSTATE;
	if (!uiWidth || !uiHeight || uiWidth > 20000 || uiHeight > 20000)
		return E_INVALIDARG;
	m_Width = uiWidth;
	m_Height = uiHeight;
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
	if (m_Created || m_HasPixels) return WINCODEC_ERR_WRONGSTATE;

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
	// All below is not tested - not supported
	else if (IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat48bppBGR)) {
		m_PixelFormat = GUID_WICPixelFormat32bppRGBA1010102;
		m_Chroma = heif_chroma_interleaved_RRGGBB_BE;
		m_BitDepthPerChannel = 16;
	}
	else if (IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat48bppRGB)) {
		m_PixelFormat = GUID_WICPixelFormat32bppRGBA1010102;
		m_Chroma = heif_chroma_interleaved_RRGGBB_LE;
		m_BitDepthPerChannel = 16;
	}
	else if (IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat64bppBGRA)) {
		m_PixelFormat = GUID_WICPixelFormat32bppRGBA1010102;
		m_Chroma = heif_chroma_interleaved_RRGGBBAA_BE;
		m_BitDepthPerChannel = 16;
	}
	else if (IsEqualGUID(*pPixelFormat, GUID_WICPixelFormat64bppRGBA)) {
		m_PixelFormat = GUID_WICPixelFormat32bppRGBA1010102;
		m_Chroma = heif_chroma_interleaved_RRGGBBAA_LE; // !
		m_BitDepthPerChannel = 16;
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
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::SetColorContexts(UINT cCount, __RPC__in_ecount_full(cCount) IWICColorContext **ppIColorContext)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::SetPalette(__RPC__in_opt IWICPalette *pIPalette)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::SetThumbnail(__RPC__in_opt IWICBitmapSource *pIThumbnail)
{
	try
	{
		// TODO
		return S_OK;
	}
	catch (const heif::Error& ex)
	{
		Log("Call to CHeicBitmapFrameEncode::SetThumbnail failed: %s", ex.get_message().c_str());
		return E_INVALIDARG;
	}
	catch (const std::exception& ex)
	{
		Log("Call to CHeicBitmapFrameEncode::SetThumbnail failed: %s", ex.what());
		return E_INVALIDARG;
	}
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::WritePixels(UINT lineCount, UINT cbStride, UINT cbBufferSize, __RPC__in_ecount_full(cbBufferSize) BYTE *pbPixels)
{
	if (m_HeifCompressionMethod==1 || m_BitmapTransform!=0) {
		DbgLog("HEIC_WIC_LOSSLESS_UNAVAILABLE WritePixels transform=%u compression=%u",unsigned(m_BitmapTransform),unsigned(m_HeifCompressionMethod));
		return WINCODEC_ERR_UNSUPPORTEDOPERATION;
	}
	if (!pbPixels) return E_POINTER;
	if (!m_Width || !m_Height || !lineCount || m_HasPixels) return WINCODEC_ERR_WRONGSTATE;
	if (lineCount > m_Height - m_WrittenLines) return E_INVALIDARG;

	// Only the explicitly supported 8-bit interleaved formats may be written
	// through this path; never silently truncate 10/12/16-bit samples.
	UINT bytesPerPixel = 0;
	if (IsEqualGUID(m_PixelFormat, GUID_WICPixelFormat24bppRGB)) bytesPerPixel = 3;
	else if (IsEqualGUID(m_PixelFormat, GUID_WICPixelFormat32bppRGBA)) bytesPerPixel = 4;
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

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameEncode::WriteSource(__RPC__in_opt IWICBitmapSource *pIBitmapSource, __RPC__in_opt WICRect *prc)
{
	if (m_HeifCompressionMethod==1 || m_BitmapTransform!=0) {
		UINT w=0,h=0;
		if(pIBitmapSource) pIBitmapSource->GetSize(&w,&h);
		DbgLog("HEIC_WIC_LOSSLESS_UNAVAILABLE WriteSource transform=%u compression=%u src=%ux%u frame=%ux%u",unsigned(m_BitmapTransform),unsigned(m_HeifCompressionMethod),w,h,m_Width,m_Height);
		return WINCODEC_ERR_UNSUPPORTEDOPERATION;
	}
	if (!pIBitmapSource) return E_POINTER;
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
			Log("WriteSource CopyPixels failed HRESULT=0x%08X", unsigned(hr));
			return hr;
		}
		m_WrittenLines = m_Height;
		m_HasPixels = true;
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
	return E_NOTIMPL;
}

void CHeicBitmapFrameEncode::CreateFrame()
{
	if (m_Created) return;
	if (!m_Width || !m_Height || m_Chroma == heif_chroma_undefined)
		throw std::runtime_error("Frame dimension or pixel format not initialized");
	m_Frame.create(m_Width, m_Height, m_Colorspace, m_Chroma);
	m_Frame.add_plane(heif_channel_interleaved, m_Width, m_Height, m_BitDepthPerChannel);

	m_Created = true;
}
