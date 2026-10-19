// wictest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "../wic_heic/ExifOrientationFix.h"
#include <vector>
#include <utility>
#include "..\wic_heic\wic_heic.h"

#pragma comment (lib, "windowscodecs.lib")


// Deterministic unit tests for the metadata rewrite, no real images required.
void CheckExifOrientationNormalization()
{
    auto makeExif = [](bool le, uint16_t orientation) {
        std::vector<uint8_t> bytes(4,0);  // libheif Exif item's offset field
        const uint8_t leBase[] = {'I','I',42,0,8,0,0,0,1,0,0x12,1,3,0,1,0,0,0,0,0,0,0,0,0,0,0,0};
        const uint8_t beBase[] = {'M','M',0,42,0,0,0,8,0,1,1,0x12,0,3,0,0,0,1,0,0,0,0,0,0,0,0};
        const uint8_t* source=le?leBase:beBase;
        bytes.insert(bytes.end(), source, source+26);
        if (le) { bytes[22]=uint8_t(orientation); bytes[23]=0; }
        else { bytes[22]=0; bytes[23]=uint8_t(orientation); }
        return bytes;
    };
    for (bool le : {true,false}) {
        for (uint16_t value : {uint16_t(6), uint16_t(8)}) {
            auto data=makeExif(le,value);
            auto original=data;
            if (WicHeicOrientation::NormalizeExifForRotatedPixels(data,false) || data!=original)
                throw std::exception("EXIF regression: unrotated metadata changed");
            if (!WicHeicOrientation::NormalizeExifForRotatedPixels(data,true))
                throw std::exception("EXIF regression: rotated case not normalized");
            if (WicHeicOrientation::Get16(data,22,le)!=1)
                throw std::exception("EXIF regression: orientation not set to 1");
            for (size_t i=0;i<data.size();++i) {
                if (i!=22 && i!=23 && data[i]!=original[i])
                    throw std::exception("EXIF regression: unrelated metadata was altered");
            }
        }
        auto normal=makeExif(le,1);
        if (WicHeicOrientation::NormalizeExifForRotatedPixels(normal,true))
            throw std::exception("EXIF regression: Orientation=1 must be preserved");
    }
    printf("EXIF orientation normalization unit tests PASSED\n");
}


// Regression fixture is synthetic (generated from repository sample 03.heic).
// HEIF irot=3 + EXIF Orientation=6, matching the user's FAIL metadata signature.
// It must decode to 1080x1920 *without* exposing a second rotation to WIC.
void CheckRotatedExifFixture()
{
    CComPtr<IWICImagingFactory> factory;
    CComPtr<IWICBitmapDecoder> decoder;
    CComPtr<IWICBitmapFrameDecode> frame;
    CComPtr<IWICMetadataQueryReader> metadata;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL,
        CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(hr)) throw std::exception("fixture: WIC imaging factory unavailable");

    hr = factory->CreateDecoderFromFilename(L"samples\\fixture-irot3-exif6.heic",
        NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        printf("fixture: CreateDecoderFromFilename HRESULT=0x%08X\n", (unsigned)hr);
        throw std::exception("fixture: HEIC could not be decoded");
    }
    hr = decoder->GetFrame(0,&frame);
    if (FAILED(hr)) throw std::exception("fixture: missing frame");
    UINT width=0,height=0;
    hr=frame->GetSize(&width,&height);
    printf("fixture: WIC size = %ux%u\n", width, height);
    if (FAILED(hr) || width!=1080 || height!=1920)
        throw std::exception("fixture: irot=3 dimensions were not applied");

    hr=frame->GetMetadataQueryReader(&metadata);
    if (FAILED(hr)) {
        printf("fixture: GetMetadataQueryReader HRESULT=0x%08X\n", (unsigned)hr);
        throw std::exception("fixture: metadata reader unavailable");
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    hr=metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}",&value);
    if (FAILED(hr)) {
        printf("fixture: Orientation query HRESULT=0x%08X\n", (unsigned)hr);
        PropVariantClear(&value);
        throw std::exception("fixture: EXIF Orientation not returned");
    }
    const unsigned orientation=(value.vt==VT_UI2)?value.uiVal:
         (value.vt==VT_UI4)?value.ulVal:9999;
    printf("fixture: EXIF Orientation returned by WIC = %u (type %u)\n",
        orientation,(unsigned)value.vt);
    PropVariantClear(&value);
    if (orientation!=1) throw std::exception("fixture: duplicate rotation still exposed");

    CComPtr<IWICBitmapSource> pixels;
    hr=frame->QueryInterface(IID_IWICBitmapSource,(void**)&pixels);
    if (FAILED(hr)) throw std::exception("fixture: IWICBitmapSource QI failed");
    std::vector<BYTE> firstRow(width*3);
    WICRect region={0,0,static_cast<INT>(width),1};
    hr=pixels->CopyPixels(&region,width*3,static_cast<UINT>(firstRow.size()),firstRow.data());
    if (FAILED(hr)) {
        printf("fixture: CopyPixels HRESULT=0x%08X\n",(unsigned)hr);
        throw std::exception("fixture: transformed pixels could not be read");
    }
    printf("fixture: ROTATED_HEIC_METADATA_AND_PIXELS_PASSED\n");
}

