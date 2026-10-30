#pragma once
#include <vector>
#include <mutex>
#include "WicHeifDecodeContext.h"

class CHeicBitmapDecoder : public IWICBitmapDecoder
{
private:
	std::atomic<uint32_t> m_Count;
	CWicHeifDecodeContext m_Context;
	CHeifStreamReader* m_Reader;
    // HEIF context is immutable after Initialize; reuse stable top-level
    // ids instead of allocating/enumerating the same list on every GetFrame.
    std::vector<heif_item_id> m_TopLevelIds;
    // Only small libheif image-handle references are retained. The decoded
    // RGB planes remain owned by short-lived WIC frame objects, never cached
    // here. Lazy initialization avoids eagerly parsing auxiliary frames.
    std::vector<heif::ImageHandle> m_TopLevelHandles;
    std::mutex m_TopLevelHandleMutex;
    // GetFrame may run concurrently in Photos' MTA; the bridge is offered once.
    std::atomic<bool> m_OfferedRotationSource{false};

public:
	CHeicBitmapDecoder();
	virtual ~CHeicBitmapDecoder();

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
	virtual ULONG STDMETHODCALLTYPE AddRef(void);
	virtual ULONG STDMETHODCALLTYPE Release(void);
	virtual HRESULT STDMETHODCALLTYPE QueryCapability(__RPC__in_opt IStream *pIStream, __RPC__out DWORD *pdwCapability);
	virtual HRESULT STDMETHODCALLTYPE Initialize(__RPC__in_opt IStream *pIStream, WICDecodeOptions cacheOptions);
	virtual HRESULT STDMETHODCALLTYPE GetContainerFormat(__RPC__out GUID *pguidContainerFormat);
	virtual HRESULT STDMETHODCALLTYPE GetDecoderInfo(__RPC__deref_out_opt IWICBitmapDecoderInfo **ppIDecoderInfo);
	virtual HRESULT STDMETHODCALLTYPE CopyPalette(__RPC__in_opt IWICPalette *pIPalette);
	virtual HRESULT STDMETHODCALLTYPE GetMetadataQueryReader(__RPC__deref_out_opt IWICMetadataQueryReader **ppIMetadataQueryReader);
	virtual HRESULT STDMETHODCALLTYPE GetPreview(__RPC__deref_out_opt IWICBitmapSource **ppIBitmapSource);
	virtual HRESULT STDMETHODCALLTYPE GetColorContexts(UINT cCount, __RPC__inout_ecount_full_opt(cCount) IWICColorContext **ppIColorContexts, __RPC__out UINT *pcActualCount);
	virtual HRESULT STDMETHODCALLTYPE GetThumbnail(__RPC__deref_out_opt IWICBitmapSource **ppIThumbnail);
	virtual HRESULT STDMETHODCALLTYPE GetFrameCount(__RPC__out UINT *pCount);
	virtual HRESULT STDMETHODCALLTYPE GetFrame(UINT index, __RPC__deref_out_opt IWICBitmapFrameDecode **ppIBitmapFrame);
	HRESULT CopyOriginalBytes(std::vector<uint8_t>& content);
};

