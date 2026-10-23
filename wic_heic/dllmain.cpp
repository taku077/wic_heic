// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "wic_heic.h"
#include "ComFactory.h"
#include "ComCounter.h"
#include "UtlReg.h"
#include "HeicException.h"

#if defined(X86)
#pragma comment (linker, "/export:DllCanUnloadNow=_DllCanUnloadNow@0,PRIVATE")
#pragma comment (linker, "/export:DllGetClassObject=_DllGetClassObject@12,PRIVATE")
#pragma comment (linker, "/export:DllRegisterServer=_DllRegisterServer@0,PRIVATE")
#pragma comment (linker, "/export:DllUnregisterServer=_DllUnregisterServer@0,PRIVATE")
#else
#pragma comment (linker, "/export:DllCanUnloadNow,PRIVATE")
#pragma comment (linker, "/export:DllGetClassObject,PRIVATE")
#pragma comment (linker, "/export:DllRegisterServer,PRIVATE")
#pragma comment (linker, "/export:DllUnregisterServer,PRIVATE")
#endif

HMODULE GModule = NULL;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		GModule = hModule;
		DisableThreadLibraryCalls(hModule);
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}


HRESULT RegisterDecoder(const std::wstring& dllPath)
{
	try
	{
		UtlReg regInstance;
		UtlReg regDecoder;

		regInstance.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\{7ED96837-96F0-4812-B211-F13C24117ED3}\\Instance\\" S_CLSID_HEICDecoder);
		regInstance.CreateValue("CLSID", S_CLSID_HEICDecoder);
		regInstance.CreateValue("FriendlyName", "HEIC Decoder");

		regDecoder.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICDecoder);
		regDecoder.CreateValue("ArbitrationPriority", 0);
		regDecoder.CreateValue("SupportAnimation", 0);
		regDecoder.CreateValue("SupportChromaKey", 0);
		regDecoder.CreateValue("SupportLossless", 0);
		regDecoder.CreateValue("SupportMultiframe", 1);
		regDecoder.CreateValue("Author", "prsyahmi@github");
		regDecoder.CreateValue("ContainerFormat", S_GUID_ContainerFormatHEIC);
		regDecoder.CreateValue("Description", "HEIC Image Decoder");
		regDecoder.CreateValue("FriendlyName", "HEIC Image Decoder");
		regDecoder.CreateValue("FileExtensions", ".heic");
		regDecoder.CreateValue("MimeTypes", "image/heic");
		regDecoder.CreateValue("SpecVersion", "1.0.0.0");
		regDecoder.CreateValue("Vendor", S_GUID_VendorPrsyahmi);
		regDecoder.CreateValue("Version", "1.0.0.0");

		regDecoder.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICDecoder "\\InprocServer32");
		regDecoder.CreateValue(L"", dllPath.c_str());
		regDecoder.CreateValue("ThreadingModel", "Both");

		// GUID_WICPixelFormat24bppRGB
		regDecoder.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICDecoder "\\Formats\\{6fddc324-4e03-4bfe-b185-3d77768dc90d}");
		// These are formats our decoder can actually return from
		// IWICBitmapFrameDecode::GetPixelFormat for 8-bit alpha and
		// 10/12/16-bit HEIC. Without registration, WIC component-info
		// clients only see 24bpp RGB and may reject legitimate images.
		// GUID values correspond to WinSDK wincodec.h.
		regDecoder.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICDecoder "\\Formats\\{f5c7ad2d-6a8d-43dd-a7a8-a29935261ae9}"); // 32bppRGBA
		regDecoder.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICDecoder "\\Formats\\{6fddc324-4e03-4bfe-b185-3d77768dc915}"); // 48bppRGB
		regDecoder.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICDecoder "\\Formats\\{6fddc324-4e03-4bfe-b185-3d77768dc916}"); // 64bppRGBA


		struct TPattern {
			char* Mask;
			char* Pattern;
			DWORD Size;
			DWORD Offset;
		};

		TPattern patterns[] = {
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftypheic", // Regular image
				8,
				4
			},
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftypheix", // Extended range >8 bit
				8,
				4
			},
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftyphevc", // Image sequence
				8,
				4
			},
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftypheim", // Image sequence
				8,
				4
			},
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftypheis", // Scalable image
				8,
				4
			},
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftyphevm", // Multiview sequence
				8,
				4
			},
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftyphevs", // Scalable sequence
				8,
				4
			},
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftypmif1", // Nokia alpha image
				8,
				4
			},
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftypmsf1", // Nokia animation image
				8,
				4
			},
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftypmsf1", // Nokia animation image
				8,
				4
			},
			{
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				"ftypavif", // AV1 image
				8,
				4
			},
		};

		for (size_t i = 0; i < _countof(patterns); i++) {
			std::string sKey = "CLSID\\" S_CLSID_HEICDecoder "\\Patterns\\";
			sKey += std::to_string(i);

			regDecoder.CreateKeyTree(HKEY_CLASSES_ROOT, sKey);
			regDecoder.CreateValue("Mask", patterns[i].Mask, patterns[i].Size);
			regDecoder.CreateValue("Pattern", patterns[i].Pattern, patterns[i].Size);
			regDecoder.CreateValue("Length", patterns[i].Size);
			regDecoder.CreateValue("Position", patterns[i].Offset);
		}

	}
	catch (const HeicException& ex)
	{
		Log("Unable to register dll: %s", ex.what());
		return ex.GetResult();
	}
	catch (const std::exception& ex)
	{
		Log("Unable to register dll: %s", ex.what());
		return S_FALSE;
	}

	return S_OK;
}

