// CI research: compare sequential vs bounded concurrent IThumbnailProvider
// generation using seven pinned repository RExt HEICs. Thread-per-worker
// COM initialized correctly, separate decoder per request, no registration.
// Measures warm-file-cache *batch* throughput, NOT Explorer cold-first paint.
#pragma once
#include <array>
#include <atomic>
#include <future>
#include <utility>
namespace RextBatch {
using Clock=std::chrono::steady_clock;
constexpr size_t kCount=7;
struct ComScope {
 bool active=false;
 ComScope(){
  const HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  Require(SUCCEEDED(hr),"Batch worker COM must be MTA");
  active=true;
 }
 ~ComScope(){if(active)CoUninitialize();}
 ComScope(const ComScope&)=delete;
 ComScope& operator=(const ComScope&)=delete;
};
struct Item {
 std::wstring name,file;
 std::vector<BYTE> pixels;
 UINT w=0,h=0;
};
double Median4(std::vector<double> v){
 Require(v.size()==4,"Exactly four batch trials expected");
 std::sort(v.begin(),v.end());
 return (v[1]+v[2])/2.;
}
void Run(const std::wstring& providerDll,const std::wstring& samplesPath){
 const std::wstring temp=Env(L"RUNNER_TEMP"),workspace=Env(L"GITHUB_WORKSPACE");
 Require(Env(L"GITHUB_ACTIONS")==L"true"&&!temp.empty()&&!workspace.empty(),
         "Batch trial is restricted to isolated GitHub Actions runner");
 const std::wstring dir=temp+L"\\heic_rext_direct_ab";
 Require(providerDll==dir+L"\\WicHeicFastPreview.dll" &&
         samplesPath==workspace+L"\\wictest\\samples",
         "Batch trial cannot inspect any arbitrary local HEIC");
 Require(RextDirectCorpus::IsExactCiDll(providerDll),
         "Research-only unregistered provider path invalid");
 ComScope mainCom;
 HMODULE provider=LoadLibraryExW(providerDll.c_str(),nullptr,
     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
 Require(provider!=nullptr,"Cannot open research Shell provider");
 const auto entry=reinterpret_cast<FactoryFn>(
      GetProcAddress(provider,"DllGetClassObject"));
 Require(entry!=nullptr,"Missing isolated IThumbnailProvider COM factory");
 constexpr const wchar_t* names[kCount]={
  L"03",L"04",L"05",L"06",L"07",
  L"fixture-irot3-exif6-noaux",L"fixture-irot3-exif6"};
 std::array<Item,kCount> items;
 for(size_t i=0;i<kCount;++i){
  items[i].name=names[i];
  items[i].file=samplesPath+L"\\"+names[i]+L".heic";
  const DWORD attributes=GetFileAttributesW(items[i].file.c_str());
  Require(attributes!=INVALID_FILE_ATTRIBUTES &&
          (attributes&FILE_ATTRIBUTE_DIRECTORY)==0 &&
          (attributes&FILE_ATTRIBUTE_REPARSE_POINT)==0,
          "Fixed HEIC source must be a regular file");
  const Result baseline=Capture(entry,items[i].file,256);
  Require(!baseline.pixels.empty(),"Missing deterministic RExt reference pixels");
  items[i].pixels=baseline.pixels;
  items[i].w=baseline.width;items[i].h=baseline.height;
 }
 std::printf("HEIC_REXT_BATCH_REFERENCES sample_count=7 strict_full_PBGRA_baselines=7 no_reencoding=1\n");
 for(unsigned workers:{1u,2u,4u}){
  std::vector<double> trials;
  for(unsigned repetition=0;repetition<4;++repetition){
   std::atomic<size_t> next{0};
   std::atomic<size_t> verified{0};
   const auto t0=Clock::now();
   std::vector<std::future<void>> tasks;
   tasks.reserve(workers);
   for(unsigned worker=0;worker<workers;++worker){
    tasks.emplace_back(std::async(std::launch::async,[&,worker](){
      ComScope com;
      for(;;){
       const size_t index=next.fetch_add(1);
       if(index>=items.size())break;
       const auto& item=items[index];
       Result result=Capture(entry,item.file,256);
       Require(result.width==item.w && result.height==item.h &&
               result.pixels==item.pixels,
               "Concurrent provider changed RExt WIC Fant/PBGRA bytes");
       verified.fetch_add(1);
      }
    }));
   }
   for(auto& task:tasks)task.get();
   Require(verified.load()==kCount,"Missing worker thumbnail or pixel parity");
   const double wall=std::chrono::duration<double,std::milli>(
        Clock::now()-t0).count();
   trials.push_back(wall);
   std::printf("HEIC_REXT_BATCH_TRIAL workers=%u repeat=%u source_count=7 warm_cache_wall_ms=%.3f byte_exact_count=7 provider_unregistered=1\n",
      workers,repetition,wall);
  }
  std::printf("HEIC_REXT_BATCH_MEDIAN workers=%u repeats=4 source_count=7 warm_cache_total_ms=%.3f byte_exact_all=1 explorer_first_paint_unverified=1 product_enabled=0\n",
    workers,Median4(trials));
 }
 FreeLibrary(provider);
 std::puts("HEIC_REXT_BATCH_1_2_4_WORKERS_ALL_7_HEIC_WIC_PIXEL_EXACT_PASS");
}
} // namespace RextBatch