#include "stdafx.h"
#include "wic_heic.h"
#include "HeicBitmapFrameEncode.h"
#include "HeicBitmapEncoder.h"
#include "HeicFrameOptions.h"
#include "HeifStreamWriter.h"
#include "PhotosWicTrace.h"
#include "HeicColorProfileWic.h"


CHeicBitmapEncoder::CHeicBitmapEncoder()
	: m_Count(1)
	, m_IccExplicitlySet(false)
	, m_Committed(false)
	, m_OutputAttempted(false)
	, m_Stream(nullptr)
{
}


CHeicBitmapEncoder::~CHeicBitmapEncoder()
{
	for (auto it = m_Frames.begin(); it != m_Frames.end(); ++it) {
		(*it)->Release();
	}

	if (m_Stream) {
		m_Stream->Release();
	}
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::QueryInterface(REFIID riid, void **ppvObject)
{
	HRESULT hr = S_OK;

	if (!ppvObject) {
		return E_POINTER;
	}

	*ppvObject = nullptr;
	if (IsEqualGUID(riid, IID_IUnknown)) {
		*ppvObject = static_cast<IUnknown*>(this);
	} else if (IsEqualGUID(riid, IID_IWICBitmapEncoder)) {
		*ppvObject = static_cast<IWICBitmapEncoder*>(this);
	} else {
		hr = E_NOINTERFACE;
	}

	if (SUCCEEDED(hr)) {
		AddRef();
	}

	return hr;
}

ULONG STDMETHODCALLTYPE CHeicBitmapEncoder::AddRef(void)
{
	return ++m_Count;
}

ULONG STDMETHODCALLTYPE CHeicBitmapEncoder::Release(void)
{
	uint32_t n = --m_Count;
	if (m_Count == 0) {
		delete this;
	}

	return n;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::Initialize(__RPC__in_opt IStream *pIStream, WICBitmapEncoderCacheOption cacheOption)
{
	WicHeicDiagnostic::Emit("EncoderInitialize");
	if (!pIStream) return E_INVALIDARG;
	if (m_Stream || m_Committed) return WINCODEC_ERR_WRONGSTATE;
	m_Stream = pIStream;
	m_Stream->AddRef();

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::GetContainerFormat(__RPC__out GUID *pguidContainerFormat)
{
	if (!pguidContainerFormat) {
		return E_POINTER;
	}

	*pguidContainerFormat = GUID_ContainerFormatHEIC;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::GetEncoderInfo(__RPC__deref_out_opt IWICBitmapEncoderInfo **ppIEncoderInfo)
{
	DbgLog("CHeicBitmapEncoder::GetEncoderInfo()");

	if (!ppIEncoderInfo) {
		return E_POINTER;
	}

	HRESULT hr = S_OK;
	IWICImagingFactory* pFactory = nullptr;
	IWICComponentInfo* pCompInfo = nullptr;

	hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));

	if (SUCCEEDED(hr)) {
		hr = pFactory->CreateComponentInfo(CLSID_HEICEncoder, &pCompInfo);
	}

	if (SUCCEEDED(hr)) {
		hr = pCompInfo->QueryInterface(IID_IWICBitmapEncoderInfo, (void**)ppIEncoderInfo);
	}

	if (pFactory) pFactory->Release();
	if (pCompInfo) pCompInfo->Release();

	return hr;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::SetColorContexts(UINT cCount, __RPC__in_ecount_full(cCount) IWICColorContext **ppIColorContext)
{
	if (m_Committed || m_OutputAttempted) return WINCODEC_ERR_WRONGSTATE;
	std::vector<uint8_t> profile;
	HRESULT hr = HeicColorProfileWic::ReadArray(cCount, ppIColorContext, &profile);
	if (FAILED(hr)) {
		WicHeicDiagnostic::Emit("EncoderSetColorContexts",cCount,0,0,0,"icc-rejected",hr);
		return hr;
	}
	// If any frame already has pixels, changing the color space would mislabel it.
	for (auto* frame : m_Frames) {
		// A lossless frame already owns the exact original container bytes;
		// later ICC changes would otherwise be accepted but discarded.
		if (frame->m_Created || frame->m_HasPixels || frame->m_LosslessReady)
			return WINCODEC_ERR_WRONGSTATE;
	}
	m_IccProfile = std::move(profile);
	m_IccExplicitlySet = true;
	for (auto* frame : m_Frames) {
		frame->m_IccProfile = m_IccProfile;
		frame->m_IccExplicitlySet = true;
	}
	WicHeicDiagnostic::Emit("EncoderSetColorContexts",cCount,0,0,0,"icc-accepted");
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::SetPalette(__RPC__in_opt IWICPalette *pIPalette)
{
	return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::SetThumbnail(__RPC__in_opt IWICBitmapSource *pIThumbnail)
{
	WicHeicDiagnostic::Emit("EncoderSetThumbnail",0,0,0,0,pIThumbnail?"thumbnail-rejected":"none",WINCODEC_ERR_UNSUPPORTEDOPERATION);
	return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::SetPreview(__RPC__in_opt IWICBitmapSource *pIPreview)
{
	WicHeicDiagnostic::Emit("EncoderSetPreview",0,0,0,0,pIPreview?"preview-rejected":"none",WINCODEC_ERR_UNSUPPORTEDOPERATION);
	return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::CreateNewFrame(__RPC__deref_out_opt IWICBitmapFrameEncode **ppIFrameEncode, __RPC__deref_opt_inout_opt IPropertyBag2 **ppIEncoderOptions)
{
	try
	{
		if (!ppIFrameEncode) return E_POINTER;
		*ppIFrameEncode=nullptr;
		if (ppIEncoderOptions) *ppIEncoderOptions=nullptr;
		if (!m_Stream || m_Committed || m_OutputAttempted) return WINCODEC_ERR_WRONGSTATE;
		// WIC requires finishing one frame before starting the next.
		// This is essential with NoCache, which cannot buffer overlapping
		// partially-defined images safely.
		if (!m_Frames.empty() && !m_Frames.back()->m_Committed)
			return WINCODEC_ERR_WRONGSTATE;
		WicHeicDiagnostic::Emit("CreateNewFrame",0,0,0,0,ppIEncoderOptions?"bag-requested":"no-bag");

		CHeicFrameOptions* opts=nullptr;
		if (ppIEncoderOptions) {
			opts=new(std::nothrow) CHeicFrameOptions();
			if(!opts)return E_OUTOFMEMORY;
		}
		CHeicBitmapFrameEncode* pFrame=new(std::nothrow) CHeicBitmapFrameEncode();
		if(!pFrame) {
			if(opts)opts->Release();
			return E_OUTOFMEMORY;
		}
		try {
			pFrame->m_IccProfile = m_IccProfile;
			pFrame->m_IccExplicitlySet = m_IccExplicitlySet;
			m_Frames.push_back(pFrame); // encoder keeps initial reference
		} catch (...) {
			pFrame->Release();
			if(opts)opts->Release();
			throw;
		}
		pFrame->AddRef(); // caller reference
		*ppIFrameEncode=pFrame;
		if(ppIEncoderOptions) *ppIEncoderOptions=opts;
	}
	catch (const heif::Error& ex)
	{
		Log("Call to CHeicBitmapEncoder::CreateNewFrame failed: %s", ex.get_message().c_str());
		return WINCODEC_ERR_GENERIC_ERROR;
	}
	catch (const std::exception& ex)
	{
		Log("Call to CHeicBitmapEncoder::CreateNewFrame failed: %s", ex.what());
		return WINCODEC_ERR_GENERIC_ERROR;
	}
	catch (...)
	{
		Log("Call to CHeicBitmapEncoder::CreateNewFrame failed: Unknown exception");
		return WINCODEC_ERR_GENERIC_ERROR;
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::Commit(void)
{
	WicHeicDiagnostic::Emit("EncoderCommitEnter",0,0,0,0,m_Frames.size()==1?"one-frame":"many-frames");
	// HEVC encoding writes through CHeifStreamWriter. If the destination
	// IStream accepts only a prefix (even with HRESULT S_OK), libheif throws
	// after bytes have already reached the destination. In that case a
	// caller-visible error must not leave a corrupt HEIC prefix behind when
	// the initially-empty stream supports truncation.
	auto rollbackRasterPartial = [this]() {
		if (!m_Stream) return;
		STATSTG st{};
		if (FAILED(m_Stream->Stat(&st,STATFLAG_NONAME)) ||
		    st.cbSize.QuadPart==0) return;
		ULARGE_INTEGER zero{};
		const HRESULT reset=m_Stream->SetSize(zero);
		if (SUCCEEDED(reset)) {
			LARGE_INTEGER begin{};
			m_Stream->Seek(begin,STREAM_SEEK_SET,nullptr);
		}
		WicHeicDiagnostic::Emit("EncoderRasterPartialWriteRollback",0,0,0,0,
			SUCCEEDED(reset)?"empty-stream-restored":"stream-rollback-unavailable",
			reset);
	};
	try
	{
		if (m_Committed || m_OutputAttempted) return WINCODEC_ERR_WRONGSTATE;
		// Reject existing output for both lossless and raster encodes.
		// This does not undo truncation by the caller before Initialize.
		if (!m_Stream) return WINCODEC_ERR_NOTINITIALIZED;
		STATSTG stat{};
		HRESULT hr=m_Stream->Stat(&stat,STATFLAG_NONAME);
		if (FAILED(hr) || stat.cbSize.QuadPart!=0) {
			WicHeicDiagnostic::Emit("EncoderCommitRejected",0,0,0,0,"nonempty-target",WINCODEC_ERR_UNSUPPORTEDOPERATION);
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}
		LARGE_INTEGER zero{};
		ULARGE_INTEGER pos{};
		hr=m_Stream->Seek(zero,STREAM_SEEK_CUR,&pos);
		if (FAILED(hr) || pos.QuadPart!=0) {
			WicHeicDiagnostic::Emit("EncoderCommitRejected",0,0,0,0,"nonzero-position",WINCODEC_ERR_UNSUPPORTEDOPERATION);
			return WINCODEC_ERR_UNSUPPORTEDOPERATION;
		}
		if (m_Frames.empty()) return WINCODEC_ERR_WRONGSTATE;
		// WIC requires every frame.Commit() before encoder.Commit().
		// Previously complete raster rows were enough to serialize an image
		// even when the caller never finalized the frame. Do not emit a single
		// byte or invoke the HEVC encoder in this invalid lifecycle state.
		for (auto* frame : m_Frames) {
			if (!frame->m_Committed) {
				WicHeicDiagnostic::Emit("EncoderCommitRejected",0,0,0,0,
					"frame-not-committed",WINCODEC_ERR_WRONGSTATE);
				return WINCODEC_ERR_WRONGSTATE;
			}
		}

		// A single-frame direct WIC rotation/copy can now retain the entire
		// original HEIF file, including metadata, thumbnails and HDR assets.
		// A pre-existing nonempty target stream is NEVER overwritten here.
		if(m_Frames.size()==1 && m_Frames[0]->m_LosslessReady) {
			const auto& bytes=m_Frames[0]->m_LosslessBytes;
			if(bytes.empty()||bytes.size()>0xFFFFFFFFULL)
				return WINCODEC_ERR_UNSUPPORTEDOPERATION;
			ULONG written=0;
			m_OutputAttempted=true;
			hr=m_Stream->Write(bytes.data(),static_cast<ULONG>(bytes.size()),&written);
			if(FAILED(hr)||written!=bytes.size()) {
				// Even a short S_OK write leaves a corrupt HEIC prefix in
				// an initially empty output stream. Roll it back where the
				// caller's IStream implements SetSize. Never report success
				// for an incomplete image, and do not touch input images.
				ULARGE_INTEGER empty{};
				const HRESULT reset=m_Stream->SetSize(empty);
				if(SUCCEEDED(reset)) {
					LARGE_INTEGER begin{};
					m_Stream->Seek(begin,STREAM_SEEK_SET,nullptr);
				}
				WicHeicDiagnostic::Emit("EncoderCommitPartialWriteRollback",
					m_Frames[0]->m_Width,m_Frames[0]->m_Height,0,0,
					SUCCEEDED(reset)?"empty-stream-restored":"stream-rollback-unavailable",
					STG_E_WRITEFAULT);
				return STG_E_WRITEFAULT;
			}
			m_Committed = true;
			WicHeicDiagnostic::Emit("EncoderCommitLossless",m_Frames[0]->m_Width,m_Frames[0]->m_Height,m_Frames[0]->m_BitmapTransform,m_Frames[0]->m_HeifCompressionMethod,"heif-copied");
			return S_OK;
		}
		for(auto* frame:m_Frames){
			if(frame->m_LosslessReady || !frame->m_HasPixels ||
			   frame->m_WrittenLines!=frame->m_Height)
				return WINCODEC_ERR_WRONGSTATE;
		}
		// Failure after native encoding begins may leave m_Context mutated.
		// Reject repeated Commit attempts even if stream rollback succeeded.
		m_OutputAttempted=true;
		heif::Encoder encoder(heif_compression_HEVC);
		CHeifStreamWriter writer(m_Stream);

		if (!m_Stream) {
			return WINCODEC_ERR_NOTINITIALIZED;
		}

		for (auto it = m_Frames.begin(); it != m_Frames.end(); ++it) {
			m_Context.encode_image((*it)->m_Frame, encoder);
		}

		m_Context.write(writer);
		m_Committed = true;
		WicHeicDiagnostic::Emit("EncoderCommitRecompressed",0,0,0,0,"hevc-encoded");
	}
	catch (const heif::Error& ex)
	{
		rollbackRasterPartial();
		Log("Call to CHeicBitmapEncoder::Commit failed: %s", ex.get_message().c_str());
		return WINCODEC_ERR_INVALIDPARAMETER;
	}
	catch (const std::exception& ex)
	{
		rollbackRasterPartial();
		Log("Call to CHeicBitmapEncoder::Commit failed: %s", ex.what());
		return WINCODEC_ERR_GENERIC_ERROR;
	}
	catch (...)
	{
		rollbackRasterPartial();
		Log("Call to CHeicBitmapEncoder::Commit failed: Unknown exception");
		return WINCODEC_ERR_GENERIC_ERROR;
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CHeicBitmapEncoder::GetMetadataQueryWriter(__RPC__deref_out_opt IWICMetadataQueryWriter **ppIMetadataQueryWriter)
{
	if (ppIMetadataQueryWriter) *ppIMetadataQueryWriter=nullptr;
	WicHeicDiagnostic::Emit("EncoderMetadataWriterUnavailable",0,0,0,0,"metadata-rejected",WINCODEC_ERR_UNSUPPORTEDOPERATION);
	return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}
