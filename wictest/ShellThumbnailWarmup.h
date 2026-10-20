#pragma once
// Bounded Windows Shell thumbnail warmup, intentionally OUTSIDE the WIC DLL.
// Does not touch registry, association, original file contents, or thumbnail
// cache database directly. The Windows IThumbnailCache service owns its cache.
// Keep COM work per-thread and worker concurrency <=2 for low-powered PCs.
#include <thumbcache.h>
#include <shlobj.h>
#include <windows.h>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>
#include <cwctype>

namespace ShellThumbnailWarmup {
struct Result {
    std::atomic<unsigned> cacheHits{0}, generated{0}, failed{0};
    std::atomic<unsigned> attempted{0};
};
inline bool HeicExtension(const std::wstring& name) {
    const size_t dot=name.find_last_of(L'.');
    if (dot==std::wstring::npos) return false;
    std::wstring ext=name.substr(dot);
    for(auto& ch:ext) ch=static_cast<wchar_t>(towlower(ch));
    return ext==L".heic" || ext==L".heif";
}
// Intentionally one directory level; no wildcard traversal into junctions.
// This is used only when an explicit directory is provided to a future UI.
inline std::vector<std::wstring> CollectFolder(const std::wstring& dir,
                                                size_t limit=30) {
    std::vector<std::wstring> out;
    if (dir.empty() || limit==0) return out;
    limit=(std::min)(limit,size_t(30));
    const DWORD rootAttr=GetFileAttributesW(dir.c_str());
    if(rootAttr==INVALID_FILE_ATTRIBUTES || !(rootAttr&FILE_ATTRIBUTE_DIRECTORY) ||
       (rootAttr&FILE_ATTRIBUTE_REPARSE_POINT)) return out;
    const std::wstring pattern=dir+L"\\*";
    WIN32_FIND_DATAW found{};
    HANDLE search=FindFirstFileW(pattern.c_str(),&found);
    if(search==INVALID_HANDLE_VALUE) return out;
    do {
        if((found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))!=0)
            continue;
        if(HeicExtension(found.cFileName)) {
            out.emplace_back(dir+L"\\"+found.cFileName);
            if(out.size()>=limit)break;
        }
    } while(FindNextFileW(search,&found));
    FindClose(search);
    return out;
}

inline void WarmPaths(const std::vector<std::wstring>& paths,
                      unsigned workers, Result& result,
                      std::atomic<bool>* cancel=nullptr,
                      bool background=true) {
    if(paths.empty())return;
    workers=(std::min)(2u,(std::max)(1u,workers));
    std::atomic<size_t> next{0};
    const auto work=[&]() {
        HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(co)) { result.failed.fetch_add(1); return; }
        // Disk I/O and CPU pressure of the requesting worker stay low.
        const bool bg=background &&
                      SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN)!=0;
        if(background && !bg) SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        CComPtr<IThumbnailCache> cache;
        const HRESULT cacheHr=CoCreateInstance(CLSID_LocalThumbnailCache,nullptr,
                                               CLSCTX_ALL,IID_PPV_ARGS(&cache));
        if(FAILED(cacheHr) || !cache) {
            result.failed.fetch_add(1);
        } else {
            while(!cancel || !cancel->load()) {
                const size_t idx=next.fetch_add(1);
                if(idx>=paths.size())break;
                ++result.attempted;
                CComPtr<IShellItem> item;
                HRESULT hr=SHCreateItemFromParsingName(paths[idx].c_str(),nullptr,
                                                       IID_PPV_ARGS(&item));
                if(FAILED(hr)) {++result.failed;continue;}
                CComPtr<ISharedBitmap> bmp;
                WTS_CACHEFLAGS flags=WTS_DEFAULT;
                hr=cache->GetThumbnail(item,256,WTS_INCACHEONLY,
                                       &bmp,&flags,nullptr);
                if(SUCCEEDED(hr) && bmp) {
                    ++result.cacheHits;
                    continue;
                }
                bmp.Release();
                flags=WTS_DEFAULT;
                hr=cache->GetThumbnail(item,256,WTS_EXTRACT,&bmp,&flags,nullptr);
                if(SUCCEEDED(hr) && bmp) ++result.generated;
                else ++result.failed;
            }
        }
        if(bg)SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_END);
        CoUninitialize();
    };
    std::thread first(work);
    if(workers==2) {
        std::thread second(work);
        first.join(); second.join();
    } else first.join();
}
} // namespace ShellThumbnailWarmup
