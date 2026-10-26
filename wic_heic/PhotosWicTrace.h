#pragma once
// Opt-in trace for the EXPERIMENTAL Photos/WIC diagnostic build.
// Records ONLY numeric geometry, WIC option values, source kind and HRESULT.
// NEVER records filenames, paths, EXIF, HEIC bytes, application arguments,
// image pixels or directory names. No logging unless explicitly enabled.
// To capture app-installed Photos, HKCU\Software\WicHeic\TraceFile is read
// per invocation; the host app need not inherit an environment variable.
// Default is off. No registry writes are performed by this DLL.
#include <windows.h>
#include <string>
#include <cstdio>
#include <cwchar>
#pragma comment(lib, "Advapi32.lib")

namespace WicHeicDiagnostic {
inline bool EndsInTxt(const wchar_t* s) {
    const size_t n=wcslen(s);
    return n>=4 && _wcsicmp(s+n-4,L".txt")==0;
}
inline bool ApprovedLocalPath(const wchar_t* p) {
    // Only local absolute drive paths such as C:\Test\log.txt.
    // Never accept UNC, relative names or extended path/device namespace.
    const size_t n=wcslen(p);
    if(n<8 || n>=500 || !EndsInTxt(p))return false;
    if(!((p[0]>=L'A'&&p[0]<=L'Z') || (p[0]>=L'a'&&p[0]<=L'z')))
        return false;
    if(p[1]!=L':' || p[2]!=L'\\')return false;
    for(size_t i=3;i<n;i++)
        if(p[i]<32 || p[i]==L'/' || p[i]==L'"' ||
           p[i]==L'<' || p[i]==L'>' || p[i]==L'|' ||
           p[i]==L'*' || p[i]==L'?')
            return false;
    return true;
}
inline bool TracePath(wchar_t (&out)[512]) {
    out[0]=0;
    // Process-local env var is useful for the synthetic native CI harness.
    DWORD n=GetEnvironmentVariableW(L"WIC_HEIC_TRACE_FILE",out,512);
    if(n>0 && n<512 && ApprovedLocalPath(out))return true;
    out[0]=0;
    // HKCU is opt-in; never creates/changes keys, and is OFF by default.
    DWORD cb=sizeof(out);
    const LONG rc=RegGetValueW(HKEY_CURRENT_USER,L"Software\\WicHeic",
        L"TraceFile",RRF_RT_REG_SZ,nullptr,out,&cb);
    if(rc!=ERROR_SUCCESS)return false;
    out[511]=0;
    if(!ApprovedLocalPath(out))return false;
    // HKCU tracing expires automatically; a forgotten Stop command
    // can never leave persistent photo-edit diagnostics active.
    ULONGLONG deadline=0;
    DWORD nDeadline=sizeof(deadline);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\WicHeic",
        L"TraceUntilFileTime",RRF_RT_REG_QWORD,nullptr,
        &deadline,&nDeadline)!=ERROR_SUCCESS)return false;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const ULONGLONG now64=(ULONGLONG(now.dwHighDateTime)<<32)|now.dwLowDateTime;
    return deadline>now64;
}
inline void Emit(const char* event,UINT width=0,UINT height=0,
                 BYTE transform=0,BYTE compression=0,
                 const char* source="none",HRESULT result=S_OK,
                 UINT operation=0,ULONGLONG elapsedMs=0) {
    // Pixel consumers (including Photos' WIC flip/rotate) can issue many
    // thousands of one-scanline CopyPixels calls. Logging every one caused
    // 15,000+ entries and exhausted the 2-MiB cap BEFORE the encoder commit,
    // hiding precisely the save route that this diagnostic is meant to find.
    // Retain the first four calls and then 1/512 per process. Never sample
    // encoder, metadata, rejection, size, or commit events.
    if(lstrcmpA(event,"DecoderCopyPixels")==0) {
        static volatile LONG copyPixelsSequence=0;
        const LONG sequence=InterlockedIncrement(&copyPixelsSequence);
        if(sequence>4 && (static_cast<unsigned long>(sequence)%512UL)!=0UL)
            return;
    }
    wchar_t path[512]{};
    if(!TracePath(path))return;
    // APPEND only, never truncate or overwrite; do not auto-create folders.
    // Obtain a write handle and serialize writers across processes before
    // checking the cap. Never truncate or overwrite the existing log.
    HANDLE h=CreateFileW(path,GENERIC_WRITE|FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    // During burst saves the file-lock best-effort strategy can drop the
    // critical EncoderCommitEnter or EncoderCommitExit event while the
    // corresponding other boundary is logged. That makes elapsed-time and
    // overlap diagnostics unreliable. Serialize ONLY Commit boundaries;
    // continue to skip optional high-volume events rather than stall Photos.
    const bool commitBoundary=
        lstrcmpA(event,"EncoderCommitEnter")==0 ||
        lstrcmpA(event,"EncoderCommitExit")==0;
    // A busy Photos session can reach the 2 MiB hard cap and truncate a
    // Commit pair right between its Enter and Exit. Preserve a 512 KiB
    // reserve for numeric save boundaries and final outcomes without
    // increasing the cap, writing any user metadata, or slowing down the
    // high-frequency optional trace path.
    const bool savePriority=commitBoundary ||
        lstrcmpA(event,"EncoderCommitLossless")==0 ||
        lstrcmpA(event,"EncoderCommitRecompressed")==0 ||
        lstrcmpA(event,"EncoderCommitRejected")==0;
    const DWORD lockFlags=LOCKFILE_EXCLUSIVE_LOCK |
        (commitBoundary?0:LOCKFILE_FAIL_IMMEDIATELY);
    OVERLAPPED lock{};
    if(!LockFileEx(h,lockFlags,0,MAXDWORD,MAXDWORD,&lock)){
        CloseHandle(h);
        return;
    }
    char line[384]{};
    // Keep legacy v1 fields intact; append correlation-only numeric fields.
    // utc_filetime uses Windows UTC 100 ns units and aligns with the
    // read-only FileSystemWatcher timestamps without storing file paths.
    // operation is process-local (pair with pid), never a photo identifier.
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const ULONGLONG utcFileTime=
        (ULONGLONG(now.dwHighDateTime)<<32)|now.dwLowDateTime;
    // All string parameters MUST be trusted fixed literals from this DLL.
    const int n=sprintf_s(line,sizeof(line),
        "WIC_HEIC_TRACE v1 tick=%llu pid=%lu event=%s width=%u height=%u transform=%u compression=%u source=%s hr=0x%08X tid=%lu operation=%u elapsed_ms=%llu utc_filetime=%llu\r\n",
        static_cast<unsigned long long>(GetTickCount64()),
        static_cast<unsigned long>(GetCurrentProcessId()),event,
        static_cast<unsigned>(width),static_cast<unsigned>(height),
        static_cast<unsigned>(transform),static_cast<unsigned>(compression),
        source,static_cast<unsigned>(result),
        static_cast<unsigned long>(GetCurrentThreadId()),
        static_cast<unsigned>(operation),
        static_cast<unsigned long long>(elapsedMs),
        static_cast<unsigned long long>(utcFileTime));
    LARGE_INTEGER length{},zero{};
    if(n>0 && n<int(sizeof(line)) &&
       GetFileSizeEx(h,&length) && length.QuadPart>=0 &&
       static_cast<ULONGLONG>(length.QuadPart)+static_cast<ULONGLONG>(n)<=
          (savePriority?2ULL*1024*1024:3ULL*512*1024) &&
       SetFilePointerEx(h,zero,nullptr,FILE_END)){
        DWORD written=0;
        WriteFile(h,line,static_cast<DWORD>(n),&written,nullptr);
    }
    UnlockFileEx(h,0,MAXDWORD,MAXDWORD,&lock);
    CloseHandle(h);
}
} // namespace WicHeicDiagnostic
