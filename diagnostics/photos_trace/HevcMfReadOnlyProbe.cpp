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
#include <cstring>
#include <vector>
#include <string>
#include <wincrypt.h>
#include <wrl/client.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "advapi32.lib")

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
// Opt-in ONLY: genuine WIC decoder output on a single checked-in PUBLIC
// synthetic HEIC. No arbitrary image path argument is supported. The 10,597
// bytes must exactly match git blob SHA1 126b24780e8c36a0e1521df982067ab12f971470,
// including the Git "blob 10597\0" prefix. No photos/EXIF/paths are logged.
bool VerifySyntheticFixture(std::vector<BYTE>& out) {
    wchar_t exe[MAX_PATH]{};
    const DWORD len=GetModuleFileNameW(nullptr,exe,MAX_PATH);
    if(!len||len>=MAX_PATH-40)return false;
    wchar_t* slash=std::wcsrchr(exe,L'\\');
    if(!slash)return false;
    slash[1]=L'\0';
    const std::wstring path=std::wstring(exe)+L"fixture-irot3-exif6-noaux.heic";
    const HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION stat{};
    LARGE_INTEGER size{};
    bool valid=GetFileInformationByHandle(h,&stat) &&
        !(stat.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) &&
        GetFileType(h)==FILE_TYPE_DISK &&
        GetFileSizeEx(h,&size) && size.QuadPart==10597;
    if(valid) {
        out.resize(10597);
        DWORD read=0;
        valid=ReadFile(h,out.data(),DWORD(out.size()),&read,nullptr) &&
              read==out.size();
    }
    CloseHandle(h);
    if(!valid){out.clear();return false;}
    HCRYPTPROV provider=0;
    HCRYPTHASH hash=0;
    const bool acquired=!!CryptAcquireContextW(&provider,nullptr,nullptr,
        PROV_RSA_AES,CRYPT_VERIFYCONTEXT);
    if(!acquired){out.clear();return false;}
    bool success=!!CryptCreateHash(provider,CALG_SHA1,0,0,&hash);
    const char prefix[]="blob 10597";
    if(success) success=!!CryptHashData(hash,
        reinterpret_cast<const BYTE*>(prefix),DWORD(sizeof(prefix)),0);
    if(success) success=!!CryptHashData(hash,out.data(),DWORD(out.size()),0);
    BYTE result[20]{};
    DWORD hashBytes=DWORD(sizeof(result));
    if(success) success=!!CryptGetHashParam(hash,HP_HASHVAL,
                                             result,&hashBytes,0);
    if(hash)CryptDestroyHash(hash);
    // Git SHA-1 proves repository object provenance; an independent
    // SHA-256 pins content against theoretical SHA-1 collisions.
    HCRYPTHASH sha256=0;
    if(success) success=!!CryptCreateHash(provider,CALG_SHA_256,0,0,&sha256);
    if(success) success=!!CryptHashData(sha256,out.data(),DWORD(out.size()),0);
    BYTE contentDigest[32]{};
    DWORD digestBytes=DWORD(sizeof(contentDigest));
    if(success) success=!!CryptGetHashParam(sha256,HP_HASHVAL,contentDigest,&digestBytes,0);
    if(sha256)CryptDestroyHash(sha256);
    CryptReleaseContext(provider,0);
    const BYTE expectedSha256[32]={
        0xd8,0x74,0x94,0x8c,0x2e,0x6f,0x21,0x8a,
        0x45,0xc7,0x40,0xa2,0x70,0xfe,0x5e,0x10,
        0xf0,0x34,0x4c,0x8f,0x3a,0x52,0x5b,0x59,
        0xfd,0x44,0xfd,0xce,0x89,0x8d,0xcc,0x95};
    success=success&&digestBytes==32 &&
        std::memcmp(expectedSha256,contentDigest,sizeof(expectedSha256))==0;
    const BYTE expected[20]={
        0x12,0x6b,0x24,0x78,0x0e,0x8c,0x36,0xa0,0xe1,0x52,
        0x1d,0xf9,0x82,0x06,0x7a,0xb1,0x2f,0x97,0x14,0x70};
    success=success&&hashBytes==20 &&
        std::memcmp(expected,result,sizeof(expected))==0;
    if(!success)out.clear();
    return success;
}
// Full-frame digest is computed ONLY after the adjacent SHA-pinned public
// fixture was verified. A bounded RGB buffer is never printed or written.
// Its geometry/digest makes a Photos-style orientation or partial-row
// corruption distinguishable from a successful 16x8 corner read.
void DigestPinnedSyntheticFullFrame(IWICBitmapSource* rgb,UINT w,UINT h,
                                   const char* role,bool automatic) {
    if(!rgb||!w||!h||w>20000||h>20000)return;
    const unsigned long long stride=static_cast<unsigned long long>(w)*3ull;
    const unsigned long long bytes=stride*h;
    if(stride>0xFFFFFFFFull||bytes>64ull*1024*1024ull)return;
    std::vector<BYTE> pixels(static_cast<size_t>(bytes));
    const HRESULT hr=rgb->CopyPixels(nullptr,UINT(stride),
        UINT(bytes),pixels.data());
    const char* prefix=automatic?"HEVC_SYNTHETIC_AUTO_FULLFRAME":
                                  "HEVC_SYNTHETIC_FULLFRAME";
    std::printf("%s role=%s stage=copy_pixels hr=0x%08lX width=%u height=%u bytes=%llu\n",
        prefix,role,static_cast<unsigned long>(hr),w,h,bytes);
    if(SUCCEEDED(hr)) {
        unsigned long long fingerprint=14695981039346656037ull;
        for(BYTE sample:pixels){fingerprint^=sample;fingerprint*=1099511628211ull;}
        std::printf("%s role=%s stage=rgb24_fnv64 value=%016llX\n",
            prefix,role,fingerprint);
    }
}
void DecodePinnedSynthetic(IWICImagingFactory* factory,
    const CLSID& decoderClsid,const char* role,std::vector<BYTE>& source) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICComponentInfo> info;
    HRESULT hr=factory->CreateComponentInfo(decoderClsid,info.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_WIC role=%s stage=info hr=0x%08lX\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr))return;
    ComPtr<IWICBitmapDecoderInfo> decoderInfo;
    hr=info.As(&decoderInfo);
    if(FAILED(hr))return;
    ComPtr<IWICBitmapDecoder> decoder;
    hr=decoderInfo->CreateInstance(decoder.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_WIC role=%s stage=create hr=0x%08lX\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr))return;
    ComPtr<IWICStream> stream;
    hr=factory->CreateStream(stream.GetAddressOf());
    if(SUCCEEDED(hr))
        hr=stream->InitializeFromMemory(source.data(),UINT(source.size()));
    if(FAILED(hr))return;
    hr=decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand);
    std::printf("HEVC_SYNTHETIC_WIC role=%s stage=initialize hr=0x%08lX\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr))return;
    ComPtr<IWICBitmapFrameDecode> frame;
    hr=decoder->GetFrame(0,frame.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_WIC role=%s stage=frame0 hr=0x%08lX\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr))return;
    UINT width=0,height=0;
    hr=frame->GetSize(&width,&height);
    std::printf("HEVC_SYNTHETIC_WIC role=%s stage=frame_size hr=0x%08lX width=%u height=%u\n",
        role,static_cast<unsigned long>(hr),width,height);
    if(FAILED(hr)||width<16||height<8)return;
    ComPtr<IWICBitmapSource> rgb;
    hr=WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,
        frame.Get(),rgb.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_WIC role=%s stage=rgb24 hr=0x%08lX\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr))return;
    BYTE sample[16*8*3]{};
    WICRect roi{0,0,16,8};
    hr=rgb->CopyPixels(&roi,16*3,UINT(sizeof(sample)),sample);
    std::printf("HEVC_SYNTHETIC_WIC role=%s stage=copy_pixels hr=0x%08lX\n",
        role,static_cast<unsigned long>(hr));
    if(SUCCEEDED(hr)) {
        unsigned long long fp=14695981039346656037ull;
        for(BYTE b:sample){fp^=b;fp*=1099511628211ull;}
        std::printf("HEVC_SYNTHETIC_WIC role=%s stage=rgb24_roi fnv64=%016llX\n",
            role,fp);
        DigestPinnedSyntheticFullFrame(rgb.Get(),width,height,role,false);
    }
}
// Investigate a missing Windows 10 Photos save precondition: is the native
// HEIF encoder merely COM-instantiable, or can it accept the SAME verified
// original HEIC frame that the automatically selected WIC decoder reads?
// This is a synthetic, opt-in, memory-only operation. Never save a file,
// modify the HEIF fixture, query user pictures or change WIC registration.
// A successful Commit is NOT proof of pixel/EXIF/ICC preservation.
void ProbePinnedSyntheticEncoderMemoryOnly(
    IWICImagingFactory* factory,const GUID& container,const char* role,
    IWICBitmapFrameDecode* source,const std::vector<BYTE>& original) {
    using Microsoft::WRL::ComPtr;
    if(!factory || !source || original.empty())return;
    ComPtr<IWICBitmapEncoder> encoder;
    HRESULT hr=factory->CreateEncoder(container,nullptr,encoder.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=create hr=0x%08lX\\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr)||!encoder)return;
    ComPtr<IStream> output;
    hr=CreateStreamOnHGlobal(nullptr,TRUE,output.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=memory_stream hr=0x%08lX\\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr)||!output)return;
    hr=encoder->Initialize(output.Get(),WICBitmapEncoderNoCache);
    std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=initialize hr=0x%08lX\\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr))return;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    hr=encoder->CreateNewFrame(frame.GetAddressOf(),properties.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=create_frame hr=0x%08lX\\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr)||!frame)return;
    hr=frame->Initialize(properties.Get());
    std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=frame_initialize hr=0x%08lX\\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr))return;
    ComPtr<IWICMetadataQueryWriter> metadata;
    const HRESULT writer=frame->GetMetadataQueryWriter(metadata.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=metadata_writer hr=0x%08lX\\n",
        role,static_cast<unsigned long>(writer));
    // No raster flattening or caller-specified resize. The input is the
    // unchanged, hash-pinned public WIC frame, not Photos' edited pixels.
    hr=frame->WriteSource(source,nullptr);
    std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=write_source hr=0x%08lX\\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr))return;
    hr=frame->Commit();
    std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=frame_commit hr=0x%08lX\\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr))return;
    hr=encoder->Commit();
    std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=encoder_commit hr=0x%08lX\\n",
        role,static_cast<unsigned long>(hr));
    if(FAILED(hr))return;
    STATSTG stat{};
    hr=output->Stat(&stat,STATFLAG_NONAME);
    if(FAILED(hr)||stat.cbSize.QuadPart==0 ||
       stat.cbSize.QuadPart>32ull*1024ull*1024ull) {
        std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=output_bounds hr=0x%08lX within_limit=0\\n",
            role,static_cast<unsigned long>(hr));
        return;
    }
    LARGE_INTEGER begin{};
    hr=output->Seek(begin,STREAM_SEEK_SET,nullptr);
    if(FAILED(hr))return;
    const size_t count=static_cast<size_t>(stat.cbSize.QuadPart);
    std::vector<BYTE> bytes(count);
    ULONG received=0;
    hr=output->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&received);
    const bool exact=SUCCEEDED(hr) && received==bytes.size() &&
                     bytes==original;
    std::printf("HEVC_SYNTHETIC_ENCODER role=%s stage=readback hr=0x%08lX bytes=%llu exact_original_bytes=%u photos_not_tested=1\\n",
        role,static_cast<unsigned long>(hr),
        static_cast<unsigned long long>(count),static_cast<unsigned>(exact));
}

