// Disposable GitHub Actions only: generate a reproducible, artificial 12MP HEIC.
// No global WIC registrations, no private photo access, no preexisting-file overwrite.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "../../wic_heic/wic_heic.h"
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"windowscodecs.lib")
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT kWidth=4032,kHeight=3024;
void Check(HRESULT hr,const char* what) {
 if(FAILED(hr)) {
   std::printf("SYNTHETIC_12MP_GENERATION_FAILED stage=%s hr=0x%08X\n",what,unsigned(hr));
   throw hr;
 }
}
std::wstring Env(const wchar_t* key) {
 wchar_t buf[32768]{};
 const DWORD n=GetEnvironmentVariableW(key,buf,32768);
 return n>0&&n<32768?std::wstring(buf,n):L"";
}
void GuardPath(const wchar_t* path){
 if(Env(L"GITHUB_ACTIONS")!=L"true")throw E_ACCESSDENIED;
 const auto root=Env(L"RUNNER_TEMP");
 if(root.empty()||!path)throw E_INVALIDARG;
 const auto candidate=std::wstring(path);
 const auto expected=root+L"\\wicfast_synthetic_4032x3024.heic";
 if(_wcsicmp(candidate.c_str(),expected.c_str())!=0)throw E_ACCESSDENIED;
 if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES)throw HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
}
}
int wmain(int argc,wchar_t** argv){
 try {
  if(argc!=3)throw E_INVALIDARG;
  GuardPath(argv[2]);
  Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"COM");
  const auto dll=std::wstring(argv[1]);
  HMODULE module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if(!module)throw HRESULT_FROM_WIN32(GetLastError());
  using GetFactory=HRESULT (STDAPICALLTYPE *)(REFCLSID,REFIID,LPVOID*);
  auto entry=reinterpret_cast<GetFactory>(GetProcAddress(module,"DllGetClassObject"));
  if(!entry)throw E_NOINTERFACE;
  ComPtr<IClassFactory> cls;
  Check(entry(CLSID_HEICEncoder,IID_IClassFactory,reinterpret_cast<void**>(cls.GetAddressOf())),"encoder class");
  ComPtr<IWICBitmapEncoder> encoder;
  Check(cls->CreateInstance(nullptr,IID_PPV_ARGS(encoder.GetAddressOf())),"encoder instance");
  ComPtr<IWICImagingFactory> factory;
  Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(factory.GetAddressOf())),"WIC factory");
  ComPtr<IWICStream> stream;
  Check(factory->CreateStream(stream.GetAddressOf()),"stream create");
  Check(stream->InitializeFromFilename(argv[2],GENERIC_WRITE),"stream open");
  Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"encoder init");
  ComPtr<IWICBitmapFrameEncode> frame;
  Check(encoder->CreateNewFrame(frame.GetAddressOf(),nullptr),"frame create");
  Check(frame->Initialize(nullptr),"frame init");
  Check(frame->SetSize(kWidth,kHeight),"frame size");
  WICPixelFormatGUID format=GUID_WICPixelFormat24bppRGB;
  Check(frame->SetPixelFormat(&format),"frame pixel format");
  if(!IsEqualGUID(format,GUID_WICPixelFormat24bppRGB))throw E_FAIL;
  // Natural-looking synthetic texture/edges, without photos or metadata.
  const UINT stride=kWidth*3u;
  std::vector<BYTE> pixels(size_t(stride)*kHeight);
  for(UINT y=0;y<kHeight;++y){
    for(UINT x=0;x<kWidth;++x){
      const size_t offset=size_t(y)*stride+size_t(x)*3u;
      const uint32_t fine=(x*13u+y*7u+(x^y)*3u)&255u;
      const uint32_t tile=((x/64u+y/64u)&1u)?22u:0u;
      pixels[offset+0]=BYTE((x/16u+fine/3u+tile)&255u);
      pixels[offset+1]=BYTE((y/12u+fine/4u+tile)&255u);
      pixels[offset+2]=BYTE(((x+y)/25u+fine/2u+tile)&255u);
    }
  }
  Check(frame->WritePixels(kHeight,stride,UINT(pixels.size()),pixels.data()),"write pixels");
  Check(frame->Commit(),"frame commit");
  Check(encoder->Commit(),"encoder commit");
  pixels.clear();
  frame.Reset();encoder.Reset();stream.Reset();factory.Reset();cls.Reset();
  FreeLibrary(module);CoUninitialize();
  WIN32_FILE_ATTRIBUTE_DATA attrs{};
  if(!GetFileAttributesExW(argv[2],GetFileExInfoStandard,&attrs))throw E_FAIL;
  const uint64_t bytes=(uint64_t(attrs.nFileSizeHigh)<<32u)|attrs.nFileSizeLow;
  if(bytes<1000u || bytes>32ull*1024*1024)throw E_FAIL;
  std::printf("SYNTHETIC_12MP_HEIC_CREATED width=%u height=%u bytes=%llu\n",
        kWidth,kHeight,static_cast<unsigned long long>(bytes));
  return 0;
 } catch(HRESULT hr) {
  std::printf("SYNTHETIC_12MP_FAILED hr=0x%08X\n",unsigned(hr));
  return 1;
 } catch(...) {
  std::puts("SYNTHETIC_12MP_FAILED unknown_exception");
  return 2;
 }
}
