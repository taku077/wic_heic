#pragma once

class CComFactory : public IClassFactory
{
private:
	std::atomic<uint32_t> m_Count;
	const bool m_IsDecoder;

public:
	explicit CComFactory(bool isDecoder);
	virtual ~CComFactory();

	virtual HRESULT STDMETHODCALLTYPE CreateInstance(_In_opt_ IUnknown *pUnkOuter, _In_ REFIID riid, _COM_Outptr_ void **ppvObject);
	virtual HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock);
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
	virtual ULONG STDMETHODCALLTYPE AddRef(void);
	virtual ULONG STDMETHODCALLTYPE Release(void);
};

