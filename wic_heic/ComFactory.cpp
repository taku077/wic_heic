#include "stdafx.h"
#include "wic_heic.h"
#include "ComFactory.h"
#include "ComCounter.h"
#include "HeifStreamReader.h"
#include "HeicBitmapFrameEncode.h"
#include "HeicBitmapDecoder.h"
#include "HeicBitmapEncoder.h"


CComFactory::CComFactory(bool isDecoder)
	: m_Count(1)
	, m_IsDecoder(isDecoder)
{
	CComCounter::GetInstance().m_ObjLock++;
}


CComFactory::~CComFactory()
{
	CComCounter::GetInstance().m_ObjLock--;
}

ULONG STDMETHODCALLTYPE CComFactory::AddRef(void)
{
	return ++m_Count;
}

ULONG STDMETHODCALLTYPE CComFactory::Release(void)
{
	uint32_t n = --m_Count;
	if (n == 0) {
		delete this;
	}

	return n;
}

HRESULT STDMETHODCALLTYPE CComFactory::QueryInterface(REFIID riid, void **ppvObject)
{
	HRESULT hr = S_OK;

	if (!ppvObject) {
		return E_POINTER;
	}

	*ppvObject = nullptr;
	if (IsEqualGUID(riid, IID_IUnknown)) {
		*ppvObject = static_cast<IUnknown*>(this);
	} else if (IsEqualGUID(riid, IID_IClassFactory)) {
		*ppvObject = static_cast<IClassFactory*>(this);
	} else {
		hr = E_NOINTERFACE;
	}

	if (SUCCEEDED(hr)) {
		AddRef();
	}

	return hr;
}

HRESULT STDMETHODCALLTYPE CComFactory::CreateInstance(_In_opt_ IUnknown *pUnkOuter, _In_ REFIID riid, _COM_Outptr_ void **ppvObject)
{
	if (!ppvObject) return E_POINTER;
	*ppvObject = nullptr;
	// The codec does not implement COM aggregation.
	if (pUnkOuter) return CLASS_E_NOAGGREGATION;
	// COM factories must honor CLSID (decoder vs encoder) independently
	// of the requested IID. A Photos caller may ask for IID_IUnknown
	// first and QueryInterface for its WIC interface later.
	if (m_IsDecoder) {
		CHeicBitmapDecoder* decoder = new(std::nothrow) CHeicBitmapDecoder();
		if (!decoder) return E_OUTOFMEMORY;
		const HRESULT hr = decoder->QueryInterface(riid, ppvObject);
		decoder->Release();
		return hr;
	}
	CHeicBitmapEncoder* encoder = new(std::nothrow) CHeicBitmapEncoder();
	if (!encoder) return E_OUTOFMEMORY;
	const HRESULT hr = encoder->QueryInterface(riid, ppvObject);
	encoder->Release();
	return hr;
}

HRESULT STDMETHODCALLTYPE CComFactory::LockServer(BOOL fLock)
{
	if (fLock) {
		CComCounter::GetInstance().m_ServerLock++;
	} else {
		CComCounter::GetInstance().m_ServerLock--;
	}

	return CoLockObjectExternal(this, fLock, TRUE);
}
