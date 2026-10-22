// Windows 10 HEIC/Photos triage: read-only Media Foundation HEVC inventory.
// No image input, filesystem enumeration, registry mutation or codec activation.
// A listed MFT does NOT establish Photos support, decoder entitlement or license.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <cstdio>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

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
}
int wmain(int argc, wchar_t**) {
    if (argc!=1) {
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
