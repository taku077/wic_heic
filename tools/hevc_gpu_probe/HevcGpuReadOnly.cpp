// Read-only GPU HEVC capability gate for Windows 10.
// No HEIC files, no decoder activation, no registry changes, no uploads.
// Availability is NOT proof that a libheif/WIC GPU decode path works.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxguid.lib") // SDK-defined HEVC decoder profile GUIDs
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

enum class State { Unknown, No, Yes };
static const char* Label(State x) {
    switch (x) {
    case State::Yes: return "YES";
    case State::No: return "NO";
    default: return "UNKNOWN";
    }
}
static State Answer(HRESULT hr, BOOL found) {
    return FAILED(hr) ? State::Unknown : (found ? State::Yes : State::No);
}

static void CheckD3D11() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    D3D_FEATURE_LEVEL level{};
    const HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        device.GetAddressOf(), &level, immediate.GetAddressOf());
    if (FAILED(hr)) {
        std::printf("D3D11_HARDWARE_DEVICE=UNAVAILABLE hr=0x%08lX\n",
                    static_cast<unsigned long>(hr));
        std::puts("D3D11_HEVC_MAIN_NV12=UNKNOWN");
        std::puts("D3D11_HEVC_MAIN10_P010=UNKNOWN");
        return;
    }
    std::puts("D3D11_HARDWARE_DEVICE=AVAILABLE");
    ComPtr<ID3D11VideoDevice> video;
    const HRESULT q = device.As(&video);
    if (FAILED(q)) {
        std::puts("D3D11_VIDEO_INTERFACE=UNAVAILABLE");
        std::puts("D3D11_HEVC_MAIN_NV12=UNKNOWN");
        std::puts("D3D11_HEVC_MAIN10_P010=UNKNOWN");
        return;
    }
    std::puts("D3D11_VIDEO_INTERFACE=AVAILABLE");
    bool main8=false, main10=false;
    const UINT profiles=video->GetVideoDecoderProfileCount();
    for (UINT i=0;i<profiles;i++) {
        GUID profile{};
        if (FAILED(video->GetVideoDecoderProfile(i,&profile))) continue;
        if (IsEqualGUID(profile,D3D11_DECODER_PROFILE_HEVC_VLD_MAIN)) main8=true;
        if (IsEqualGUID(profile,D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10)) main10=true;
    }
    // A returned decoder profile does not guarantee a particular pixel format
    // or that all requested HEIC tiles and bitstreams can be decoded.
    BOOL output=FALSE;
    if (main8) {
        const HRESULT h=video->CheckVideoDecoderFormat(
            &D3D11_DECODER_PROFILE_HEVC_VLD_MAIN,DXGI_FORMAT_NV12,&output);
        std::printf("D3D11_HEVC_MAIN_NV12=%s\n",Label(Answer(h,output)));
    } else std::puts("D3D11_HEVC_MAIN_NV12=NO");
    output=FALSE;
    if (main10) {
        const HRESULT h=video->CheckVideoDecoderFormat(
            &D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10,DXGI_FORMAT_P010,&output);
        std::printf("D3D11_HEVC_MAIN10_P010=%s\n",Label(Answer(h,output)));
    } else std::puts("D3D11_HEVC_MAIN10_P010=NO");
}

static void CheckMediaFoundation() {
    HRESULT h=MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(h)) {
        std::printf("MF_STARTUP=FAIL hr=0x%08lX\n",
            static_cast<unsigned long>(h));
        std::puts("MF_HEVC_HARDWARE_MFT_COUNT=UNKNOWN");
        return;
    }
    MFT_REGISTER_TYPE_INFO input={MFMediaType_Video, MFVideoFormat_HEVC};
    IMFActivate** items=nullptr;
    UINT32 count=0;
    // Enumeration only. Never ActivateObject(), never use any decoder.
    h=MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
       MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
       &input,nullptr,&items,&count);
    if (FAILED(h))
        std::printf("MF_HEVC_HARDWARE_MFT_COUNT=UNKNOWN hr=0x%08lX\n",
            static_cast<unsigned long>(h));
    else
        std::printf("MF_HEVC_HARDWARE_MFT_COUNT=%u\n",count);
    if (items) {
        for (UINT32 i=0;i<count;i++) if(items[i]) items[i]->Release();
        CoTaskMemFree(items);
    }
    MFShutdown();
}
int main(int argc,char** argv) {
    if (argc==2 && std::strcmp(argv[1],"--self-test")==0) {
        if(std::strcmp(Label(Answer(S_OK,TRUE)),"YES")!=0 ||
           std::strcmp(Label(Answer(S_OK,FALSE)),"NO")!=0 ||
           std::strcmp(Label(Answer(E_FAIL,TRUE)),"UNKNOWN")!=0) return 10;
        std::puts("HEVC_GPU_CAPABILITY_SELFTEST_PASS");
        return 0;
    }
    if(argc!=1) {
        std::puts("Usage: HevcGpuReadOnly.exe [--self-test]");
        return 2;
    }
    std::puts("HEVC_GPU_READONLY_CAPABILITY_REPORT_V1");
    std::puts("NO_IMAGE_IO=1 REGISTRY_WRITES=0 DECODER_ACTIVATION=0");
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com)) {
        std::printf("COM_INIT=FAIL hr=0x%08lX\n",
            static_cast<unsigned long>(com));
        return 3;
    }
    CheckD3D11();
    CheckMediaFoundation();
    CoUninitialize();
    std::puts("RESULT_SCOPE=CAPABILITY_ONLY_NOT_HEIC_DECODE");
    std::puts("CI_GPU_RESULTS_ARE_NOT_WINDOWS10_USER_PC_RESULTS");
    return 0;
}
