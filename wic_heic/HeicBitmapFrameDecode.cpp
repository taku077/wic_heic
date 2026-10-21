#include "stdafx.h"
#include "wic_heic.h"
#include "HeifStreamReader.h"
#include "HeicBitmapDecoder.h"
#include "HeicBitmapFrameDecode.h"
#include "HeicMetadataBlockReader.h"
#ifdef WIC_HEIC_PRIVATE_FAST2
#include <stdexcept>
#include <new>
#endif
#include "PhotosWicTrace.h"

CHeicBitmapFrameDecode::CHeicBitmapFrameDecode(CHeicBitmapDecoder* decoder, heif::ImageHandle handle)
	: m_Count(1)
	, m_pDecoder(decoder)
	, m_Handle(handle)
	, m_Bpp(0)
	, m_SourceBits(8)
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

	// A 10/12/16-bit HEIC must not be advertised as 8-bit RGB.
	// WIC 48/64bpp formats store little-endian 16-bit channels. The HEIF
	// decoder produces native 10/12-bit samples; CopyPixels scales those
	// samples to WIC's full 16-bit range without dropping source precision.
	const bool alpha = m_Handle.has_alpha_channel();
	const bool highBit = (bpc == 10 || bpc == 12 || bpc == 16);
	m_SourceBits = highBit ? bpc : 8;
	if (highBit) {
		m_chroma = alpha ? heif_chroma_interleaved_RRGGBBAA_LE :
		                  heif_chroma_interleaved_RRGGBB_LE;
		m_PixelFormat = alpha ? GUID_WICPixelFormat64bppRGBA :
		                        GUID_WICPixelFormat48bppRGB;
		m_Bpp = alpha ? 64 : 48;
	} else {
		m_chroma = alpha ? heif_chroma_interleaved_RGBA :
		                  heif_chroma_interleaved_RGB;
		m_PixelFormat = alpha ? GUID_WICPixelFormat32bppRGBA :
		                        GUID_WICPixelFormat24bppRGB;
		m_Bpp = alpha ? 32 : 24;
	}

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

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::CopyPixels(__RPC__in_opt const WICRect *prc, UINT cbStride, UINT cbBufferSize, __RPC__out_ecount_full(cbBufferSize) BYTE *pbBuffer)
{
	WicHeicDiagnostic::Emit("DecoderCopyPixels",prc?UINT(prc->Width):UINT(m_Handle.get_width()),prc?UINT(prc->Height):UINT(m_Handle.get_height()),0,0,prc?"region":"full");
	DbgLog("%s: (prc=%p, cbStride=%u, cbBufferSize=%u, pbBuffer=%p)", __FUNCTION__, prc, cbStride, cbBufferSize, pbBuffer);

	try
	{
		DecodeImage();
	}
	catch (...)
	{
		DbgLog("%s: Exception", __FUNCTION__);
		return WINCODEC_ERR_BADIMAGE;
	}

	if (!pbBuffer) {
		DbgLog("%s: pbBuffer is null", __FUNCTION__);
		return E_POINTER;
	}

	WICRect rc;
	rc.X = 0;
	rc.Y = 0;
	rc.Width = m_Handle.get_width();
	rc.Height = m_Handle.get_height();

	if (prc) {
		rc = *prc;
	}

	DbgLog("CopyPixels: (X=%d,Y=%d,W=%d,H=%d), outStride=%u, inStride=%d, bpp=%d",
		rc.X, rc.Y, rc.Width, rc.Height,
		cbStride, m_Stride, m_Bpp);

	if (rc.X < 0 || rc.Y < 0 || rc.Width <= 0 || rc.Height <= 0 ||
		uint64_t(rc.X) + UINT(rc.Width) > UINT(m_Handle.get_width()) ||
		uint64_t(rc.Y) + UINT(rc.Height) > UINT(m_Handle.get_height()))
		return E_INVALIDARG;
	const UINT bytesPerPixel = UINT(m_Bpp / 8);
	const uint64_t rowBytes = uint64_t(UINT(rc.Width)) * bytesPerPixel;
	const uint64_t inputOffset = uint64_t(UINT(rc.X)) * bytesPerPixel;
	const uint64_t outputBytes = (uint64_t(UINT(rc.Height)) - 1) * cbStride + rowBytes;
	if (rowBytes > cbStride || outputBytes > cbBufferSize ||
		inputOffset + rowBytes > m_Stride) return E_INVALIDARG;
	const uint8_t* plane = m_PlaneInterleaved +
		(size_t(m_Stride) * size_t(rc.Y)) + size_t(inputOffset);
	uint8_t* out = pbBuffer;
	const uint32_t sourceMax = (1u << m_SourceBits) - 1u;
	for (int row = 0; row < rc.Height; ++row) {
		if (m_SourceBits <= 8) {
			memcpy(out, plane, size_t(rowBytes));
		} else {
			// Convert HEIF N-bit integer samples into WIC 16-bit full range.
			// Input and output are little-endian packed unsigned channels.
			for (size_t i = 0; i < size_t(rowBytes); i += 2) {
				const uint32_t sample = uint32_t(plane[i]) |
				                        (uint32_t(plane[i + 1]) << 8);
				if (sample > sourceMax) return WINCODEC_ERR_BADIMAGE;
				const uint32_t normalized = (sample * 65535u + sourceMax/2) / sourceMax;
				out[i] = BYTE(normalized & 255);
				out[i + 1] = BYTE(normalized >> 8);
			}
		}
		out += cbStride;
		plane += m_Stride;
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

	CHeicMetadataBlockReader* pMetadataBlockReader = new(std::nothrow) CHeicMetadataBlockReader(this->m_Handle);
	if (!pMetadataBlockReader) {
		return E_OUTOFMEMORY;
	}

	HRESULT hr = pMetadataBlockReader->CreateQueryReader(ppIMetadataQueryReader);
	return hr;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapFrameDecode::GetColorContexts(UINT cCount, __RPC__inout_ecount_full_opt(cCount) IWICColorContext **ppIColorContexts, __RPC__out UINT *pcActualCount)
{
	if (!pcActualCount) return E_POINTER;
	*pcActualCount = 0;
	if (cCount && !ppIColorContexts) return E_POINTER;
	if (cCount) ppIColorContexts[0] = nullptr;
	const heif_image_handle* handle = m_Handle.get_raw_image_handle();
	if (!handle) return WINCODEC_ERR_BADIMAGE;
	const size_t profileSize = heif_image_handle_get_raw_color_profile_size(handle);
	if (!profileSize) return S_OK;
	if (profileSize > 16u*1024u*1024u) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
	*pcActualCount = 1;
	if (!cCount) return S_OK;
	std::vector<uint8_t> bytes(profileSize);
	const heif_error err = heif_image_handle_get_raw_color_profile(handle, bytes.data());
	if (err.code != heif_error_Ok) return WINCODEC_ERR_BADIMAGE;
	IWICImagingFactory* factory = nullptr;
	IWICColorContext* color = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&factory));
	if (SUCCEEDED(hr)) hr = factory->CreateColorContext(&color);
	if (SUCCEEDED(hr)) hr = color->InitializeFromMemory(bytes.data(), UINT(bytes.size()));
	if (SUCCEEDED(hr)) ppIColorContexts[0] = color;
	else if (color) color->Release();
	if (factory) factory->Release();
	return hr;
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

	// Small thumbnails and uncommon HEIC formats keep the original decoder.
	// The CI-only 12MP A/B showed exact PBGRA pixel parity and lower wall
	// time at two codec threads, at a measured cost in CPU time.
#ifdef WIC_HEIC_PRIVATE_FAST2
	// Only the MSI's PRIVATE decoder is built with this macro. Never enable
	// it in the globally registered WIC codec or the Windows Photos encoder.
	// Restrict extra CPU usage to high-resolution primary frames.
	const uint64_t pixels = uint64_t(m_Handle.get_width()) * uint64_t(m_Handle.get_height());
	SYSTEM_INFO sys{};
	GetSystemInfo(&sys);
	if (pixels >= 6000000ull && m_SourceBits == 8 &&
		!m_Handle.has_alpha_channel() && sys.dwNumberOfProcessors >= 2) {
		heif_decoding_options* options = heif_decoding_options_alloc();
		if (!options) throw std::bad_alloc();
		options->num_codec_threads = 2;
		heif_image* decoded = nullptr;
		const heif_error error = heif_decode_image(m_Handle.get_raw_image_handle(),
			&decoded, heif_colorspace_RGB, m_chroma, options);
		heif_decoding_options_free(options);
		if (error.code != heif_error_Ok) {
			if (decoded) heif_image_release(decoded);
			throw heif::Error(error);
		}
		if (!decoded) throw std::runtime_error("HEIC decode image missing");
		m_Image = heif::Image(decoded);
	} else {
		m_Image = m_Handle.decode_image(heif_colorspace_RGB, m_chroma);
	}
#else
	m_Image = m_Handle.decode_image(heif_colorspace_RGB, m_chroma);
#endif
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
