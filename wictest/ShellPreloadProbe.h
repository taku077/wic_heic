#pragma once
// WinThumbsPreloader-inspired, isolated experimental benchmark.
// Uses Windows IThumbnailCache: warmed shell thumbnails are usable by Explorer,
// unlike a private raster-only conversion cache. No registry changes.
// Two workers max; test artifacts are copies of the repository's synthetic HEIC.
//
// NOTE: The runner may lack the full Explorer thumbnail infrastructure.
// "UNAVAILABLE" or zero cache hits is not success. This is not automatically
// enabled by the WIC DLL; no user image is ever scanned or modified.
#include <thumbcache.h>
#include <shlobj.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

namespace ShellPreloadProbe {
struct Counts {
    std::atomic<int> success{0}, cacheHits{0}, unavailable{0};
};
inline HRESULT One(const std::wstring& path, WTS_FLAGS flags, bool* cached) {
    if (cached) *cached=false;
    CComPtr<IThumbnailCache> thumbCache;
    HRESULT hr=CoCreateInstance(CLSID_LocalThumbnailCache,nullptr,CLSCTX_ALL,
                                IID_PPV_ARGS(&thumbCache));
    if (FAILED(hr)) return hr;
    CComPtr<IShellItem> item;
    hr=SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item));
    if (FAILED(hr)) return hr;
    CComPtr<ISharedBitmap> bitmap;
    WTS_CACHEFLAGS cacheFlags=WTS_DEFAULT;
    hr=thumbCache->GetThumbnail(item,256,flags,&bitmap,&cacheFlags,nullptr);
    if (SUCCEEDED(hr) && cached) *cached=((cacheFlags&WTS_CACHED)!=0);
    return hr;
}
inline std::wstring Absolute(const std::wstring& path) {
    DWORD needed=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);
    if (!needed || needed>=32768) throw std::runtime_error("preload absolute path length");
    std::vector<wchar_t> buffer(needed+1);
    DWORD written=GetFullPathNameW(path.c_str(),DWORD(buffer.size()),buffer.data(),nullptr);
    if (!written || written>=buffer.size()) throw std::runtime_error("preload absolute path");
    return std::wstring(buffer.data(),written);
}
inline void Run() {
    constexpr int perMode=10;
    const std::wstring source=Absolute(L"samples\\fixture-irot3-exif6-noaux.heic");
    const std::wstring dir=Absolute(L"samples\\preload-probe-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(GetTickCount64()));
    if(!CreateDirectoryW(dir.c_str(),nullptr))
        throw std::runtime_error("synthetic-only preload test directory");
    std::vector<std::wstring> allFiles;
    auto cleanup=[&](){
        for(const auto& path:allFiles) DeleteFileW(path.c_str());
        RemoveDirectoryW(dir.c_str());
    };
    try {
        for(int mode=0;mode!=2;++mode) {
            std::vector<std::wstring> paths;
            for(int i=0;i!=perMode;++i) {
                auto path=dir+L"\\synthetic-"+std::to_wstring(mode)+L"-"+
                          std::to_wstring(i)+L".heic";
                if(!CopyFileW(source.c_str(),path.c_str(),TRUE))
                    throw std::runtime_error("synthetic HEIC copy failed");
                paths.push_back(path);
                allFiles.push_back(path);
            }
            Counts result;
            const auto start=std::chrono::steady_clock::now();
            auto worker=[&](std::atomic<int>& next) {
                const HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
                if(FAILED(co)) { ++result.unavailable; return; }
                while(true) {
                    const int i=next.fetch_add(1);
                    if(i>=perMode)break;
                    bool wasCached=false;
                    HRESULT hr=One(paths[size_t(i)],WTS_EXTRACT,&wasCached);
                    if(SUCCEEDED(hr)) ++result.success;
                    else ++result.unavailable;
                    if(wasCached) ++result.cacheHits;
                }
                CoUninitialize();
            };
            std::atomic<int> next{0};
            if(mode==0) worker(next);
            else {
                // A deliberate 2-worker cap for low-power CPUs and HDD safety.
                std::thread second([&](){worker(next);});
                worker(next);
                second.join();
            }
            const auto duration=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-start).count();
            int verified=0;
            // Independent cache-only lookup checks whether Windows Shell now
            // recognizes the thumbnail, rather than just a private image buffer.
            for(const auto& path:paths) {
                bool cached=false;
                if(SUCCEEDED(One(path,WTS_INCACHEONLY,&cached)) && cached)
                    ++verified;
            }
            printf("SHELL_PRELOAD_%s files=%d extracted=%d cache_only_hits=%d errors=%d elapsed_ms=%.3f\n",
                mode==0?"SERIAL":"PARALLEL2",perMode,result.success.load(),
                verified,result.unavailable.load(),duration);
        }
        printf("SHELL_PRELOAD_EXPERIMENT_ONLY_NOT_EXPLORER_WIN10_VERIFIED\n");
    } catch (...) {cleanup();throw;}
    cleanup();
}
} // namespace ShellPreloadProbe
