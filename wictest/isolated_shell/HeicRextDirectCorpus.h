// Research-only no-registration RExt HEIC direct COM/WIC comparison.
// Include after ShellEmbeddedDirect.cpp's definitions; uses its Capture.
// All source files are a seven-entry allowlist under GitHub workspace.
// Private codec and provider DLLs are ONLY loaded from RUNNER_TEMP.
// Source images never written, byte-for-byte output parity required.
// No user photos, Explorer registration, MSI, or system WIC modifications.
#pragma once
#include <cstring>
#include <limits>
namespace RextDirectCorpus {
using Bytes=std::vector<BYTE>;
using Clock=std::chrono::steady_clock;
using PrivateEntry=HRESULT(STDAPICALLTYPE*)(REFCLSID,REFIID,LPVOID*);
using ResetFn=void(__stdcall*)();
using SnapshotFn=void(__stdcall*)(unsigned long long*,unsigned long long*,
                                  unsigned long long*,unsigned long long*);
using LookupFlagFn=unsigned(__stdcall*)();
struct Stages{
 double hevcRgbMs=0,rawWicCopyMs=0;
 unsigned long long hevcCalls=0,rawCopyCalls=0;
};
inline Stages Snapshot(SnapshotFn function){
 unsigned long long d=0,c=0,dc=0,cc=0;
 function(&d,&c,&dc,&cc);
 std::printf("HEIC_REXT_TIMING_COUNTERS decoder_calls=%llu raw_copy_calls=%llu decode_us=%llu raw_copy_us=%llu\n",
   dc,cc,d,c);
 // WIC Fant may request narrow scanline stripes many times: 1080
 // CopyPixels calls for one 1920x1080 RExt fixture is normal. Actual HEVC
 // decoding must occur once on a fresh private WIC frame.
 Require(dc==1 && cc>=1 && cc<=65536,
         "RExt frame must decode once and allow WIC striped CopyPixels calls");
 Stages result;
 result.hevcRgbMs=double(d)/1000.;
 result.rawWicCopyMs=double(c)/1000.;
 result.hevcCalls=dc;result.rawCopyCalls=cc;
 return result;
}
inline void CheckHr(HRESULT hr,const char* stage){
 if(FAILED(hr)) {
  std::printf("REXT_PROVIDER_FAIL stage=%s hr=0x%08X\n",stage,unsigned(hr));
  throw std::runtime_error(stage);
 }
}
inline bool IsExactCiDirectory(const std::wstring& p,
                               const std::wstring& expected){
 if(p!=expected || p.empty())return false;
 const DWORD attr=GetFileAttributesW(p.c_str());
 return attr!=INVALID_FILE_ATTRIBUTES &&
         (attr&FILE_ATTRIBUTE_DIRECTORY)!=0 &&
         (attr&FILE_ATTRIBUTE_REPARSE_POINT)==0;
}
inline bool IsExactCiDll(const std::wstring& file){
 const DWORD a=GetFileAttributesW(file.c_str());
 return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY)==0 &&
       (a&FILE_ATTRIBUTE_REPARSE_POINT)==0;
}
struct Reference {
 Bytes pixels;
 UINT width=0,height=0;
 WICPixelFormatGUID originalFormat{};
 double elapsedMs=0, wicCopyPixelsMs=0;
};
inline Reference FromUnregisteredWic(PrivateEntry getFactory,
                                    const std::wstring& image,
                                    IWICImagingFactory* wic){
 const auto start=Clock::now();
 ComPtr<IClassFactory> factory;
 CheckHr(getFactory(CLSID_HEICDecoder,IID_IClassFactory,
         reinterpret_cast<void**>(factory.GetAddressOf())),
         "private HEIC decoder class factory");
 ComPtr<IWICBitmapDecoder> decoder;
 CheckHr(factory->CreateInstance(nullptr,IID_PPV_ARGS(decoder.GetAddressOf())),
         "private HEIC decoder");
 ComPtr<IWICStream> stream;
 CheckHr(wic->CreateStream(stream.GetAddressOf()),"private reference stream");
 CheckHr(stream->InitializeFromFilename(image.c_str(),GENERIC_READ),
         "read-only HEIC source open");
 CheckHr(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand),
         "private WIC decoder initialize");
 ComPtr<IWICBitmapFrameDecode> frame;
 CheckHr(decoder->GetFrame(0,frame.GetAddressOf()),"private WIC frame");
 UINT w=0,h=0;
 CheckHr(frame->GetSize(&w,&h),"private WIC image geometry");
 Require(w>0&&h>0&&w<=8192&&h<=8192,"invalid RExt source geometry");
 Reference reference;
 CheckHr(frame->GetPixelFormat(&reference.originalFormat),
         "private WIC original pixel format");
 const UINT largest=(std::max)(w,h);
 reference.width=(std::max)(1u,UINT(uint64_t(w)*256/largest));
 reference.height=(std::max)(1u,UINT(uint64_t(h)*256/largest));
 const uint64_t bytes=uint64_t(reference.width)*reference.height*4;
 Require(bytes>0&&bytes<=4*1024*1024,"RExt WIC output exceeds bounds");
 ComPtr<IWICBitmapScaler> scaler;
 CheckHr(wic->CreateBitmapScaler(scaler.GetAddressOf()),"WIC Fant scaler");
 CheckHr(scaler->Initialize(frame.Get(),reference.width,reference.height,
          WICBitmapInterpolationModeFant),"WIC Fant initialisation");
 ComPtr<IWICFormatConverter> converter;
 CheckHr(wic->CreateFormatConverter(converter.GetAddressOf()),"WIC PBGRA converter");
 CheckHr(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,
         WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),
         "WIC PBGRA initialise");
 reference.pixels.resize(size_t(bytes));
 const auto wicCopyStart=Clock::now();
 CheckHr(converter->CopyPixels(nullptr,reference.width*4,
         UINT(bytes),reference.pixels.data()),"WIC reference CopyPixels");
 const auto done=Clock::now();
 reference.wicCopyPixelsMs=std::chrono::duration<double,std::milli>(
   done-wicCopyStart).count();
 reference.elapsedMs=std::chrono::duration<double,std::milli>(done-start).count();
 return reference;
}
inline double Median(std::vector<double> samples) {
 Require(samples.size()==4,"RExt benchmark must have exactly four samples");
 std::sort(samples.begin(),samples.end());
 return (samples[1]+samples[2])/2;
}
inline void Run(const std::wstring& providerDll,
                const std::wstring& privateDll,
                const std::wstring& sourceDir){
 Require(Env(L"GITHUB_ACTIONS")==L"true","RExt test is GitHub CI only");
 const std::wstring runner=Env(L"RUNNER_TEMP");
 const std::wstring workspace=Env(L"GITHUB_WORKSPACE");
 Require(!runner.empty()&&!workspace.empty(),"CI paths missing");
 const auto staging=runner+L"\\heic_rext_direct_ab";
 const std::wstring scalarDll=staging+L"\\WicHeicPrivateCodecScalar.dll";
 Require(IsExactCiDirectory(staging,staging),"missing isolated staging folder");
 Require(sourceDir==workspace+L"\\wictest\\samples",
         "RExt source directory must be fixed public corpus");
 Require(providerDll==staging+L"\\WicHeicFastPreview.dll" &&
         privateDll==staging+L"\\WicHeicPrivateCodec.dll",
         "only isolated private DLL names permitted");
 Require(IsExactCiDll(providerDll)&&IsExactCiDll(privateDll)&&
         IsExactCiDll(scalarDll),
         "isolated provider or scalar/LUT codec missing/reparse point");
 CheckHr(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"RExt COM init");
 HMODULE provider=LoadLibraryExW(providerDll.c_str(),nullptr,
     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
 Require(provider!=nullptr,"unregistered provider load failed");
 HMODULE codec=LoadLibraryExW(privateDll.c_str(),nullptr,
     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
 Require(codec!=nullptr,"unregistered decoder load failed");
 HMODULE scalar=LoadLibraryExW(scalarDll.c_str(),nullptr,
     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
 Require(scalar!=nullptr,"unregistered scalar reference codec load failed");
 const auto entry=reinterpret_cast<FactoryFn>(
     GetProcAddress(provider,"DllGetClassObject"));
 const auto decoderEntry=reinterpret_cast<PrivateEntry>(
     GetProcAddress(codec,"DllGetClassObject"));
 Require(entry&&decoderEntry,"private COM class factory export missing");
 const auto reset=reinterpret_cast<ResetFn>(
   GetProcAddress(codec,"WicHeicRextTimingReset"));
 const auto snapshot=reinterpret_cast<SnapshotFn>(
   GetProcAddress(codec,"WicHeicRextTimingSnapshot"));
 Require(reset&&snapshot,
         "Research-only RExt timing exports absent: refuse uninstrumented test");
 const auto scalarEntry=reinterpret_cast<PrivateEntry>(
   GetProcAddress(scalar,"DllGetClassObject"));
 const auto scalarReset=reinterpret_cast<ResetFn>(
   GetProcAddress(scalar,"WicHeicRextTimingReset"));
 const auto scalarSnapshot=reinterpret_cast<SnapshotFn>(
   GetProcAddress(scalar,"WicHeicRextTimingSnapshot"));
 const auto scalarLookup=reinterpret_cast<LookupFlagFn>(
   GetProcAddress(scalar,"WicHeicRextLookupEnabled"));
 const auto candidateLookup=reinterpret_cast<LookupFlagFn>(
   GetProcAddress(codec,"WicHeicRextLookupEnabled"));
 Require(scalarEntry&&scalarReset&&scalarSnapshot&&scalarLookup&&candidateLookup &&
         scalarLookup()==0 && candidateLookup()==1,
         "A/B must compare actual scalar baseline and compiled LUT optimization");
 ComPtr<IWICImagingFactory> wic;
 CheckHr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
    IID_PPV_ARGS(wic.GetAddressOf())),"WIC imaging factory");
 constexpr const wchar_t* samples[]={
  L"03",L"04",L"05",L"06",L"07",
  L"fixture-irot3-exif6-noaux",L"fixture-irot3-exif6"};
 unsigned approved=0;
 for(const wchar_t* stem:samples) {
  const std::wstring file=sourceDir+L"\\"+stem+L".heic";
  const DWORD attr=GetFileAttributesW(file.c_str());
  Require(attr!=INVALID_FILE_ATTRIBUTES &&
    (attr&FILE_ATTRIBUTE_DIRECTORY)==0 &&
    (attr&FILE_ATTRIBUTE_REPARSE_POINT)==0,"unexpected HEIC file");
  std::vector<double> directTime,referenceTime;
  std::vector<double> directHevc,directRawCopy,directOther;
  std::vector<double> refHevc,refRawCopy,refWicOnly,refDownstream,refSetup;
  std::vector<double> baselineCopy,optimizedCopy,baselineTotal,optimizedTotal;
  UINT lastWidth=0,lastHeight=0;
  // ABBA-like alternation. Two separate implementations, both using the
  // same UNREGISTERED WIC decoder and the exact original Fant->PBGRA.
  for(int pass=0;pass<4;++pass){
   const bool referenceFirst=(pass==0||pass==3);
   Reference ref;
   Result direct;
   Stages directStages,refStages;
   if(referenceFirst) {
    reset();
    ref=FromUnregisteredWic(decoderEntry,file,wic.Get());
    refStages=Snapshot(snapshot);
    reset();
    direct=Capture(entry,file,256);
    directStages=Snapshot(snapshot);
   } else {
    reset();
    direct=Capture(entry,file,256);
    directStages=Snapshot(snapshot);
    reset();
    ref=FromUnregisteredWic(decoderEntry,file,wic.Get());
    refStages=Snapshot(snapshot);
   }
   const double refRest=ref.wicCopyPixelsMs-
                          refStages.hevcRgbMs-refStages.rawWicCopyMs;
   const double directRest=direct.elapsedMs-
                          directStages.hevcRgbMs-directStages.rawWicCopyMs;
   Require(refRest>=-2.0 && directRest>=-2.0 &&
           refStages.hevcRgbMs>=0 && directStages.hevcRgbMs>=0,
           "RExt stage timing is physically inconsistent");
   refHevc.push_back(refStages.hevcRgbMs);
   refRawCopy.push_back(refStages.rawWicCopyMs);
   refWicOnly.push_back(ref.wicCopyPixelsMs);
   refDownstream.push_back(refRest);
   refSetup.push_back(ref.elapsedMs-ref.wicCopyPixelsMs);
   directHevc.push_back(directStages.hevcRgbMs);
   directRawCopy.push_back(directStages.rawWicCopyMs);
   directOther.push_back(directRest);
   Require(ref.width==direct.width && ref.height==direct.height &&
     direct.pixels.size()==ref.pixels.size(),
     "RExt direct provider WIC dimensions or stride mismatch");
   // Capture() keeps native top-down DIB storage; check byte-for-byte.
   Require(direct.pixels==ref.pixels,
     "RExt direct provider failed 256px PBGRA byte parity");
   const bool hasAlpha=
    IsEqualGUID(ref.originalFormat,GUID_WICPixelFormat32bppRGBA)||
    IsEqualGUID(ref.originalFormat,GUID_WICPixelFormat64bppRGBA);
   // The isolated preview intentionally declares ARGB even for opaque
   // thumbnails to retain real Shell cache premultiplication semantics.
   // The alpha flag therefore cannot be inferred solely from source bits.
   Require(direct.alpha==WTSAT_ARGB ||
      (!hasAlpha && direct.alpha==WTSAT_RGB),
      "RExt WTS_ALPHATYPE flag incompatible with WIC alpha");
   if(!hasAlpha){
    for(size_t n=3;n<direct.pixels.size();n+=4)
     Require(direct.pixels[n]==255,
       "Opaque HEIC exported a non-opaque premultiplied alpha byte");
   }
   lastWidth=ref.width;lastHeight=ref.height;
   directTime.push_back(direct.elapsedMs);
   referenceTime.push_back(ref.elapsedMs);
  }
  // A second balanced ABBA checks whether the 10/12-bit exact LUT
  // actually beats scalar normalization. Two different private DLLs,
  // both NOT registered in Windows; output must be identical bytewise.
  for(int pass=0;pass<4;++pass){
   Reference base,opt;
   Stages baseStages,optStages;
   const bool scalarFirst=(pass==0||pass==3);
   if(scalarFirst){
    scalarReset();base=FromUnregisteredWic(scalarEntry,file,wic.Get());
    baseStages=Snapshot(scalarSnapshot);
    reset();opt=FromUnregisteredWic(decoderEntry,file,wic.Get());
    optStages=Snapshot(snapshot);
   }else{
    reset();opt=FromUnregisteredWic(decoderEntry,file,wic.Get());
    optStages=Snapshot(snapshot);
    scalarReset();base=FromUnregisteredWic(scalarEntry,file,wic.Get());
    baseStages=Snapshot(scalarSnapshot);
   }
   Require(base.width==opt.width && base.height==opt.height &&
           base.pixels==opt.pixels,
           "LUT WIC output differs from reference scalar WIC Fant pixels");
   baselineCopy.push_back(baseStages.rawWicCopyMs);
   optimizedCopy.push_back(optStages.rawWicCopyMs);
   baselineTotal.push_back(base.elapsedMs);
   optimizedTotal.push_back(opt.elapsedMs);
  }
  const double providerMs=Median(directTime);
  const double referenceMs=Median(referenceTime);
  std::string sample;
  for(const wchar_t* p=stem;*p;++p){
    Require(*p>0&&*p<127,"invalid public ASCII sample name");
    sample.push_back(char(*p));
  }
  std::printf("HEIC_REXT_DIRECT_AB sample=%s passes=4 exact_bytes_all_passes=1 provider_median_ms=%.3f private_wic_median_ms=%.3f dimensions=%ux%u no_registry=1\n",
    sample.c_str(),providerMs,referenceMs,
    unsigned(lastWidth),unsigned(lastHeight));
  std::printf("HEIC_REXT_STAGES sample=%s passes=4 provider_hevc_rgb_ms=%.3f provider_native_copy_ms=%.3f provider_other_ms=%.3f reference_hevc_rgb_ms=%.3f reference_native_copy_ms=%.3f reference_wic_copy_pixels_ms=%.3f reference_wic_downstream_ms=%.3f reference_setup_ms=%.3f data_scope=private_WIC_only source_unchanged=1\n",
    sample.c_str(),Median(directHevc),Median(directRawCopy),Median(directOther),
    Median(refHevc),Median(refRawCopy),Median(refWicOnly),
    Median(refDownstream),Median(refSetup));
  const double scalarCopyMs=Median(baselineCopy),lookupCopyMs=Median(optimizedCopy);
  std::printf("HEIC_REXT_LUT_AB sample=%s trials=4 scalar_native_copy_ms=%.3f lookup_native_copy_ms=%.3f saved_copy_pct=%.3f scalar_total_ms=%.3f lut_total_ms=%.3f pixel_bytes_identical_all_4=1 distinct_scalar_and_lut_binaries=1 production_enabled=0\n",
     sample.c_str(),scalarCopyMs,lookupCopyMs,
     scalarCopyMs>0?100.0*(1.0-lookupCopyMs/scalarCopyMs):0.0,
     Median(baselineTotal),Median(optimizedTotal));
  ++approved;
 }
 Require(approved==7,"seven RExt examples not covered");
 wic.Reset();
 FreeLibrary(scalar);
 FreeLibrary(codec);
 FreeLibrary(provider);
 CoUninitialize();
 std::puts("HEIC_REXT_DIRECT_PROVIDER_7_OF_7_WIC_FANT_PBGRA_EXACT_PASS");
 std::puts("HEIC_REXT_LUT_VS_SCALAR_7_OF_7_4PASS_WIC_BYTE_EXACT_PASS");
 std::puts("HEIC_REXT_DIRECT_RESEARCH_ONLY_NO_REGISTRY_NO_INSTALL_NO_MSI");
}
} // namespace RextDirectCorpus
