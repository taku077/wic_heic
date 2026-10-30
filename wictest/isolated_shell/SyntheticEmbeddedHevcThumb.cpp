// CI-only disposable HEVC/HEIF with an actual embedded 320x240 thumbnail.
// The production encoder deliberately refuses SetThumbnail; do not change it
// merely to manufacture a fixture. This tool links pinned libheif statically.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <libheif/heif_cxx.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <stdexcept>
#include <string>
#pragma comment(lib,"heif.lib")
#pragma comment(lib,"libde265.lib")
#pragma comment(lib,"libkvazaar.lib")
namespace {
constexpr int kWidth=4032,kHeight=3024,kThumbBound=320;
std::wstring Env(const wchar_t* name) {
    wchar_t buf[32768]{};
    const DWORD n=GetEnvironmentVariableW(name,buf,32768);
    return (n>0&&n<32768)?std::wstring(buf,n):L"";
}
void Require(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(reason);
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        Require(argc==2 || argc==3,"Expected output path and optional --without-thumbnail");
        const bool noThumb=argc==3 && wcscmp(argv[2],L"--without-thumbnail")==0;
        const bool withIcc=argc==3 && wcscmp(argv[2],L"--with-icc")==0;
        const bool withAlpha=argc==3 && wcscmp(argv[2],L"--with-alpha")==0;
        Require(argc==2 || noThumb || withIcc || withAlpha,"Invalid optional mode");
        Require(Env(L"GITHUB_ACTIONS")==L"true","Test generation is CI only");
        const auto root=Env(L"RUNNER_TEMP");
        Require(!root.empty(),"No CI runner temporary folder");
        const std::wstring exact=root+(noThumb?L"\\wicfast_no_thumb_4032x3024.heic":
                 withIcc?L"\\wicfast_with_icc_4032x3024.heic":
                 withAlpha?L"\\wicfast_with_alpha_4032x3024.heic":
                         L"\\wicfast_embedded_4032x3024.heic");
        Require(_wcsicmp(argv[1],exact.c_str())==0,"Refuse non-CI output path");
        Require(GetFileAttributesW(argv[1])==INVALID_FILE_ATTRIBUTES,
                "Refuse to overwrite existing HEIC");
        heif::Image source;
        source.create(kWidth,kHeight,heif_colorspace_RGB,
                      withAlpha?heif_chroma_interleaved_RGBA:heif_chroma_interleaved_RGB);
        source.add_plane(heif_channel_interleaved,kWidth,kHeight,8);
        size_t stride=0;
        uint8_t* pixels=source.get_plane2(heif_channel_interleaved,&stride);
        const unsigned channels=withAlpha?4u:3u;
        Require(pixels&&stride>=size_t(kWidth)*channels,"RGB(A) input plane invalid");
        // Easily recognized four-quadrant synthetic pattern. Never personal photos.
        const uint8_t colors[4][3]={
            {10,30,240},{220,200,30},{25,190,80},{200,50,190}
        };
        for(int y=0;y<kHeight;++y) {
            auto* row=pixels+size_t(y)*stride;
            for(int x=0;x<kWidth;++x) {
                const int q=(y>=kHeight/2?2:0)+(x>=kWidth/2?1:0);
                row[channels*x+0]=colors[q][0];
                row[channels*x+1]=colors[q][1];
                row[channels*x+2]=colors[q][2];
                if(withAlpha)row[channels*x+3]=uint8_t(64+((x/17+y/19)%192));
            }
        }
        if(withIcc) {
            const std::filesystem::path profile=L"wictest/samples/Synthetic_sRGB_ICC_from_LittleCMS.icc";
            std::ifstream icc(profile,std::ios::binary);
            Require(icc.good(),"Synthetic sRGB ICC test fixture missing");
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(icc)),
                                              std::istreambuf_iterator<char>());
            Require(bytes.size()>=132 && bytes.size()<=8192,"Unexpected ICC fixture size");
            source.set_raw_color_profile(heif_color_profile_type_prof,bytes);
        }
        heif::Context ctx;
        heif::Encoder encoder(heif_compression_HEVC);
        encoder.set_lossy_quality(55);
        heif::Context::EncodingOptions options{};
        auto master=ctx.encode_image(source,encoder,options);
        Require(!master.empty(),"HEVC main handle missing");
        if(!noThumb){
            auto thumb=ctx.encode_thumbnail(source,master,encoder,options,kThumbBound);
            Require(!thumb.empty(),"HEVC thumbnail handle missing");
            Require(thumb.get_width()==320 && thumb.get_height()==240,
                    "HEVC embedded thumbnail dimensions unexpected");
        }
        const std::string filename=std::filesystem::path(argv[1]).string();
        ctx.write_to_file(filename);
        // Readback checks real saved container associations, not only a handle.
        heif::Context verify;
        verify.read_from_file(filename);
        auto primary=verify.get_primary_image_handle();
        const auto ids=primary.get_list_of_thumbnail_IDs();
        if(withAlpha){
            Require(heif_image_handle_has_alpha_channel(primary.get_raw_image_handle())!=0,
                    "Encoded synthetic alpha was lost");
        }
        if(withIcc){
            Require(heif_image_handle_get_raw_color_profile_size(primary.get_raw_image_handle())>=132,
                    "ICC unexpectedly discarded in encoded primary HEIF item");
        }
        Require(primary.get_width()==kWidth&&primary.get_height()==kHeight,
                "HEIF primary geometry changed");
        if(noThumb){Require(ids.empty(),"Missing-thumbnail fixture unexpectedly had a thumbnail");}
        else {
            Require(!ids.empty(),"HEIF thumbnail relation missing after write");
            auto savedThumb=primary.get_thumbnail(ids[0]);
            Require(savedThumb.get_width()==320&&savedThumb.get_height()==240,
                    "HEIF saved thumbnail geometry changed");
        }
        WIN32_FILE_ATTRIBUTE_DATA attrs{};
        Require(GetFileAttributesExW(argv[1],GetFileExInfoStandard,&attrs)!=0,
                "HEIF write did not create a file");
        const uint64_t bytes=(uint64_t(attrs.nFileSizeHigh)<<32)|attrs.nFileSizeLow;
        Require(bytes>1000&&bytes<32ull*1024*1024,"Unreasonable HEIF output size");
        std::printf("EMBEDDED_HEVC_FIXTURE_CREATED primary=4032x3024 "
                    "thumbnail=320x240 ids=%zu bytes=%llu\n",ids.size(),
                    static_cast<unsigned long long>(bytes));
        return 0;
    } catch(const heif::Error& e) {
        std::printf("EMBEDDED_HEVC_FIXTURE_FAIL code=%d message=%s\n",
                    int(e.get_code()),e.get_message().c_str());
        return 1;
    } catch(const std::exception& e) {
        std::printf("EMBEDDED_HEVC_FIXTURE_FAIL reason=%s\n",e.what());
        return 2;
    } catch(...) {
        std::puts("EMBEDDED_HEVC_FIXTURE_FAIL unexpected");
        return 3;
    }
}