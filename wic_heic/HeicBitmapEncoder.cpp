#include "stdafx.h"
#include "wic_heic.h"
#include "HeicBitmapFrameEncode.h"
#include "HeicBitmapEncoder.h"
#include "HeicFrameOptions.h"
#include "HeifStreamWriter.h"
#include "PhotosWicTrace.h"
#include "PhotosPureRotationBridge.h"


CHeicBitmapEncoder::CHeicBitmapEncoder()
	: m_Count(1)
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
	if (m_Stream) return WINCODEC_ERR_WRONGSTATE;
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
	WicHeicDiagnostic::Emit("EncoderSetColorContexts",cCount,0,0,0,"metadata-rejected",WINCODEC_ERR_UNSUPPORTEDOPERATION);
	return WINCODEC_ERR_UNSUPPORTEDOPERATION;
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
		WicHeicDiagnostic::Emit("CreateNewFrame",0,0,0,0,ppIEncoderOptions?"bag-requested":"no-bag");
		*ppIFrameEncode=nullptr;
		if (ppIEncoderOptions) *ppIEncoderOptions=nullptr;

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
	try
	{
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
        // Windows Photos 10 has been observed to pass an opaque raster and
        // default (transform=0, compression=0) options for pure CW90 rotations.
        // Reuse original HEIF ONLY if the in-process source is unambiguous,
        // all rotated RGB bytes match exactly one angle, ICC is not changed, and the
        // native HEIF metadata-only transformer accepts this file structure.
        // This path is one-shot; non-rotation edits fall through unchanged.
        if(m_Frames.size()==1) {
            auto* f=m_Frames[0];
            if(f->m_OptionsInitialized && !f->m_LosslessReady &&
               f->m_HasPixels && f->m_WrittenLines==f->m_Height &&
               f->m_BitmapTransform==0 && f->m_HeifCompressionMethod==0 &&
               IsEqualGUID(f->m_PixelFormat,GUID_WICPixelFormat24bppRGB)) {
                HeicLosslessNative::Bytes matched;
                if(PhotosPureRotationBridge::TryRotation(
                        f->m_Frame,f->m_Width,f->m_Height,
                        f->m_RasterICC,matched)) {
                    f->m_LosslessBytes.swap(matched);
                    f->m_LosslessReady=true;
                }
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
			hr=m_Stream->Write(bytes.data(),static_cast<ULONG>(bytes.size()),&written);
			if(FAILED(hr)||written!=bytes.size())return STG_E_WRITEFAULT;
			WicHeicDiagnostic::Emit("EncoderCommitLossless",m_Frames[0]->m_Width,m_Frames[0]->m_Height,m_Frames[0]->m_BitmapTransform,m_Frames[0]->m_HeifCompressionMethod,"heif-copied");
			return S_OK;
		}
		for(auto* frame:m_Frames){
			if(frame->m_LosslessReady || !frame->m_HasPixels ||
			   frame->m_WrittenLines!=frame->m_Height)
				return WINCODEC_ERR_WRONGSTATE;
		}
		heif::Encoder encoder(heif_compression_HEVC);
		// Photos can hand an already-rotated WIC raster to WriteSource.
		// That path MUST be re-encoded (there is no authenticated original
		// compressed frame to rotate). The codec previously left the encoder's
		// quality at its default, causing a visibly large generational loss.
		// Use explicit high-quality encoding for that unavoidable raster path.
		// This is NOT HEIF lossless rotation and cannot recover 10/12-bit input
		// already reduced by the decoder to 8-bit RGB.
		encoder.set_lossy_quality(92);
		CHeifStreamWriter writer(m_Stream);

		if (!m_Stream) {
			return WINCODEC_ERR_NOTINITIALIZED;
		}

		for (auto it = m_Frames.begin(); it != m_Frames.end(); ++it) {
			// A Photos raster edit can explicitly supply an ICC profile.
			// Carry it into the encoded HEIF rather than acknowledging and
			// discarding the color information as older builds did.
			if (!(*it)->m_RasterICC.empty()) {
				(*it)->m_Frame.set_raw_color_profile(
					heif_color_profile_type_prof,(*it)->m_RasterICC);
			}
			m_Context.encode_image((*it)->m_Frame, encoder);
		}

		m_Context.write(writer);
		WicHeicDiagnostic::Emit("EncoderCommitRecompressed",0,0,0,0,"hevc-encoded");
	}
	catch (const heif::Error& ex)
	{
		Log("Call to CHeicBitmapEncoder::Commit failed: %s", ex.get_message().c_str());
		return WINCODEC_ERR_INVALIDPARAMETER;
	}
	catch (const std::exception& ex)
	{
		Log("Call to CHeicBitmapEncoder::Commit failed: %s", ex.what());
		return WINCODEC_ERR_GENERIC_ERROR;
	}
	catch (...)
	{
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