HRESULT RegisterEncoder(const std::wstring& dllPath)
{
	try
	{
		UtlReg regInstance;
		UtlReg regEncoder;

		regInstance.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\{AC757296-3522-4E11-9862-C17BE5A1767E}\\Instance\\" S_CLSID_HEICEncoder);
		regInstance.CreateValue("CLSID", S_CLSID_HEICEncoder);
		regInstance.CreateValue("FriendlyName", "HEIC Encoder");

		regEncoder.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICEncoder);
		regEncoder.CreateValue("ArbitrationPriority", 0);
		regEncoder.CreateValue("SupportAnimation", 0);
		regEncoder.CreateValue("SupportChromaKey", 0);
		regEncoder.CreateValue("SupportLossless", 0);
		regEncoder.CreateValue("SupportMultiframe", 1);
		regEncoder.CreateValue("Author", "prsyahmi@github");
		regEncoder.CreateValue("ContainerFormat", S_GUID_ContainerFormatHEIC);
		regEncoder.CreateValue("Description", "HEIC Image Encoder");
		regEncoder.CreateValue("FriendlyName", "HEIC Image Encoder");
		regEncoder.CreateValue("FileExtensions", ".heic");
		regEncoder.CreateValue("MimeTypes", "image/heic");
		regEncoder.CreateValue("SpecVersion", "1.0.0.0");
		regEncoder.CreateValue("Vendor", S_GUID_VendorPrsyahmi);
		regEncoder.CreateValue("Version", "1.0.0.0");

		regEncoder.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICEncoder "\\InprocServer32");
		regEncoder.CreateValue(L"", dllPath.c_str());
		regEncoder.CreateValue("ThreadingModel", "Both");

		// GUID_WICPixelFormat24bppRGB
		regEncoder.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICEncoder "\\Formats\\{6fddc324-4e03-4bfe-b185-3d77768dc90d}");
		// Synthetic 8-bit RGBA HEIC WritePixels -> Commit -> WIC decode
		// retains the alpha channel. Advertise this tested format only.
		// Do NOT advertise 48/64bpp here: native 10-bit HEVC recompression
		// is not verified for the pinned encoder backend.
		regEncoder.CreateKeyTree(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICEncoder "\\Formats\\{f5c7ad2d-6a8d-43dd-a7a8-a29935261ae9}"); // 32bppRGBA


	}
	catch (const HeicException& ex)
	{
		Log("Unable to register dll: %s", ex.what());
		return ex.GetResult();
	}
	catch (const std::exception& ex)
	{
		Log("Unable to register dll: %s", ex.what());
		return S_FALSE;
	}

	return S_OK;
}

