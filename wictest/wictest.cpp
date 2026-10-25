// wictest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "../wic_heic/ExifOrientationFix.h"
#include <vector>
#include <utility>
#include "..\wic_heic\wic_heic.h"
#include "GeometryProbe.h"
#include "EncoderGuardProbe.h"
#include "WICLosslessRotationProbe.h"
#include "WICHeifOptionsProbe.h"
#include "NativeLosslessProbe.h"
#include "WICDirectLosslessProbe.h"
#include "WICPhotosOpaqueRotationProbe.h"
#include "WICDecoderRectProbe.h"

#pragma comment (lib, "windowscodecs.lib")


// Deterministic unit tests for the metadata rewrite, no real images required.
void CheckExifOrientationNormalization()
{
    auto makeExif = [](bool le, uint16_t orientation, bool app1Prefix) {
        std::vector<uint8_t> bytes;
        if (app1Prefix) bytes = {0,0,0,6, 'E','x','i','f',0,0};
        else bytes = {0,0,0,0};
        const uint8_t leBase[] = {'I','I',42,0,8,0,0,0,1,0,0x12,1,3,0,1,0,0,0,0,0,0,0,0,0,0,0};
        const uint8_t beBase[] = {'M','M',0,42,0,0,0,8,0,1,1,0x12,0,3,0,0,0,1,0,0,0,0,0,0,0};
        const uint8_t* source=le?leBase:beBase;
        bytes.insert(bytes.end(), source, source+26);
        const size_t valuePos=app1Prefix?28:22;
        if (le) { bytes[valuePos]=uint8_t(orientation); bytes[valuePos+1]=0; }
        else { bytes[valuePos]=0; bytes[valuePos+1]=uint8_t(orientation); }
        return bytes;
    };
    for (bool le : {true,false}) {
        for (bool app1Prefix : {false,true}) {
            const size_t valuePos=app1Prefix?28:22;
            for (uint16_t value : {uint16_t(6), uint16_t(8)}) {
                auto data=makeExif(le,value,app1Prefix);
                auto original=data;
                if (WicHeicOrientation::NormalizeExifForRotatedPixels(data,false) || data!=original)
                    throw std::exception("EXIF regression: unrotated metadata changed");
                if (!WicHeicOrientation::NormalizeExifForRotatedPixels(data,true))
                    throw std::exception("EXIF regression: rotated case not normalized");
                if (WicHeicOrientation::Get16(data,valuePos,le)!=1)
                    throw std::exception("EXIF regression: orientation not set to 1");
                for (size_t i=0;i<data.size();++i) {
                    if (i!=valuePos && i!=valuePos+1 && data[i]!=original[i])
                        throw std::exception("EXIF regression: unrelated metadata altered");
                }
            }
            auto half=makeExif(le,3,app1Prefix);
            auto unmodified=half;
            if(WicHeicOrientation::NormalizeExifForRotatedPixels(half,false) ||
               WicHeicOrientation::NormalizeExifForRotatedPixels(half,true) ||
               half!=unmodified)
                throw std::exception("EXIF regression: half-turn changed without irot proof");
            if(!WicHeicOrientation::NormalizeExifForRotatedPixels(half,false,true))
                throw std::exception("EXIF regression: verified half-turn not normalized");
            if(WicHeicOrientation::Get16(half,valuePos,le)!=1)
                throw std::exception("EXIF regression: half-turn Orientation != 1");
            for(size_t k=0;k<half.size();++k){
                if(k!=valuePos && k!=valuePos+1 && half[k]!=unmodified[k])
                    throw std::exception("EXIF regression: half-turn corrupted unrelated metadata");
            }
            auto normal=makeExif(le,1,app1Prefix);
            if (WicHeicOrientation::NormalizeExifForRotatedPixels(normal,true))
                throw std::exception("EXIF regression: Orientation=1 changed");
        }
    }
    printf("EXIF orientation normalization unit tests PASSED (raw TIFF + APP1)\n");
}

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
    // Metadata query reader must own its backing COM metadata blocks.
    // Historically the metadata block reader's reference count was left
    // uninitialized; releasing the source frame could destroy the reader.
    pixels.Release();
    frame.Release();
    decoder.Release();
    PropVariantInit(&value);
    hr=metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}",&value);
    const unsigned retainedOrientation=(value.vt==VT_UI2)?value.uiVal:
        (value.vt==VT_UI4)?value.ulVal:9999;
    PropVariantClear(&value);
    if(FAILED(hr) || retainedOrientation!=1)
        throw std::exception("fixture: Exif reader died with its decoder frame");
    printf("WIC118_EXIF_QUERY_READER_LIFETIME_PASS\\n");
    printf("fixture: ROTATED_HEIC_METADATA_AND_PIXELS_PASSED\\n");
}

void CheckHalfTurnExifFixture()
{
    // This fixture is generated from a PUBLIC synthetic HEIC by changing
    // just its EXIF Orientation 1->3 while irot stays at 2.
    CComPtr<IWICImagingFactory> f;
    CComPtr<IWICBitmapDecoder> dec;
    CComPtr<IWICBitmapFrameDecode> frame;
    CComPtr<IWICMetadataQueryReader> meta;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,(void**)&f);
    if(FAILED(hr))throw std::exception("HALF_TURN: WIC factory");
    hr=f->CreateDecoderFromFilename(
        L"samples\\fixture-irot2-exif3-noaux.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec);
    if(FAILED(hr))throw std::exception("HALF_TURN: generated HEIC not readable");
    hr=dec->GetFrame(0,&frame);
    if(FAILED(hr))throw std::exception("HALF_TURN: frame unavailable");
    UINT w=0,h=0;
    hr=frame->GetSize(&w,&h);
    if(FAILED(hr) || w!=1920 || h!=1080)
        throw std::exception("HALF_TURN: irot2 dimensions invalid");
    hr=frame->GetMetadataQueryReader(&meta);
    if(FAILED(hr))throw std::exception("HALF_TURN: metadata unavailable");
    PROPVARIANT v{};
    PropVariantInit(&v);
    hr=meta->GetMetadataByName(L"/app1/ifd/{ushort=274}",&v);
    const unsigned x=(v.vt==VT_UI2)?v.uiVal:(v.vt==VT_UI4?v.ulVal:9999);
    PropVariantClear(&v);
    if(FAILED(hr) || x!=1)
        throw std::exception("HALF_TURN: EXIF3 double rotation still exposed to Photos");
    printf("WIC_HALF_TURN_IROT2_EXIF3_NORMALIZED_TO_1_PASS\n");
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
		// Verify the Photos default opaque raster bridge before any other
		// HEIC decoder in this WIC test process offers a competing source.
		WICPhotosOpaqueRotationProbe::Run();
		CheckExifOrientationNormalization();
		CheckRotatedExifFixture();
		CheckHalfTurnExifFixture();
		WICDecoderRectProbe::Run();
		GeometryProbe::Run();
		EncoderGuardProbe::Run();
		WICLosslessRotationProbe::Run();
		WICHeifOptionsProbe::Run();
		NativeLosslessProbe::Run();
		WICDirectLosslessProbe::Run();
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