// This is the pathway WIC callers commonly use without an explicit vendor.
// Both Microsoft and custom HEIF decoders may be registered; COM activation
// does not guarantee CopyPixels, as observed on the user's Windows 10.
// Report the selected CLSID and exact image-read error without changing any
// registry priority, writing to files, or querying user images.
void DecodePinnedSyntheticAutomatic(IWICImagingFactory* factory,
                                    std::vector<BYTE>& source) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICStream> stream;
    HRESULT hr=factory->CreateStream(stream.GetAddressOf());
    if(SUCCEEDED(hr))
        hr=stream->InitializeFromMemory(source.data(),UINT(source.size()));
    if(FAILED(hr)) {
        std::printf("HEVC_SYNTHETIC_AUTO stage=memory_stream hr=0x%08lX\n",
            static_cast<unsigned long>(hr));
        return;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    hr=factory->CreateDecoderFromStream(stream.Get(),nullptr,
        WICDecodeMetadataCacheOnDemand,decoder.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_AUTO stage=create_decoder hr=0x%08lX\n",
        static_cast<unsigned long>(hr));
    if(FAILED(hr)||!decoder)return;
    ComPtr<IWICBitmapDecoderInfo> info;
    hr=decoder->GetDecoderInfo(info.GetAddressOf());
    CLSID selected{};
    HRESULT clsidHr=FAILED(hr)?hr:info->GetCLSID(&selected);
    std::printf("HEVC_SYNTHETIC_AUTO stage=decoder_clsid hr=0x%08lX custom=%u microsoft=%u\n",
        static_cast<unsigned long>(clsidHr),
        static_cast<unsigned>(SUCCEEDED(clsidHr)&&IsEqualGUID(selected,kCustomDecoder)),
        static_cast<unsigned>(SUCCEEDED(clsidHr)&&IsEqualGUID(selected,kMicrosoftDecoder)));
    ComPtr<IWICBitmapFrameDecode> frame;
    hr=decoder->GetFrame(0,frame.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_AUTO stage=frame0 hr=0x%08lX\n",
        static_cast<unsigned long>(hr));
    if(FAILED(hr)||!frame)return;
    UINT w=0,h=0;
    hr=frame->GetSize(&w,&h);
    std::printf("HEVC_SYNTHETIC_AUTO stage=frame_size hr=0x%08lX width=%u height=%u\n",
        static_cast<unsigned long>(hr),w,h);
    if(FAILED(hr)||w<16||h<8)return;
    // Compare standard Microsoft HEIF vs our private HEIC encoder with
    // the identical ORIGINAL decoded WIC frame, memory stream only.
    ProbePinnedSyntheticEncoderMemoryOnly(factory,kNativeHeif,
        "microsoft_heif",frame.Get(),source);
    ProbePinnedSyntheticEncoderMemoryOnly(factory,kPrivateHeic,
        "custom_heic",frame.Get(),source);
    ComPtr<IWICBitmapSource> rgb;
    hr=WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,
        frame.Get(),rgb.GetAddressOf());
    std::printf("HEVC_SYNTHETIC_AUTO stage=rgb24 hr=0x%08lX\n",
        static_cast<unsigned long>(hr));
    if(FAILED(hr)||!rgb)return;
    BYTE sample[16*8*3]{};
    WICRect roi{0,0,16,8};
    hr=rgb->CopyPixels(&roi,16*3,UINT(sizeof(sample)),sample);
    std::printf("HEVC_SYNTHETIC_AUTO stage=copy_pixels hr=0x%08lX\n",
        static_cast<unsigned long>(hr));
    if(SUCCEEDED(hr)) {
        unsigned long long fp=14695981039346656037ull;
        for(BYTE c:sample){fp^=c;fp*=1099511628211ull;}
        std::printf("HEVC_SYNTHETIC_AUTO stage=rgb24_roi fnv64=%016llX\n",fp);
        DigestPinnedSyntheticFullFrame(rgb.Get(),w,h,"selected",true);
    }
}
int RunPinnedSynthetic() {
    std::vector<BYTE> publicFixture;
    if(!VerifySyntheticFixture(publicFixture)) {
        std::puts("HEVC_SYNTHETIC_REFUSED public_fixture_missing_or_hash_mismatch=1");
        return 8;
    }
    std::puts("HEVC_SYNTHETIC_GIT_BLOB_VERIFIED_PASS bytes=10597");
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        reinterpret_cast<void**>(factory.GetAddressOf()));
    if(FAILED(hr))return 9;
    DecodePinnedSynthetic(factory.Get(),kMicrosoftDecoder,
        "microsoft_heif",publicFixture);
    DecodePinnedSynthetic(factory.Get(),kCustomDecoder,
        "custom_heic",publicFixture);
    DecodePinnedSyntheticAutomatic(factory.Get(),publicFixture);
    std::puts("HEVC_SYNTHETIC_WIC_COMPARISON_COMPLETE photos_not_tested=1 source_unchanged=1 no_photo_path_input=1");
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    const bool activation=(argc==2 && argv && argv[1] &&
        std::wcscmp(argv[1],L"--wic-activate-no-image")==0);
    const bool synthetic=(argc==2 && argv && argv[1] &&
        std::wcscmp(argv[1],L"--synthetic-wic-test")==0);
    if (argc!=1 && !activation && !synthetic) {
        std::puts("HEVC_MF_INVENTORY_REFUSED arguments_not_allowed=1");
        return 2;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
        std::printf("HEVC_MF_PROBE_STATUS stage=com hr=0x%08lX\n",
                    static_cast<unsigned long>(com));
        return 3;
    }
    if(synthetic) {
        const int result=RunPinnedSynthetic();
        CoUninitialize();
        return result;
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