HRESULT RegisterFiletype(const std::wstring& dllPath)
{
	try
	{
		UtlReg regInstance;
		UtlReg regDecoder;

		// Windows Photo Gallery and Windows Explorer
		regInstance.CreateKeyTree(HKEY_CLASSES_ROOT, ".heic");
		regInstance.CreateValue("ContentType", "image/heic");
		regInstance.CreateValue("PerceivedType", "image");

		regInstance.CreateKeyTree(HKEY_CLASSES_ROOT, ".heics");
		regInstance.CreateValue("ContentType", "image/heic-sequence");
		regInstance.CreateValue("PerceivedType", "image");

		regInstance.CreateKeyTree(HKEY_CLASSES_ROOT, ".heif");
		regInstance.CreateValue("ContentType", "image/heif");
		regInstance.CreateValue("PerceivedType", "image");

		regInstance.CreateKeyTree(HKEY_CLASSES_ROOT, ".heifs");
		regInstance.CreateValue("ContentType", "image/heif-sequence");
		regInstance.CreateValue("PerceivedType", "image");

		regDecoder.CreateKeyTree(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap");
		regDecoder.CreateValue(".heic", "picture");
		regDecoder.CreateValue(".heics", "picture");
		regDecoder.CreateValue(".heif", "picture");
		regDecoder.CreateValue(".heifs", "picture");

		// Integration with the Windows Thumbnail Cache
		regInstance.CreateKeyTree(HKEY_CLASSES_ROOT, "SystemFileAssociations\\.heic\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");
		regInstance.CreateValue("", "{C7657C4A-9F68-40fa-A4DF-96BC08EB3551}");
		regInstance.CreateKeyTree(HKEY_CLASSES_ROOT, "SystemFileAssociations\\.heics\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");
		regInstance.CreateValue("", "{C7657C4A-9F68-40fa-A4DF-96BC08EB3551}");
		regInstance.CreateKeyTree(HKEY_CLASSES_ROOT, "SystemFileAssociations\\.heif\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");
		regInstance.CreateValue("", "{C7657C4A-9F68-40fa-A4DF-96BC08EB3551}");
		regInstance.CreateKeyTree(HKEY_CLASSES_ROOT, "SystemFileAssociations\\.heifs\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");
		regInstance.CreateValue("", "{C7657C4A-9F68-40fa-A4DF-96BC08EB3551}");
	}
	catch (const HeicException& ex)
	{
		Log("Unable to register dll: %s", ex.what());
		return ex.GetResult();
	}
	catch (const std::exception& ex)
	{
		Log("Unable to register dll: %s", ex.what());
		return S_FALSE;
	}

	return S_OK;
}

STDAPI DllRegisterServer()
{
	// Requires Admin rights

	std::wstring dllPath;

	DWORD copied = 0;
	do {
		dllPath.resize(dllPath.size() + MAX_PATH);
		copied = GetModuleFileName(GModule, &dllPath[0], (DWORD)dllPath.size());
	} while (copied >= dllPath.size());
	dllPath.resize(copied);

	HRESULT hr;
	
	hr = RegisterDecoder(dllPath);
	if (hr != S_OK) {
		return hr;
	}

	hr = RegisterEncoder(dllPath);
	if (hr != S_OK) {
		return hr;
	}

	hr = RegisterFiletype(dllPath);
	if (hr != S_OK) {
		return hr;
	}

	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	return S_OK;
}

STDAPI DllUnregisterServer()
{
	RegDelnodeRecurse(HKEY_CLASSES_ROOT, "CLSID\\{7ED96837-96F0-4812-B211-F13C24117ED3}\\Instance\\" S_CLSID_HEICDecoder);
	RegDelnodeRecurse(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICDecoder);
	RegDelnodeRecurse(HKEY_CLASSES_ROOT, "CLSID\\{AC757296-3522-4E11-9862-C17BE5A1767E}\\Instance\\" S_CLSID_HEICEncoder);
	RegDelnodeRecurse(HKEY_CLASSES_ROOT, "CLSID\\" S_CLSID_HEICEncoder);

	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	return S_OK;
}

_Check_return_
STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID* ppvObj)
{
	if (!ppvObj) return E_POINTER;
	*ppvObj = nullptr;
	if (!IsEqualGUID(riid,IID_IClassFactory) &&
	    !IsEqualGUID(riid,IID_IUnknown)) return E_NOINTERFACE;
	const bool isDecoder=IsEqualCLSID(rclsid,CLSID_HEICDecoder);
	if (!isDecoder && !IsEqualCLSID(rclsid,CLSID_HEICEncoder))
		return CLASS_E_CLASSNOTAVAILABLE;
	CComFactory* factory=new(std::nothrow) CComFactory(isDecoder);
	if (!factory) return E_OUTOFMEMORY;
	const HRESULT hr=factory->QueryInterface(riid,ppvObj);
	factory->Release();
	// Never report S_OK when requested COM interface activation failed.
	return hr;
}

__control_entrypoint(DllExport)
STDAPI DllCanUnloadNow(void)
{
	CComCounter& counter = CComCounter::GetInstance();
	if (counter.m_ObjLock == 0 && counter.m_ServerLock == 0) {
		return S_OK;
	}

	return S_FALSE;
}
