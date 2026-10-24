// Windows 10 HEIC/Photos triage: read-only Media Foundation HEVC inventory.
// No image inputs or deliberate filesystem/registry writes. Default mode only
// enumerates registrations; the explicit WIC option can load codec DLLs.
// A listed MFT does NOT establish Photos support, decoder entitlement or license.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wincodec.h>
#include <cstdio>
#include <cwchar>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {
struct Result { HRESULT hr; UINT32 count; };
Result Enumerate(const GUID& category, const GUID& codec, UINT32 flags) noexcept {
    MFT_REGISTER_TYPE_INFO type{ MFMediaType_Video, codec };
    IMFActivate** list = nullptr;
    UINT32 count = 0;
    const HRESULT hr = MFTEnumEx(category, flags, &type, nullptr, &list, &count);
    // Enumerate-only: do NOT call ActivateObject; never load individual MFTs.
    if (list) {
        if (SUCCEEDED(hr)) {
            for (UINT32 i=0; i<count; ++i) if (list[i]) list[i]->Release();
        }
        CoTaskMemFree(list);
    }
    return { hr, SUCCEEDED(hr) ? count : 0u };
}
void Print(const char* kind, const char* mode, Result r) {
    std::printf("HEVC_MF_INVENTORY codec=%s route=%s hr=0x%08lX count=%lu scope=process_enumeration_only license_not_verified=1 photos_not_verified=1\n",
                kind, mode, static_cast<unsigned long>(r.hr),
                static_cast<unsigned long>(r.count));
}
// No files, images, Photos IPC, AppX changes, registry writes or codec
// installation. A passive WIC query only reads WIC component registration.
// The optional --wic-activate-no-image creates COM instances to distinguish
// "listed" from "instantiable" (DLLs may load and perform their own internal
// initialization; this is NOT a proof of HEVC decode or store entitlement).
const GUID kNativeHeif={0xE1E62521,0x6787,0x405B,
    {0xA3,0x39,0x50,0x07,0x15,0xB5,0x76,0x3F}};
const GUID kPrivateHeic={0x98CE31DD,0x5435,0x41BD,
    {0xA1,0xAF,0x29,0xC3,0x87,0xC2,0x01,0x35}};
const CLSID kMicrosoftDecoder={0xE9A4A80A,0x44FE,0x4DE4,
    {0x89,0x71,0x71,0x50,0xB1,0x0A,0x51,0x99}};
const CLSID kCustomDecoder={0x6215667C,0x74CA,0x4076,
    {0xA7,0x69,0x3F,0x83,0x4C,0x3F,0x25,0xF5}};
const CLSID kCustomEncoder={0x0E8AB687,0x73A5,0x455B,
    {0x8C,0x19,0xB9,0x3C,0xA9,0xAC,0x0E,0xEB}};

