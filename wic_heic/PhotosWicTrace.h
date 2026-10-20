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
                 const char* source="none",HRESULT result=S_OK) {
    wchar_t path[512]{};
    if(!TracePath(path))return;
    // APPEND only, never truncate or overwrite; do not auto-create folders.
    HANDLE h=CreateFileW(path,FILE_APPEND_DATA|FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER length{};
    if(!GetFileSizeEx(h,&length) || length.QuadPart >= 2*1024*1024){
        CloseHandle(h);
        return;
    }
    char line[384]{};
    // All string parameters MUST be trusted fixed literals from this DLL.
    const int n=sprintf_s(line,sizeof(line),
        "WIC_HEIC_TRACE v1 tick=%llu pid=%lu event=%s width=%u height=%u transform=%u compression=%u source=%s hr=0x%08X\r\n",
        static_cast<unsigned long long>(GetTickCount64()),
        static_cast<unsigned long>(GetCurrentProcessId()),event,
        static_cast<unsigned>(width),static_cast<unsigned>(height),
        static_cast<unsigned>(transform),static_cast<unsigned>(compression),
        source,static_cast<unsigned>(result));
    if(n>0 && n<int(sizeof(line))){
        DWORD written=0;
        WriteFile(h,line,static_cast<DWORD>(n),&written,nullptr);
    }
    CloseHandle(h);
}
} // namespace WicHeicDiagnostic
