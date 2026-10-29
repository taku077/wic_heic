#pragma once
// Regression for real embedded HEVC thumbnail retrieval. Artificial CI fixture
// only; NEVER rewrites user HEIC or alters Shell registration permanently.
#include <windows.h>
#include <wincodec.h>
#include <atlbase.h>
#include <array>
#include <vector>
#include <string>
#include <cstdio>
#include <stdexcept>
namespace WICThumbnailEmbeddedProbe {
inline void Must(HRESULT hr,const char* phase) {
    if(FAILED(hr)) {
        std::printf("THUMB_EMBEDDED_WIC_FAIL stage=%s hr=0x%08X\n",
                    phase,static_cast<unsigned>(hr));
        throw std::runtime_error(phase);
    }
}
inline std::vector<BYTE> Pixels(IWICBitmapSource* source) {
    UINT w=0,h=0;
    Must(source->GetSize(&w,&h),"thumb GetSize");
    if(w!=320 || h!=240)throw std::runtime_error("Embedded WIC thumbnail dimensions");
    WICPixelFormatGUID format{};
    Must(source->GetPixelFormat(&format),"thumb GetPixelFormat");
    if(!IsEqualGUID(format,GUID_WICPixelFormat24bppRGB))
        throw std::runtime_error("Expected thumbnail 24bpp RGB");
    std::vector<BYTE> result(size_t(w)*h*3);
    Must(source->CopyPixels(nullptr,w*3,UINT(result.size()),result.data()),
         "thumb CopyPixels");
    return result;
}
inline void Run() {
    wchar_t ci[12]{};
    if(GetEnvironmentVariableW(L"GITHUB_ACTIONS",ci,12)!=4 ||
       wcscmp(ci,L"true")!=0)
        throw std::runtime_error("Embedded HEVC probe is CI only");
    wchar_t temp[32768]{};
    const DWORD count=GetEnvironmentVariableW(L"RUNNER_TEMP",temp,32768);
    if(!count||count>=32768)throw std::runtime_error("Missing CI temp path");
    const std::wstring path=std::wstring(temp,count)+
        L"\\wicfast_embedded_4032x3024.heic";
    if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("Missing generated thumbnail fixture");
    CComPtr<IWICImagingFactory> factory;
    Must(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
         CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"factory");
    CComPtr<IWICStream> stream;
    Must(factory->CreateStream(&stream),"stream");
    Must(stream->InitializeFromFilename(path.c_str(),GENERIC_READ),
         "read HEIC");
    CComPtr<IWICBitmapDecoder> decoder;
    Must(CoCreateInstance(CLSID_HEICDecoder,nullptr,
         CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&decoder)),"own decoder");
    Must(decoder->Initialize(stream,WICDecodeMetadataCacheOnDemand),
         "decode Initialize");
    UINT frames=0;
    Must(decoder->GetFrameCount(&frames),"GetFrameCount");
    if(frames!=1)throw std::runtime_error("Unexpected synthetic frame count");
    CComPtr<IWICBitmapFrameDecode> mainFrame;
    Must(decoder->GetFrame(0,&mainFrame),"GetFrame(0)");
    UINT mainW=0,mainH=0;
    Must(mainFrame->GetSize(&mainW,&mainH),"main frame GetSize");
    if(mainW!=4032||mainH!=3024)
        throw std::runtime_error("Primary HEIC dimensions lost");
    // First the decoder-level HEIF thmb reference, then frame-level. Both
    // must retrieve the same native compressed thumbnail, NOT full RGB.
    CComPtr<IWICBitmapSource> topThumb,frameThumb;
    Must(decoder->GetThumbnail(&topThumb),"Decoder::GetThumbnail");
    Must(mainFrame->GetThumbnail(&frameThumb),"Frame::GetThumbnail");
    auto top=Pixels(topThumb);
    auto child=Pixels(frameThumb);
    if(top.empty()||top!=child)
        throw std::runtime_error("Decoder/frame thumbnails differ in RGB");
    const auto sample=[&](unsigned x,unsigned y){
        const size_t off=(size_t(y)*320u+x)*3u;
        return std::array<BYTE,3>{top[off],top[off+1],top[off+2]};
    };
    // The artificial 4-quadrant HEVC must not decode to an empty/flat image.
    if(sample(40,40)==sample(280,40) ||
       sample(40,200)==sample(280,200) ||
       sample(40,40)==sample(40,200))
        throw std::runtime_error("Embedded thumbnail did not decode its pixels");
    std::printf("THUMB_EMBEDDED_HEVC_RGB_PARITY_PASS "
                "source=4032x3024 thumb=320x240 frame_count=1 "
                "decoder_vs_frame_exact_rgb=1 primary_pixels_not_requested=1\n");
}
}