void ConvertToHeic(const std::wstring& srcFile, const std::wstring& destFile, bool rotate90 = false)
{
	CComPtr<IWICImagingFactory> pFactory;
	CComPtr<IWICBitmapDecoder> pDecoder;
	CComPtr<IWICBitmapEncoder> pEncoder;
	CComPtr<IWICFormatConverter> pConvertedFrame;
	CComPtr<IWICStream> pStream;
	CComPtr<IWICBitmapEncoderInfo> pEncInfo;
	UINT nFrame;

	HRESULT hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory,
		(LPVOID*)&pFactory);

	if (FAILED(hr)) {
		throw std::exception("Failed to get imaging factory");
	}

	hr = pFactory->CreateEncoder(GUID_ContainerFormatHEIC, &GUID_VendorPrsyahmi, &pEncoder);
	if (FAILED(hr)) {
		throw std::exception("Failed to CreateEncoder");
	}

	hr = pFactory->CreateStream(&pStream);
	if (FAILED(hr)) {
		throw std::exception("Failed to CreateStream");
	}

	hr = pEncoder->GetEncoderInfo(&pEncInfo);
	if (SUCCEEDED(hr)) {
		TCHAR friendlyName[64] = {};
		UINT nActual = 0;
		hr = pEncInfo->GetFriendlyName(64, friendlyName, &nActual);

		if (SUCCEEDED(hr)) {
			printf("Encoder Info: %ws\n", friendlyName);
		}
	}
	if (FAILED(hr)) {
		printf("Encoder Info: Error %x\n", hr);
	}

	hr = pFactory->CreateDecoderFromFilename(
		srcFile.c_str(),
		NULL,
		GENERIC_READ,
		WICDecodeMetadataCacheOnDemand,
		&pDecoder);

	if (FAILED(hr)) {
		throw std::exception("Failed to CreateDecoderFromFilename");
	}

	hr = pStream->InitializeFromFilename(destFile.c_str(), GENERIC_WRITE);
	if (FAILED(hr)) {
		throw std::exception("Failed to Create Stream file");
	}

	hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
	if (FAILED(hr)) {
		throw std::exception("Failed to Initialize encoder");
	}

	// Copy decoded frame

	hr = pDecoder->GetFrameCount(&nFrame);
	if (FAILED(hr)) {
		throw std::exception("Failed to GetFrameCount");
	}

	printf("Total Frames: %d\n", nFrame);
	std::vector<std::pair<UINT, UINT>> expectedDimensions;

	for (UINT i = 0; i < nFrame; i++)
	{
		CComPtr<IWICBitmapFrameDecode> pFrameDecode;
		CComPtr<IWICBitmapFrameEncode> pFrameEncode;
		CComPtr<IWICMetadataQueryReader> pFrameQReader;
		CComPtr<IWICMetadataQueryWriter> pFrameQWriter;
		UINT width, height;
		double dpiX, dpiY;
		WICPixelFormatGUID pixFormat;

		if (nFrame > 1) {
			printf("    Frame %d\n", i);
		}

		hr = pDecoder->GetFrame(i, &pFrameDecode);
		if (FAILED(hr)) {
			throw std::exception("Failed to GetFrame");
		}

		hr = pFrameDecode->GetSize(&width, &height);
		if (FAILED(hr)) {
			throw std::exception("Failed to get image size");
		}

		hr = pFrameDecode->GetResolution(&dpiX, &dpiY);
		if (FAILED(hr)) {
			throw std::exception("Failed to GetResolution");
		}

		hr = pFrameDecode->GetPixelFormat(&pixFormat);
		if (FAILED(hr)) {
			throw std::exception("Failed to GetPixelFormat");
		}

		IWICBitmapSource* sourceToEncode = static_cast<IWICBitmapSource*>(pFrameDecode.p);
		CComPtr<IWICBitmapFlipRotator> rotatedSource;
		if (rotate90) {
			hr = pFactory->CreateBitmapFlipRotator(&rotatedSource);
			if (FAILED(hr)) throw std::exception("Failed to create WIC flip rotator");
			hr = rotatedSource->Initialize(sourceToEncode, WICBitmapTransformRotate90);
			if (FAILED(hr)) throw std::exception("Failed to rotate WIC bitmap source");
			sourceToEncode = static_cast<IWICBitmapSource*>(rotatedSource.p);
			UINT previousWidth = width;
			width = height;
			height = previousWidth;
		}
		expectedDimensions.push_back(std::make_pair(width, height));

		hr = pEncoder->CreateNewFrame(&pFrameEncode, nullptr);
		if (FAILED(hr)) {
			throw std::exception("Failed to CreateNewFrame");
		}

		// Exercise the exact COM interface previously returning E_NOINTERFACE.
		CComQIPtr<IWICBitmapFrameEncode> verifiedFrame(pFrameEncode);
		if (!verifiedFrame) {
			throw std::exception("Failed IWICBitmapFrameEncode QueryInterface");
		}

		hr = pFrameEncode->Initialize(nullptr);
		if (FAILED(hr)) {
			throw std::exception("Failed to frame encode");
		}

		hr = pFrameEncode->SetSize(width, height);
		if (FAILED(hr)) {
			throw std::exception("Failed to set image size");
		}

		hr = pFrameEncode->SetResolution(dpiX, dpiY);
		if (FAILED(hr)) {
			throw std::exception("Failed to SetResolution");
		}

		hr = pFrameEncode->SetPixelFormat(&pixFormat);
		if (FAILED(hr)) {
			throw std::exception("Failed to SetPixelFormat");
		}

		hr = pFrameEncode->WriteSource(sourceToEncode, NULL);
		if (FAILED(hr)) {
			throw std::exception("Failed to WriteSource");
		}

		hr = pFrameEncode->Commit();
		if (FAILED(hr)) {
			throw std::exception("Failed to Commit frame");
		}
	}

	hr = pEncoder->Commit();
	if (FAILED(hr)) {
		throw std::exception("Failed to Commit encoder");
	}

	// The original smoke test stopped at encoder Commit. Reopen the result
	// through Windows WIC so that empty or unreadable HEIC files fail the job.
	pEncoder.Release();
	pStream.Release();
	CComPtr<IWICBitmapDecoder> pVerificationDecoder;
	hr = pFactory->CreateDecoderFromFilename(destFile.c_str(), NULL,
		GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pVerificationDecoder);
	if (FAILED(hr)) {
		printf("Readback decoder failed: 0x%08X\n", (unsigned int)hr);
		throw std::exception("Failed to reopen encoded HEIC");
	}
	UINT verificationFrames = 0;
	hr = pVerificationDecoder->GetFrameCount(&verificationFrames);
	if (FAILED(hr) || verificationFrames != nFrame) {
		throw std::exception("HEIC readback frame count mismatch");
	}
	for (UINT frameIndex = 0; frameIndex < verificationFrames; ++frameIndex) {
		CComPtr<IWICBitmapFrameDecode> decoded;
		hr = pVerificationDecoder->GetFrame(frameIndex, &decoded);
		if (FAILED(hr)) throw std::exception("HEIC readback frame failed");
		UINT w = 0, h = 0;
		hr = decoded->GetSize(&w, &h);
		if (FAILED(hr) || w == 0 || h == 0 ||
			w != expectedDimensions[frameIndex].first || h != expectedDimensions[frameIndex].second)
			throw std::exception("HEIC readback dimensions invalid");
	}
	printf("File converted and WIC readback verified\n");
}

int main()
{
	int ret = 0;
	CoInitialize(nullptr);

	try
	{
		CheckExifOrientationNormalization();
		CheckRotatedExifFixture();
		ConvertToHeic(L"samples\\01.png", L"samples\\out-01.heic");
		ConvertToHeic(L"samples\\02.png", L"samples\\out-02.heic");
		ConvertToHeic(L"samples\\03.heic", L"samples\\out-03.heic");
		ConvertToHeic(L"samples\\04.heic", L"samples\\out-04.heic");
		ConvertToHeic(L"samples\\05.heic", L"samples\\out-05.heic");
		ConvertToHeic(L"samples\\06.heic", L"samples\\out-06.heic");
		ConvertToHeic(L"samples\\07.heic", L"samples\\out-07.heic");
		ConvertToHeic(L"samples\\08.gif", L"samples\\out-08.heic");
		// 90-degree rotation of an existing HEIC, analogous to Photos rotate-save.
		ConvertToHeic(L"samples\\03.heic", L"samples\\rotated-03.heic", true);
	}
	catch (const std::exception& ex)
	{
		printf("Exception: %s\n", ex.what());
		ret = 1;
	}

	CoUninitialize();
    return ret;
}

