// Read-only D3D11 HEVC hardware decoder capability probe.
// Query Windows display adapters only. No HEIC images, disk writes,
// Shell registrations, codec installations, or device decode submissions.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "ole32.lib")
using Microsoft::WRL::ComPtr;
namespace {
constexpr GUID hevcMain = {0x5b11d51b,0x2f4c,0x4452,{0xbc,0xc3,0x09,0xf2,0xa1,0x16,0x0c,0xc0}};
constexpr GUID hevcMain10 = {0x107af0e0,0xef1a,0x4d19,{0xab,0xa8,0x67,0xa1,0x63,0x07,0x3d,0x13}};
bool Profile(const GUID& actual,const GUID& wanted) {
 return IsEqualGUID(actual,wanted)!=FALSE;
}
bool FormatOk(ID3D11VideoDevice* video,const GUID& profile,DXGI_FORMAT fmt) {
 BOOL ok=FALSE;
 const HRESULT result=video->CheckVideoDecoderFormat(&profile,fmt,&ok);
 return SUCCEEDED(result)&&ok;
}
bool ProbeConfiguration(ID3D11VideoDevice* video,const GUID& profile,
                        DXGI_FORMAT fmt,UINT width,UINT height) {
 D3D11_VIDEO_DECODER_DESC description{};
 description.Guid=profile;
 description.SampleWidth=width;
 description.SampleHeight=height;
 description.OutputFormat=fmt;
 UINT configurations=0;
 const HRESULT hr=video->GetVideoDecoderConfigCount(&description,&configurations);
 std::printf("HEIC_GPU_DECODER_CONFIG query=%ux%u dxgi_format=%u status=0x%08X count=%u\n",
              width,height,unsigned(fmt),unsigned(hr),configurations);
 return SUCCEEDED(hr)&&configurations>0;
}
}
int wmain(){
 try {
  ComPtr<IDXGIFactory1> factory;
  const HRESULT create=CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
  if(FAILED(create))throw std::runtime_error("DXGI factory unavailable");
  int hardware=0,hevcEligible=0;
  for(UINT index=0;index<32;++index){
    ComPtr<IDXGIAdapter1> adapter;
    const HRESULT enumeration=factory->EnumAdapters1(index,adapter.GetAddressOf());
    if(enumeration==DXGI_ERROR_NOT_FOUND)break;
    if(FAILED(enumeration))throw std::runtime_error("DXGI enumeration failure");
    DXGI_ADAPTER_DESC1 description{};
    if(FAILED(adapter->GetDesc1(&description)))continue;
    // Virtual CI runners can expose WARP / Microsoft Basic software adapters.
    // Those do not prove HEVC hardware decode support on the user's Intel GPU.
    const bool software=(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0;
    std::printf("HEIC_GPU_ADAPTER index=%u vendor=0x%04X device=0x%04X software=%u description=%ls\n",
       index,description.VendorId,description.DeviceId,unsigned(software),description.Description);
    if(software)continue;
    ++hardware;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    D3D_FEATURE_LEVEL actual{};
    const HRESULT hr=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,
        UINT(sizeof(levels)/sizeof(levels[0])),D3D11_SDK_VERSION,
        device.GetAddressOf(),&actual,immediate.GetAddressOf());
    std::printf("HEIC_GPU_D3D11_DEVICE adapter=%u hr=0x%08X feature_level=0x%04X\n",
       index,unsigned(hr),unsigned(actual));
    if(FAILED(hr)||!device)continue;
    ComPtr<ID3D11VideoDevice> video;
    const HRESULT vi=device.As(&video);
    if(FAILED(vi)||!video){
      std::printf("HEIC_GPU_NO_VIDEO_DEVICE adapter=%u hr=0x%08X\n",index,unsigned(vi));
      continue;
    }
    const UINT count=video->GetVideoDecoderProfileCount();
    std::printf("HEIC_GPU_VIDEO_PROFILE_COUNT adapter=%u count=%u\n",index,count);
    bool profileMain=false,profileMain10=false;
    for(UINT i=0;i<count && i<512;++i) {
      GUID id{};
      if(FAILED(video->GetVideoDecoderProfile(i,&id)))continue;
      if(Profile(id,hevcMain))profileMain=true;
      if(Profile(id,hevcMain10))profileMain10=true;
    }
    for(int mode=0;mode<2;++mode){
      const bool found=mode?profileMain10:profileMain;
      const GUID& guid=mode?hevcMain10:hevcMain;
      const DXGI_FORMAT fmt=mode?DXGI_FORMAT_P010:DXGI_FORMAT_NV12;
      const bool supportsFormat=found && FormatOk(video.Get(),guid,fmt);
      const bool config=supportsFormat &&
         ProbeConfiguration(video.Get(),guid,fmt,4032,3024);
      std::printf("HEIC_GPU_PROFILE adapter=%u profile=%s exposed=%d output_format=%d decoder_config12mp=%d\n",
          index,mode?"hevc_main10":"hevc_main8",int(found),
          int(supportsFormat),int(config));
      if(config)++hevcEligible;
    }
  }
  std::printf("HEIC_GPU_CAPS_SUMMARY hardware_adapters=%d supported_12mp_profiles=%d\n",
      hardware,hevcEligible);
  if(hevcEligible==0)std::puts("HEIC_GPU_CAPS_CI_HARDWARE_PATH_UNAVAILABLE_OR_NOT_DEMONSTRATED");
  else std::puts("HEIC_GPU_CAPS_CANDIDATE_ONLY_NOT_HEIC_DECODE_PROVEN");
  std::puts("HEIC_GPU_CAPS_READONLY_NO_MSI_NO_REGISTRY_PASS");
  return 0;
 } catch(const std::exception& e){
  std::printf("HEIC_GPU_CAPS_ERROR %s\n",e.what());return 1;
 }
}
