// Read-only Windows WIC Fant reference for a 4032x3024 SDR BGRA frame.
// Keeps 48MB input in RAM only; never touches photos, files or codecs.
#pragma once
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace HeicThumbFant {
using Microsoft::WRL::ComPtr;
constexpr unsigned TargetW=256,TargetH=192;
inline std::vector<uint8_t> Scale(const std::vector<uint8_t>& bgra,
                                int fullWidth,int fullHeight){
 if(fullWidth!=4032||fullHeight!=3024||
    bgra.size()!=size_t(fullWidth)*size_t(fullHeight)*4)
  throw std::runtime_error("WIC Fant control is fixed to 12MP pilot sample");
 const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 if(FAILED(init))throw std::runtime_error("WIC Fant CoInitializeEx failed");
 struct Scope{~Scope(){CoUninitialize();}} cleanup;
 ComPtr<IWICImagingFactory> factory;
 if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
       IID_PPV_ARGS(factory.GetAddressOf()))))
  throw std::runtime_error("WIC imaging factory unavailable");
 ComPtr<IWICBitmap> image;
 if(FAILED(factory->CreateBitmapFromMemory(unsigned(fullWidth),unsigned(fullHeight),
     GUID_WICPixelFormat32bppBGRA,unsigned(fullWidth)*4,unsigned(bgra.size()),
     const_cast<BYTE*>(bgra.data()),image.GetAddressOf())))
  throw std::runtime_error("WIC full BGRA input refused");
 ComPtr<IWICBitmapScaler> scaler;
 if(FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) ||
    FAILED(scaler->Initialize(image.Get(),TargetW,TargetH,
          WICBitmapInterpolationModeFant)))
  throw std::runtime_error("WIC Fant scaler unavailable");
 std::vector<uint8_t> out(size_t(TargetW)*TargetH*4);
 if(FAILED(scaler->CopyPixels(nullptr,TargetW*4,unsigned(out.size()),out.data())))
  throw std::runtime_error("WIC Fant thumbnail CopyPixels failed");
 return out;
}
} // namespace HeicThumbFant
