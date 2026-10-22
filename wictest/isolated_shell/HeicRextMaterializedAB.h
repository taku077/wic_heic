// CI-only alternative: materialize N-bit WIC Fant pixels once before
// PBGRA conversion. Compare to original scaler->converter chain byte-for-byte.
// No registration, no source writes, no product implementation changes.
#pragma once
namespace RextMaterialized {
using Clock=std::chrono::steady_clock;
using Entry=RextDirectCorpus::PrivateEntry;
using Setter=unsigned(__stdcall*)(unsigned);
struct Photo {
 std::vector<BYTE> pixels;
 UINT width=0,height=0;
 double wallMs=0;
};
Photo Render(Entry entry,const std::wstring& path,IWICImagingFactory* wic){
 const auto started=Clock::now();
 ComPtr<IClassFactory> factory;
 RextDirectCorpus::CheckHr(entry(CLSID_HEICDecoder,IID_IClassFactory,
    reinterpret_cast<void**>(factory.GetAddressOf())),
    "materialized CI private WIC factory");
 ComPtr<IWICBitmapDecoder> decoder;
 RextDirectCorpus::CheckHr(factory->CreateInstance(nullptr,IID_PPV_ARGS(decoder.GetAddressOf())),
    "materialized CI private decoder");
 ComPtr<IWICStream> stream;
 RextDirectCorpus::CheckHr(wic->CreateStream(stream.GetAddressOf()),"materialized CI WIC stream");
 RextDirectCorpus::CheckHr(stream->InitializeFromFilename(path.c_str(),GENERIC_READ),
    "materialized CI read-only HEIC");
 RextDirectCorpus::CheckHr(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand),
    "materialized CI decoder initialize");
 ComPtr<IWICBitmapFrameDecode> frame;
 RextDirectCorpus::CheckHr(decoder->GetFrame(0,frame.GetAddressOf()),
    "materialized CI get frame");
 UINT w=0,h=0;
 RextDirectCorpus::CheckHr(frame->GetSize(&w,&h),"materialized CI dimensions");
 Require(w>0 && h>0 && w<=8192 && h<=8192,"materialized CI size out of bounds");
 const UINT largest=(std::max)(w,h);
 const UINT tw=(std::max)(1u,UINT(uint64_t(w)*256/largest));
 const UINT th=(std::max)(1u,UINT(uint64_t(h)*256/largest));
 ComPtr<IWICBitmapScaler> scaler;
 RextDirectCorpus::CheckHr(wic->CreateBitmapScaler(scaler.GetAddressOf()),
    "materialized CI Fant create");
 RextDirectCorpus::CheckHr(scaler->Initialize(frame.Get(),tw,th,WICBitmapInterpolationModeFant),
    "materialized CI Fant init");
 WICPixelFormatGUID native{};
 RextDirectCorpus::CheckHr(scaler->GetPixelFormat(&native),
    "materialized CI source format");
 UINT channels=0;
 if(IsEqualGUID(native,GUID_WICPixelFormat48bppRGB))channels=6;
 else if(IsEqualGUID(native,GUID_WICPixelFormat64bppRGBA))channels=8;
 else if(IsEqualGUID(native,GUID_WICPixelFormat24bppRGB))channels=3;
 else if(IsEqualGUID(native,GUID_WICPixelFormat32bppRGBA))channels=4;
 Require(channels!=0,"materialized CI unsupported native scaler format");
 const uint64_t stride=uint64_t(tw)*channels;
 const uint64_t length=stride*th;
 Require(stride<=UINT_MAX&&length>0&&length<=4u*1024u*1024u,
    "materialized CI 256px raster memory bound");
 std::vector<BYTE> staging(size_t(length));
 RextDirectCorpus::CheckHr(scaler->CopyPixels(nullptr,UINT(stride),
    UINT(length),staging.data()),"materialized CI Fant pixels");
 ComPtr<IWICBitmap> cached;
 RextDirectCorpus::CheckHr(wic->CreateBitmapFromMemory(tw,th,native,
    UINT(stride),UINT(length),staging.data(),cached.GetAddressOf()),
    "materialized CI create native WIC bitmap");
 ComPtr<IWICFormatConverter> converter;
 RextDirectCorpus::CheckHr(wic->CreateFormatConverter(converter.GetAddressOf()),
    "materialized CI PBGRA converter");
 RextDirectCorpus::CheckHr(converter->Initialize(cached.Get(),
    GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,
    0,WICBitmapPaletteTypeCustom),"materialized CI PBGRA init");
 Photo out;
 out.width=tw;out.height=th;
 const uint64_t n=uint64_t(tw)*th*4;
 Require(n<=4u*1024u*1024u,"materialized CI PBGRA memory bound");
 out.pixels.resize(size_t(n));
 RextDirectCorpus::CheckHr(converter->CopyPixels(nullptr,tw*4,UINT(n),out.pixels.data()),
    "materialized CI PBGRA pixels");
 out.wallMs=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
 return out;
}
void Run(const std::wstring& scalarPath,const std::wstring& lutPath,
         const std::wstring& sampleDir){
 Require(Env(L"GITHUB_ACTIONS")==L"true"&&!Env(L"GITHUB_WORKSPACE").empty(),
   "Materialized alternatives must run in isolated CI only");
 const std::wstring temp=Env(L"RUNNER_TEMP"), workspace=Env(L"GITHUB_WORKSPACE");
 const std::wstring folder=temp+L"\\heic_rext_direct_ab";
 Require(!temp.empty()&&
         scalarPath==folder+L"\\WicHeicPrivateCodecScalar.dll"&&
         lutPath==folder+L"\\WicHeicPrivateCodec.dll"&&
         sampleDir==workspace+L"\\wictest\\samples"&&
         RextDirectCorpus::IsExactCiDll(scalarPath)&&
         RextDirectCorpus::IsExactCiDll(lutPath),
         "Materialized CI code may load only pinned private WIC DLLs");
 RextBatch::ComScope com;
 ComPtr<IWICImagingFactory> wic;
 RextDirectCorpus::CheckHr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
    CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.GetAddressOf())),
    "materialized CI WIC factory");
 for(unsigned mode=0;mode<2;++mode){
  const std::wstring dllPath=mode?lutPath:scalarPath;
  HMODULE dll=LoadLibraryExW(dllPath.c_str(),nullptr,
     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  Require(dll!=nullptr,"Materialized CI private DLL load");
  const auto entry=reinterpret_cast<Entry>(GetProcAddress(dll,"DllGetClassObject"));
  const auto set=reinterpret_cast<Setter>(
    GetProcAddress(dll,"WicHeicRextResearchSetWorkers"));
  Require(entry&&set&&set(mode?4u:1u)==1,"Materialized CI worker mode export");
  for(const wchar_t* stem:{L"03",L"04",L"fixture-irot3-exif6-noaux"}){
   const std::wstring photo=sampleDir+L"\\"+stem+L".heic";
   const DWORD attr=GetFileAttributesW(photo.c_str());
   Require(attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_REPARSE_POINT)&&
           !(attr&FILE_ATTRIBUTE_DIRECTORY),"Unapproved CI materialized input");
   std::vector<double> baseline,alternative;
   bool pixelsIdentical=true;
   for(unsigned rep=0;rep<4;++rep){
    // Flip mode order to reduce cache and boost bias. Each mode gets a fresh WIC frame.
    const bool alternativeFirst=(rep%2)==1;
    RextDirectCorpus::Reference normal;
    Photo materialized;
    if(alternativeFirst){
      materialized=Render(entry,photo,wic.Get());
      normal=RextDirectCorpus::FromUnregisteredWic(entry,photo,wic.Get());
    } else {
      normal=RextDirectCorpus::FromUnregisteredWic(entry,photo,wic.Get());
      materialized=Render(entry,photo,wic.Get());
    }
    baseline.push_back(normal.elapsedMs);
    alternative.push_back(materialized.wallMs);
    pixelsIdentical=pixelsIdentical && normal.width==materialized.width &&
      normal.height==materialized.height && normal.pixels==materialized.pixels;
   }
   const double b=RextSingleMatrix::Median4(baseline);
   const double a=RextSingleMatrix::Median4(alternative);
   const double delta=100.0*(1.0-a/b);
   std::printf("HEIC_REXT_MATERIALIZED_FANT_AB sample=%ls mode=%s repeats=4 original_wic_ms=%.3f materialized_wic_ms=%.3f wall_saved_pct=%.3f byte_exact=%u eligible_for_product=%u pixels_protected=1 no_msi=1 research_only=1\n",
      stem,mode?"lut4":"scalar1",b,a,delta,
      unsigned(pixelsIdentical),unsigned(pixelsIdentical&&delta>=5.0));
  }
  FreeLibrary(dll);
 }
 std::puts("HEIC_REXT_MATERIALIZED_FANT_COMPARISON_COMPLETED_POSSIBLE_REJECT_NOT_PRODUCT");
}
}
