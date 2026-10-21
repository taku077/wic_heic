# CI-only patch to pinned libde265 submodule. No system registration or MSI.
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'Only allowed in disposable GitHub Actions runner'}
$path='wic_heic/3rdparty/libde265/libde265/x86/sse.cc'
$source=Get-Content -LiteralPath $path -Raw
$source=$source -replace '\r\n',[string][char]10
$method=$source.IndexOf('void init_acceleration_functions_avx2(')
if($method -lt 0){throw 'Pinned libde265 dispatch method missing'}
$brace=$source.IndexOf('{',$method)
$feature=$source.IndexOf('#if HAVE_AVX2',$method)
if($brace -lt 0 -or $feature -lt $brace -or ($feature-$brace) -gt 40){
  throw 'Unexpected AVX2 compile-time guard in pinned libde265'
}
$insertAt=$feature+'#if HAVE_AVX2'.Length
if($source.Contains('WIC_HEIC_CI_AVX2_MSVC_DISPATCH')){throw 'CI patch already applied'}
$insertion=@'

#if defined(_MSC_VER)
// WIC_HEIC_CI_AVX2_MSVC_DISPATCH: CPU feature + OS AVX/YMM state guard.
  int info[4] = {0,0,0,0};
  __cpuid(info,0);
  const int maxLeaf = info[0];
  if (maxLeaf >= 7) {
    __cpuid(info,1);
    const bool osxsave = (info[2] & (1<<27)) != 0;
    const bool avx = (info[2] & (1<<28)) != 0;
    if (osxsave && avx && (_xgetbv(0) & 0x6) == 0x6) {
      __cpuidex(info,7,0);
      if ((info[1] & (1<<5)) != 0) {
        accel->transform_add_8[2] = transform_16x16_add_8_avx2;
        accel->transform_add_8[3] = transform_32x32_add_8_avx2;
      }
    }
  }
#endif
'@
$patched=$source.Insert($insertAt,$insertion)
if($patched -eq $source){throw 'No patch change'}
[IO.File]::WriteAllText((Resolve-Path $path).Path,$patched,[Text.UTF8Encoding]::new($false))
# Pinned AVX2 transform table used GCC __attribute__ unconditionally,
# which MSVC does not accept. Adapt only its alignment macro.
$tables='wic_heic/3rdparty/libde265/libde265/x86/transform-dct-tables.h'
$header=(Get-Content -LiteralPath $tables -Raw) -replace '\r\n',[string][char]10
$oldLine='#define ALIGNED_64(decl) decl __attribute__((aligned(64)))'
$newLine=@'
#if defined(_MSC_VER)
#define ALIGNED_64(decl) __declspec(align(64)) decl
#else
#define ALIGNED_64(decl) decl __attribute__((aligned(64)))
#endif
'@
if(-not $header.Contains($oldLine)){throw 'Pinned transform alignment definition changed'}
$header=$header.Replace($oldLine,$newLine)
[IO.File]::WriteAllText((Resolve-Path $tables).Path,$header,[Text.UTF8Encoding]::new($false))
Write-Host 'WIC_HEIC_CI_AVX2_MSVC_ALIGNED_64_PATCH_APPLIED'
Write-Host 'WIC_HEIC_CI_AVX2_DISPATCH_PATCH_APPLIED_RUNTIME_GUARDED'
