#pragma once
#include "HeicSourceAccess.h"
#include <mutex>

class CHeicBitmapFrameDecode : public IWICBitmapFrameDecode, public IHeicRawSource, public IHeicAuxiliaryContainerSource
{
private:
	std::atomic<uint32_t> m_Count;
	CHeicBitmapDecoder* m_pDecoder;
	heif::ImageHandle m_Handle;
	UINT m_TopLevelIndex, m_TopLevelCount;
	heif::Image m_Image;
	heif_chroma m_chroma;
	WICPixelFormatGUID m_PixelFormat;
	int m_Bpp;
	size_t m_Stride;
	uint8_t* m_PlaneInterleaved;
	// Release/acquire publishes the immutable RGB plane after first decode.
	std::atomic<bool> m_Decoded;
	// Protect one-time lazy libheif decode when Photos issues concurrent
	// CopyPixels on the SAME COM frame. The decoded RGB plane is immutable
	// after publication; lock only the first-decode path, not every row copy.
	std::mutex m_DecodeMutex;

public:
	CHeicBitmapFrameDecode(CHeicBitmapDecoder* decoder, heif::ImageHandle handle,UINT index,UINT count);
	virtual ~CHeicBitmapFrameDecode();

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
	virtual ULONG STDMETHODCALLTYPE AddRef(void);
	virtual ULONG STDMETHODCALLTYPE Release(void);
	virtual HRESULT STDMETHODCALLTYPE GetSize(__RPC__out UINT *puiWidth, __RPC__out UINT *puiHeight);
	virtual HRESULT STDMETHODCALLTYPE GetPixelFormat(__RPC__out WICPixelFormatGUID *pPixelFormat);
	virtual HRESULT STDMETHODCALLTYPE GetResolution(__RPC__out double *pDpiX, __RPC__out double *pDpiY);
	virtual HRESULT STDMETHODCALLTYPE CopyPalette(__RPC__in_opt IWICPalette *pIPalette);
	virtual HRESULT STDMETHODCALLTYPE CopyPixels(__RPC__in_opt const WICRect *prc, UINT cbStride, UINT cbBufferSize, __RPC__out_ecount_full(cbBufferSize) BYTE *pbBuffer);
	virtual HRESULT STDMETHODCALLTYPE GetMetadataQueryReader(__RPC__deref_out_opt IWICMetadataQueryReader **ppIMetadataQueryReader);
	virtual HRESULT STDMETHODCALLTYPE GetColorContexts(UINT cCount, __RPC__inout_ecount_full_opt(cCount) IWICColorContext **ppIColorContexts, __RPC__out UINT *pcActualCount);
	virtual HRESULT STDMETHODCALLTYPE GetThumbnail(__RPC__deref_out_opt IWICBitmapSource **ppIThumbnail);

	void DecodeImage();
	HRESULT STDMETHODCALLTYPE ReadFullContent(std::vector<uint8_t>* content) override;
	HRESULT STDMETHODCALLTYPE ReadContainerAndIndex(std::vector<uint8_t>* content,UINT* index,UINT* count) override;
};

