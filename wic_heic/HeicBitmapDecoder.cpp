#include "stdafx.h"
#include "wic_heic.h"
#include "HeicThumbnailGeometry.h"
#include "HeifStreamReader.h"
#include "HeicBitmapDecoder.h"
#include "PhotosWicTrace.h"
#include "HeicBitmapFrameDecode.h"

CHeicBitmapDecoder::CHeicBitmapDecoder()
	: m_Count(1)
	, m_Reader(nullptr)
{
	DbgLog("%s", __FUNCTION__);
}


CHeicBitmapDecoder::~CHeicBitmapDecoder()
{
	DbgLog("%s", __FUNCTION__);
	if (m_Reader) {
		delete m_Reader;
	}
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::QueryInterface(REFIID riid, void **ppvObject)
{
	DbgLog("%s", __FUNCTION__);

	HRESULT hr = S_OK;

	if (!ppvObject) {
		return E_POINTER;
	}

	*ppvObject = nullptr;
	if (IsEqualGUID(riid, IID_IUnknown)) {
		*ppvObject = static_cast<IUnknown*>(this);
	} else if (IsEqualGUID(riid, IID_IWICBitmapDecoder)) {
		*ppvObject = static_cast<IWICBitmapDecoder*>(this);
	} else {
		hr = E_NOINTERFACE;
	}

	if (SUCCEEDED(hr)) {
		AddRef();
	}

	return hr;
}

ULONG STDMETHODCALLTYPE CHeicBitmapDecoder::AddRef(void)
{
	DbgLog("%s", __FUNCTION__);

	return ++m_Count;
}

ULONG STDMETHODCALLTYPE CHeicBitmapDecoder::Release(void)
{
	DbgLog("%s", __FUNCTION__);

	uint32_t n = --m_Count;
	if (m_Count == 0) {
		delete this;
	}

	return n;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::QueryCapability(__RPC__in_opt IStream *pIStream, __RPC__out DWORD *pdwCapability)
{
	// WIC calls this method to arbitrate among codecs claiming the same HEIF
	// container. Older builds claimed SameEncoder and Thumbnail for EVERY file,
	// including iPhone HEIC without an embedded thumbnail and even non-HEIF
	// streams. This violates IWICBitmapDecoder and can confuse Photos.
	if (!pdwCapability || !pIStream) return E_POINTER;
	*pdwCapability = 0;
	LARGE_INTEGER zero{};
	ULARGE_INTEGER original{};
	HRESULT hr = pIStream->Seek(zero, STREAM_SEEK_CUR, &original);
	if (FAILED(hr)) return hr;
	DWORD capabilities = 0;
	try {
		// Read from the supplied stream, not m_Context: arbitration may run
		// before Initialize. CHeifStreamReader seeks to offset zero.
		CHeifStreamReader reader(pIStream);
		heif::Context context;
		context.read_from_reader(reader);
		const auto ids = context.get_list_of_top_level_image_IDs();
		if (!ids.empty()) {
			// A parsed primary is not a guarantee all auxiliary/sequence images
			// can be decoded. Do not overclaim CanDecodeAllImages.
			auto frame = context.get_image_handle(ids.front());
			if (!frame.empty() && frame.get_width() > 0 &&
			    frame.get_height() > 0) {
				capabilities |= WICBitmapDecoderCapabilityCanDecodeSomeImages;
				// Metadata is enumerated by our IWICMetadataBlockReader.
				(void)frame.get_list_of_metadata_block_IDs();
				capabilities |= WICBitmapDecoderCapabilityCanEnumerateMetadata;
				const auto thumbnails = frame.get_list_of_thumbnail_IDs();
				if (!thumbnails.empty()) {
					auto thumbnail = frame.get_thumbnail(thumbnails.front());
					if (!thumbnail.empty() && HeicThumbnailGeometry::Compatible(
					        frame.get_width(),frame.get_height(),
					        thumbnail.get_width(),thumbnail.get_height())) {
						capabilities |= WICBitmapDecoderCapabilityCanDecodeThumbnail;
					}
				}
			}
		}
	} catch (...) {
		// An unsupported or malformed stream is not an HEIC capability claim.
		capabilities = 0;
	}
	// The WIC specification requires the caller's seek position to be
	// unchanged, including when parsing fails; otherwise subsequent decoder
	// candidates may receive the stream at the wrong offset.
	LARGE_INTEGER restore{};
	restore.QuadPart = original.QuadPart;
	hr = pIStream->Seek(restore, STREAM_SEEK_SET, nullptr);
	if (FAILED(hr)) return hr;
	*pdwCapability = capabilities;
	WicHeicDiagnostic::Emit("DecoderQueryCapability",
		UINT(capabilities),0,0,0,"stream-verified");
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::Initialize(__RPC__in_opt IStream *pIStream, WICDecodeOptions cacheOptions)
{
	WicHeicDiagnostic::Emit("DecoderOpen",0,0,0,0,pIStream?"stream":"null");
	// https://github.com/strukturag/libheif/issues/83
	try
	{
		DbgLog("%s: pIStream=%p, mReader=%p", __FUNCTION__, pIStream, m_Reader);
		if (m_Reader) {
			return WINCODEC_ERR_WRONGSTATE;
		}

		if (!pIStream) {
			return E_INVALIDARG;
		}

		// Do not request the filename for every Explorer thumbnail. A
		// STATFLAG_DEFAULT call allocates pwcsName and costs extra COM/FS
		// work; the stream reader already obtains its size with NONAME.
		m_Reader = new CHeifStreamReader(pIStream);
		LARGE_INTEGER counterFrequency{},before{},after{};
		QueryPerformanceFrequency(&counterFrequency);
		QueryPerformanceCounter(&before);
		m_Context.read_from_reader(*m_Reader);
		QueryPerformanceCounter(&after);
		// Diagnostic opt-in only. Numeric microseconds, never image contents
		// or paths. Investigates slow Explorer cold-thumbnail initialization.
		if(counterFrequency.QuadPart>0 && after.QuadPart>=before.QuadPart) {
			const uint64_t elapsedUs=
				uint64_t(after.QuadPart-before.QuadPart)*1000000ull/
				uint64_t(counterFrequency.QuadPart);
			WicHeicDiagnostic::Emit("DecoderContextReadUs",
				UINT((std::min)(elapsedUs,uint64_t(0xFFFFFFFFu))),0,0,0,"microseconds");
		}
	}
	catch (const std::bad_alloc&)
	{
		// Only an actual allocation failure should be reported as such.
		return E_OUTOFMEMORY;
	}
	catch (const heif::Error& ex)
	{
		DbgLog("%s: invalid or unsupported HEIF: %d, %d, %s",
			__FUNCTION__, ex.get_code(), ex.get_subcode(), ex.get_message());
		return WINCODEC_ERR_BADIMAGE;
	}
	catch (const std::exception& ex)
	{
		DbgLog("%s: invalid HEIF stream: %s", __FUNCTION__, ex.what());
		return WINCODEC_ERR_BADIMAGE;
	}
	catch (...)
	{
		DbgLog("%s: unexpected HEIF parsing exception", __FUNCTION__);
		return WINCODEC_ERR_BADIMAGE;
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::GetContainerFormat(__RPC__out GUID *pguidContainerFormat)
{
	DbgLog("%s", __FUNCTION__);

	if (!pguidContainerFormat) {
		return E_POINTER;
	}

	*pguidContainerFormat = GUID_ContainerFormatHEIC;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::GetDecoderInfo(__RPC__deref_out_opt IWICBitmapDecoderInfo **ppIDecoderInfo)
{
	DbgLog("%s", __FUNCTION__);

	if (!ppIDecoderInfo) {
		return E_POINTER;
	}

	HRESULT hr = S_OK;
	IWICImagingFactory* pFactory = nullptr;
	IWICComponentInfo* pCompInfo = nullptr;

	hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));

	if (SUCCEEDED(hr)) {
		hr = pFactory->CreateComponentInfo(CLSID_HEICDecoder, &pCompInfo);
	}

	if (SUCCEEDED(hr)) {
		hr = pCompInfo->QueryInterface(IID_IWICBitmapDecoderInfo, (void**)ppIDecoderInfo);
	}

	if (pFactory) pFactory->Release();
	if (pCompInfo) pCompInfo->Release();

	return hr;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::CopyPalette(__RPC__in_opt IWICPalette *pIPalette)
{
	DbgLog("%s", __FUNCTION__);

	return WINCODEC_ERR_PALETTEUNAVAILABLE;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::GetMetadataQueryReader(__RPC__deref_out_opt IWICMetadataQueryReader **ppIMetadataQueryReader)
{
	DbgLog("%s", __FUNCTION__);

	if (!ppIMetadataQueryReader) {
		return E_POINTER;
	}

	*ppIMetadataQueryReader = nullptr;

	return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::GetPreview(__RPC__deref_out_opt IWICBitmapSource **ppIBitmapSource)
{
	DbgLog("%s", __FUNCTION__);

	if (!ppIBitmapSource) {
		return E_POINTER;
	}

	*ppIBitmapSource = nullptr;

	return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::GetColorContexts(UINT cCount, __RPC__inout_ecount_full_opt(cCount) IWICColorContext **ppIColorContexts, __RPC__out UINT *pcActualCount)
{
	if (!pcActualCount) return E_POINTER;
	*pcActualCount=0;
	if (cCount && !ppIColorContexts) return E_POINTER;
	if (cCount) ppIColorContexts[0]=nullptr;
	if (!m_Reader) return WINCODEC_ERR_WRONGSTATE;
	// Applications may request the container-level color profile instead of
	// its frame profile. Do not claim "unsupported" for a real source ICC:
	// it caused callers to discard the display color-space on re-export.
	try {
		heif::ImageHandle primary=m_Context.get_primary_image_handle();
		if (primary.empty()) return WINCODEC_ERR_BADIMAGE;
		CHeicBitmapFrameDecode* frame=new(std::nothrow) CHeicBitmapFrameDecode(this,primary);
		if (!frame) return E_OUTOFMEMORY;
		const HRESULT hr=frame->GetColorContexts(cCount,ppIColorContexts,pcActualCount);
		frame->Release();
		return hr;
	} catch (...) {
		return WINCODEC_ERR_BADIMAGE;
	}
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::GetThumbnail(__RPC__deref_out_opt IWICBitmapSource **ppIThumbnail)
{
	if (!ppIThumbnail) return E_POINTER;
	*ppIThumbnail = nullptr;
	try
	{
		DbgLog("GetThumbnail@1");

		if (!m_Reader) {
			return WINCODEC_ERR_WRONGSTATE;
		}

		heif::ImageHandle handle = m_Context.get_primary_image_handle();

		std::vector<heif_item_id> thumbIds = handle.get_list_of_thumbnail_IDs();
		if (!thumbIds.size()) {
			return WINCODEC_ERR_CODECNOTHUMBNAIL;
		}

		heif::ImageHandle thumbHandle = handle.get_thumbnail(thumbIds[0]);
		if (thumbHandle.empty() || !HeicThumbnailGeometry::Compatible(
			handle.get_width(),handle.get_height(),
			thumbHandle.get_width(),thumbHandle.get_height())) {
			WicHeicDiagnostic::Emit("ThumbnailOrientationMismatch");
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

		CHeicBitmapFrameDecode* decoder = new(std::nothrow) CHeicBitmapFrameDecode(this, thumbHandle);
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

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::GetFrameCount(__RPC__out UINT *pCount)
{
	DbgLog("%s", __FUNCTION__);

	if (!pCount) {
		return E_POINTER;
	}

	DbgLog("GetFrameCount %d %d",
		m_Context.get_number_of_top_level_images(),
		m_Context.get_list_of_top_level_image_IDs().size());


	*pCount = static_cast<UINT>(m_Context.get_number_of_top_level_images());
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapDecoder::GetFrame(UINT index, __RPC__deref_out_opt IWICBitmapFrameDecode **ppIBitmapFrame)
{
	WicHeicDiagnostic::Emit("DecoderGetFrame",index,0);
	if (!ppIBitmapFrame) return E_POINTER;
	*ppIBitmapFrame = nullptr;
	try
	{
		DbgLog("GetFrame: %d", index);

		if (index >= (UINT)m_Context.get_number_of_top_level_images()) {
			return WINCODEC_ERR_FRAMEMISSING;
		}

		std::vector<heif_item_id> ids = m_Context.get_list_of_top_level_image_IDs();

		heif::ImageHandle handle = m_Context.get_image_handle(ids[index]);
		if (handle.empty()) {
			return WINCODEC_ERR_FRAMEMISSING;
		}

		DbgLog("GetFrame@handle: hasAlpha=%d, chromaBpp=%d, lumaBpp=%d, width=%d, height=%d, primary=%d",
			handle.has_alpha_channel(),
			handle.get_chroma_bits_per_pixel(),
			handle.get_luma_bits_per_pixel(),
			handle.get_width(),
			handle.get_height(),
			handle.is_primary_image()
		);

		CHeicBitmapFrameDecode* decoder = new(std::nothrow) CHeicBitmapFrameDecode(this, handle);
		if (!decoder) {
			return E_OUTOFMEMORY;
		}

		*ppIBitmapFrame = decoder;
	}
	catch (const heif::Error& ex)
	{
		Log("GetFrame@Exception: %s", ex.get_message().c_str());
		return E_INVALIDARG;
	}
	catch (const std::exception& ex)
	{
		Log("GetFrame@Exception: %s", ex.what());
		return E_INVALIDARG;
	}
	catch (...)
	{
		Log("GetFrame@Exception: Unknown");
		return E_INVALIDARG;
	}
	return S_OK;
}


HRESULT CHeicBitmapDecoder::CopyOriginalBytes(std::vector<uint8_t>& content)
{
    if(!m_Reader)return WINCODEC_ERR_WRONGSTATE;
    return m_Reader->CopyOriginalBytes(content);
}
