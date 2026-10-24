#pragma once
#include "HeicSourceAccess.h"
#include <mutex>

class CHeicBitmapFrameDecode : public IWICBitmapFrameDecode, public IWICMetadataBlockReader, public IHeicRawSource
{
private:
	std::atomic<uint32_t> m_Count;
	CHeicBitmapDecoder* m_pDecoder;
	heif::ImageHandle m_Handle;
	heif::Image m_Image;
	heif_chroma m_chroma;
	WICPixelFormatGUID m_PixelFormat;
	int m_Bpp;
	int m_SourceBits;
	size_t m_Stride;
	uint8_t* m_PlaneInterleaved;
	bool m_Decoded;
	// Serialize only the first HEVC decode of THIS frame, not different HEIC files.
	// Subsequent CopyPixels readers see immutable decoded data concurrently.
	std::mutex m_DecodeMutex;

public:
	CHeicBitmapFrameDecode(CHeicBitmapDecoder* decoder, heif::ImageHandle handle);
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
	// Frame metadata blocks are part of this same COM object, not a second
	// object with a different IUnknown identity.
	HRESULT STDMETHODCALLTYPE GetContainerFormat(GUID* pguidContainerFormat) override;
	HRESULT STDMETHODCALLTYPE GetCount(UINT* pcCount) override;
	HRESULT STDMETHODCALLTYPE GetReaderByIndex(UINT nIndex, IWICMetadataReader** ppIMetadataReader) override;
	HRESULT STDMETHODCALLTYPE GetEnumerator(IEnumUnknown** ppIEnumMetadata) override;


	void DecodeImage();
	HRESULT STDMETHODCALLTYPE ReadFullContent(std::vector<uint8_t>* content) override;
};

