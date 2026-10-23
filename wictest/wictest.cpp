// wictest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "../wic_heic/ExifOrientationFix.h"
#include <vector>
#include <utility>
#include "..\wic_heic\wic_heic.h"
#include "GeometryProbe.h"
#include "EncoderGuardProbe.h"
#include "ICCProfileProbe.h"
#include "../wic_heic/HeicThumbnailGeometry.h"
#include "HighBitDepthProbe.h"
#include "ThumbnailPerformanceProbe.h"
#include "DirectThumbnailProviderProbe.h"
#include "ShellPreloadProbe.h"
#include "WICLosslessRotationProbe.h"
#include "WICHeifOptionsProbe.h"
#include "NativeLosslessProbe.h"
#include "WICDirectLosslessProbe.h"
#include "WICDecoderCapabilityProbe.h"
#include "WICComFactoryProbe.h"
#include "PhotosNavigationZoomProbe.h"

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
    // Regression for metadata block-reader COM reference management. The
    // query reader must remain valid after its temporary block-reader
    // creation reference is released; repeated Photos metadata queries
    // must not destabilize Explorer or the image viewer.
    for (int iteration=0; iteration<256; ++iteration) {
        CComPtr<IWICMetadataQueryReader> again;
        hr=frame->GetMetadataQueryReader(&again);
        if(FAILED(hr) || !again)
            throw std::runtime_error("Repeated WIC metadata reader creation failed");
        PROPVARIANT reread;
        PropVariantInit(&reread);
        hr=again->GetMetadataByName(L"/app1/ifd/{ushort=274}",&reread);
        const bool good=SUCCEEDED(hr) &&
            ((reread.vt==VT_UI2 && reread.uiVal==1) ||
             (reread.vt==VT_UI4 && reread.ulVal==1));
        PropVariantClear(&reread);
        if(!good)
            throw std::runtime_error("Repeated WIC EXIF orientation query failed");
    }
    printf("WIC_METADATA_READER_256_CREATE_QUERY_RELEASE_PASS\\n");


    CComPtr<IWICBitmapSource> pixels;
    hr=frame->QueryInterface(IID_IWICBitmapSource,(void**)&pixels);
    if (FAILED(hr)) throw std::exception("fixture: IWICBitmapSource QI failed");
    WICPixelFormatGUID fmt{};
    hr=frame->GetPixelFormat(&fmt);
    if(FAILED(hr))throw std::exception("fixture: pixel format unavailable");
    const UINT byteCount = IsEqualGUID(fmt,GUID_WICPixelFormat48bppRGB)?6u:
                           IsEqualGUID(fmt,GUID_WICPixelFormat64bppRGBA)?8u:
                           IsEqualGUID(fmt,GUID_WICPixelFormat32bppRGBA)?4u:3u;
    std::vector<BYTE> firstRow(size_t(width)*byteCount);
    WICRect region={0,0,static_cast<INT>(width),1};
    hr=pixels->CopyPixels(&region,UINT(firstRow.size()),static_cast<UINT>(firstRow.size()),firstRow.data());
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
	bool sourceHasHighBitDepth=false;

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
		if (IsEqualGUID(pixFormat,GUID_WICPixelFormat48bppRGB) ||
		    IsEqualGUID(pixFormat,GUID_WICPixelFormat64bppRGBA))
			sourceHasHighBitDepth=true;

		IWICBitmapSource* sourceToEncode = static_cast<IWICBitmapSource*>(pFrameDecode.p);
		CComPtr<IWICBitmapFlipRotator> rotatedSource;
		CComPtr<IWICBitmap> syntheticRasterRotation;
		if (rotate90) {
			hr = pFactory->CreateBitmapFlipRotator(&rotatedSource);
			if (FAILED(hr)) throw std::exception("Failed to create WIC flip rotator");
			hr = rotatedSource->Initialize(sourceToEncode, WICBitmapTransformRotate90);
			if (FAILED(hr)) throw std::exception("Failed to rotate WIC bitmap source");
			// This function is a CI-only synthetic geometry test, not a
			// production HEIC photo editor. Explicitly materialize rotated
			// RGB pixels so we can compare geometry independent of metadata.
			// Do not feed a WIC flip-rotator directly into the HEIC encoder:
			// it cannot preserve original HEIC Exif/ICC/HDR provenance.
			hr = pFactory->CreateBitmapFromSource(rotatedSource,
				WICBitmapCacheOnLoad, &syntheticRasterRotation);
			if (FAILED(hr)) throw std::exception("Failed to materialize synthetic raster");
			sourceToEncode = static_cast<IWICBitmapSource*>(syntheticRasterRotation.p);
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

		// This utility runs only on the repository's public synthetic smoke
		// samples. The HEIC encoder cannot yet save an arbitrary physical DPI,
		// and it now correctly rejects such a request instead of lying with S_OK.
		// Explicitly record that the synthetic transcode is not DPI parity;
		// never apply this omission to an existing user's HEIC or Photos save.
		if (dpiX == 96.0 && dpiY == 96.0) {
			hr = pFrameEncode->SetResolution(dpiX, dpiY);
			if (FAILED(hr)) throw std::exception("Failed to set default DPI");
		} else {
			printf("SYNTHETIC_SMOKE_DPI_METADATA_NOT_TRANSFERRED original_x=%.6f original_y=%.6f\\n",
				dpiX, dpiY);
		}

		hr = pFrameEncode->SetPixelFormat(&pixFormat);
		if (FAILED(hr)) {
			throw std::exception("Failed to SetPixelFormat");
		}

		if (syntheticRasterRotation) {
			// This is a synthetic geometry-only conversion. A materialized
			// IWICBitmap may also have come from a real HEIC with metadata,
			// so the product encoder's WriteSource must reject it. These
			// test-generated raster pixels opt in explicitly via WritePixels.
			UINT bytesPerPixel=0;
			if (IsEqualGUID(pixFormat,GUID_WICPixelFormat24bppRGB))
				bytesPerPixel=3;
			else if (IsEqualGUID(pixFormat,GUID_WICPixelFormat32bppRGBA))
				bytesPerPixel=4;
			else if (IsEqualGUID(pixFormat,GUID_WICPixelFormat48bppRGB))
				bytesPerPixel=6;
			else if (IsEqualGUID(pixFormat,GUID_WICPixelFormat64bppRGBA))
				bytesPerPixel=8;
			if (!bytesPerPixel) throw std::exception("Unsupported synthetic pixel format");
			const uint64_t stride=uint64_t(width)*bytesPerPixel;
			const uint64_t total=stride*height;
			if (!stride || total>UINT_MAX || stride>UINT_MAX)
				throw std::exception("Synthetic rotated image too large");
			CComPtr<IWICBitmapSource> normalized;
			hr=WICConvertBitmapSource(pixFormat,sourceToEncode,&normalized);
			if (FAILED(hr)) throw std::exception("Synthetic rotated pixel negotiation failed");
			std::vector<BYTE> bytes(static_cast<size_t>(total));
			hr=normalized->CopyPixels(nullptr,UINT(stride),UINT(total),bytes.data());
			if (FAILED(hr)) throw std::exception("Synthetic rotated CopyPixels failed");
			hr=pFrameEncode->WritePixels(height,UINT(stride),UINT(total),bytes.data());
			if (FAILED(hr)) throw std::exception("Synthetic rotated WritePixels failed");
		} else {
			hr = pFrameEncode->WriteSource(sourceToEncode, NULL);
			if (FAILED(hr)) throw std::exception("Failed to WriteSource");
		}

		hr = pFrameEncode->Commit();
		if (FAILED(hr)) {
			throw std::exception("Failed to Commit frame");
		}
	}

	hr = pEncoder->Commit();
	if (FAILED(hr)) {
		if (sourceHasHighBitDepth) {
			// The pinned backend rejects 10-bit re-encoding. Never silently
			// advertise 10-bit input as 8-bit just to make this test pass.
			printf("HIGHBIT_HEVC_ENCODER_BACKEND_BLOCKED HRESULT=0x%08X (NOT JPEG PARITY)\n",(unsigned)hr);
			return;
		}
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
        // Observation-only mode: run against either pinned 1.1.18 or the
        // current experimental codec. Never opens personal images, writes
        // files, or alters WIC registry. CI isolates installed versions.
        char surveyFlag[4]{};
        if(GetEnvironmentVariableA("WIC_HEIC_CAPABILITY_SURVEY_ONLY",
           surveyFlag,DWORD(sizeof(surveyFlag)))>0 && surveyFlag[0]=='1'){
            WICDecoderCapabilityProbe::Survey();
            CoUninitialize();
            return 0;
        }
		if (!HeicThumbnailGeometry::Compatible(3024,4032,240,320) ||
		    !HeicThumbnailGeometry::Compatible(4032,3024,320,240) ||
		    HeicThumbnailGeometry::Compatible(3024,4032,320,240) ||
		    HeicThumbnailGeometry::Compatible(4032,3024,240,320) ||
		    HeicThumbnailGeometry::Compatible(0,4032,240,320)) {
			throw std::runtime_error("HEIC embedded thumbnail orientation guard failed");
		}
		printf("HEIC_THUMBNAIL_ORIENTATION_PARITY_UNIT_PASS\\n");
		CheckExifOrientationNormalization();
		CheckRotatedExifFixture();
		WICComFactoryProbe::Run();
		WICDecoderCapabilityProbe::Run();
		PhotosNavigationZoomProbe::Run();
		GeometryProbe::Run();
		EncoderGuardProbe::Run();
		ICCProfileProbe::Run();
		HighBitDepthProbe::Run();

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
		// Measure performance after the bounded diagnostic feature tests: do not consume
		// the 2 MiB WIC trace budget before checking lossless rotation events.
		ThumbnailPerformanceProbe::Run();
		ThumbnailPerformanceProbe::RunDecodeIsolation();
		ThumbnailPerformanceProbe::RunOpenPathBenchmark();
		ThumbnailPerformanceProbe::RunDirectThumbnailAB();
		DirectThumbnailProviderProbe::Run();
		ShellPreloadProbe::Run();
	}
	catch (const std::exception& ex)
	{
		printf("Exception: %s\n", ex.what());
		ret = 1;
	}

	CoUninitialize();
    return ret;
}