void ProbeWicComponent(IWICImagingFactory* factory,const char* role,
                       const CLSID& clsid,bool isDecoder,bool activate) {
    IWICComponentInfo* info=nullptr;
    const HRESULT hr=factory->CreateComponentInfo(clsid,&info);
    GUID format{};
    HRESULT formatHr=E_NOINTERFACE;
    HRESULT activationHr=E_NOINTERFACE;
    if(SUCCEEDED(hr)&&info) {
        if(isDecoder) {
            IWICBitmapDecoderInfo* decoderInfo=nullptr;
            const HRESULT qi=info->QueryInterface(
                IID_IWICBitmapDecoderInfo,reinterpret_cast<void**>(&decoderInfo));
            if(SUCCEEDED(qi)&&decoderInfo) {
                formatHr=decoderInfo->GetContainerFormat(&format);
                if(activate) {
                    IWICBitmapDecoder* instance=nullptr;
                    activationHr=decoderInfo->CreateInstance(&instance);
                    if(instance)instance->Release();
                }
                decoderInfo->Release();
            } else {
                formatHr=qi;
                if(decoderInfo)decoderInfo->Release();
            }
        } else {
            IWICBitmapEncoderInfo* encoderInfo=nullptr;
            const HRESULT qi=info->QueryInterface(
                IID_IWICBitmapEncoderInfo,reinterpret_cast<void**>(&encoderInfo));
            if(SUCCEEDED(qi)&&encoderInfo) {
                formatHr=encoderInfo->GetContainerFormat(&format);
                if(activate) {
                    IWICBitmapEncoder* instance=nullptr;
                    activationHr=encoderInfo->CreateInstance(&instance);
                    if(instance)instance->Release();
                }
                encoderInfo->Release();
            } else {
                formatHr=qi;
                if(encoderInfo)encoderInfo->Release();
            }
        }
        info->Release();
    }
    std::printf("HEVC_WIC_CODEC_INFO role=%s component_hr=0x%08lX format_hr=0x%08lX native_heif=%u private_heic=%u\n",
        role,static_cast<unsigned long>(hr),
        static_cast<unsigned long>(formatHr),
        static_cast<unsigned>(SUCCEEDED(formatHr)&&IsEqualGUID(format,kNativeHeif)),
        static_cast<unsigned>(SUCCEEDED(formatHr)&&IsEqualGUID(format,kPrivateHeic)));
    if(activate) {
        std::printf("HEVC_WIC_CODEC_CREATE role=%s hr=0x%08lX no_image=1 license_not_verified=1 photos_not_verified=1\n",
            role,static_cast<unsigned long>(activationHr));
    }
}
void ProbeWic(bool activate) {
    IWICImagingFactory* factory=nullptr;
    const HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        reinterpret_cast<void**>(&factory));
    std::printf("HEVC_WIC_FACTORY hr=0x%08lX\n",
                static_cast<unsigned long>(hr));
    if(SUCCEEDED(hr)&&factory) {
        ProbeWicComponent(factory,"microsoft_heif_decoder",kMicrosoftDecoder,true,activate);
        ProbeWicComponent(factory,"private_heic_decoder",kCustomDecoder,true,activate);
        ProbeWicComponent(factory,"private_heic_encoder",kCustomEncoder,false,activate);
        if(activate) {
            IWICBitmapEncoder* nativeEncoder=nullptr;
            const HRESULT found=factory->CreateEncoder(kNativeHeif,nullptr,&nativeEncoder);
            if(nativeEncoder)nativeEncoder->Release();
            std::printf("HEVC_WIC_NATIVE_HEIF_ENCODER_CREATE hr=0x%08lX no_image=1 license_not_verified=1 photos_not_verified=1\n",
                static_cast<unsigned long>(found));
        }
        factory->Release();
    }
    std::printf("HEVC_WIC_PROBE_COMPLETE activation=%u no_image=1 no_registry_write=1 no_codec_install=1 photos_not_verified=1\n",
        static_cast<unsigned>(activate));
}
}
int wmain(int argc, wchar_t** argv) {
    const bool activation=(argc==2 && argv && argv[1] &&
        std::wcscmp(argv[1],L"--wic-activate-no-image")==0);
    if (argc!=1 && !activation) {
        std::puts("HEVC_MF_INVENTORY_REFUSED arguments_not_allowed=1");
        return 2;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
        std::printf("HEVC_MF_PROBE_STATUS stage=com hr=0x%08lX\n",
                    static_cast<unsigned long>(com));
        return 3;
    }
    const HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(mf)) {
        std::printf("HEVC_MF_PROBE_STATUS stage=mfstartup hr=0x%08lX capability_unknown=1\n",
                    static_cast<unsigned long>(mf));
        CoUninitialize();
        return 4;
    }
    const UINT32 common = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
         MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_LOCALMFT |
         MFT_ENUM_FLAG_SORTANDFILTER;
    // Playback/Photos-interesting path: ignore field-of-use MFTs that require
    // application-specific unlock. Count does not prove actual activation.
    const auto hevcDecode = Enumerate(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_HEVC, common);
    const auto hevcEncode = Enumerate(MFT_CATEGORY_VIDEO_ENCODER, MFVideoFormat_HEVC, common);
    const auto h264Decode = Enumerate(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_H264, common);
    // Diagnostic contrast only: count possibly locked field-of-use MFTs,
    // but never attempt to unlock, instantiate or test proprietary codecs.
    const auto hevcFoU = Enumerate(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_HEVC,
                                  common | MFT_ENUM_FLAG_FIELDOFUSE);
    ProbeWic(activation);
    Print("HEVC", "decoder_common", hevcDecode);
    Print("HEVC", "encoder_common", hevcEncode);
    Print("H264", "decoder_common", h264Decode);
    Print("HEVC", "decoder_including_field_of_use", hevcFoU);
    std::puts("HEVC_MF_PROBE_COMPLETE no_photos_test=1 no_codec_install=1 no_file_input=1 no_registration_change=1");
    MFShutdown();
    CoUninitialize();
    return FAILED(hevcDecode.hr) || FAILED(hevcEncode.hr) ||
           FAILED(h264Decode.hr) || FAILED(hevcFoU.hr) ? 5 : 0;
}
