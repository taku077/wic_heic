// CI-only disposable HEVC/HEIF with an actual embedded 320x240 thumbnail.
// The production encoder deliberately refuses SetThumbnail; do not change it
// merely to manufacture a fixture. This tool links pinned libheif statically.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <libheif/heif_cxx.h>
#include <libheif/heif.h>
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
        const bool withPQ=argc==3 && wcscmp(argv[2],L"--with-pq")==0;
        const bool withHLG=argc==3 && wcscmp(argv[2],L"--with-hlg")==0;
        const bool gridGradient=argc==3 && wcscmp(argv[2],L"--with-grid-gradient")==0;
        const bool gridDetail=argc==3 && wcscmp(argv[2],L"--with-grid-detail")==0;
        const bool withGrid=(argc==3 && wcscmp(argv[2],L"--with-grid")==0) ||
            gridGradient || gridDetail;
        Require(argc==2 || noThumb || withIcc || withAlpha || withPQ || withHLG || withGrid,"Invalid optional mode");
        Require(Env(L"GITHUB_ACTIONS")==L"true","Test generation is CI only");
        const auto root=Env(L"RUNNER_TEMP");
        Require(!root.empty(),"No CI runner temporary folder");
        const std::wstring exact=root+(noThumb?L"\\wicfast_no_thumb_4032x3024.heic":
                 withIcc?L"\\wicfast_with_icc_4032x3024.heic":
                 withAlpha?L"\\wicfast_with_alpha_4032x3024.heic":
                 withPQ?L"\\wicfast_with_pq_4032x3024.heic":
                 withHLG?L"\\wicfast_with_hlg_4032x3024.heic":
                 gridGradient?L"\\wicfast_with_grid_gradient_4032x3024.heic":
                 gridDetail?L"\\wicfast_with_grid_detail_4032x3024.heic":
                 withGrid?L"\\wicfast_with_grid_4032x3024.heic":
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
                if(gridGradient){
                    row[channels*x+0]=uint8_t((255*x)/(kWidth-1));
                    row[channels*x+1]=uint8_t((255*y)/(kHeight-1));
                    row[channels*x+2]=uint8_t((255*(x+y))/(kWidth+kHeight-2));
                } else if(gridDetail){
                    // 9x11px checker (near the 256px thumbnail sampling
                    // limit) plus 3px and 5px channel-specific line detail.
                    // Deterministic artificial pattern, no user photography.
                    row[channels*x+0]=uint8_t(((x/9+y/11)&1)?240:16);
                    row[channels*x+1]=uint8_t(((x/3)&1)?225:30);
                    row[channels*x+2]=uint8_t(((y/5)&1)?220:25);
                } else {
                    row[channels*x+0]=colors[q][0];
                    row[channels*x+1]=colors[q][1];
                    row[channels*x+2]=colors[q][2];
                }
                if(withAlpha)row[channels*x+3]=uint8_t(64+((x/17+y/19)%192));
            }
        }
        if(withPQ || withHLG) {
            heif::ColorProfile_nclx nclx;
            nclx.set_color_primaries(static_cast<heif_color_primaries>(9));
            nclx.set_transfer_characteristics(static_cast<heif_transfer_characteristics>(withPQ?16:18));
            nclx.set_matrix_coefficients(static_cast<heif_matrix_coefficients>(9));
            nclx.set_full_range_flag(false);
            source.set_nclx_color_profile(nclx);
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
        if(withGrid) {
            // A genuinely coded 8x6 HEIF grid: 48 independently encoded
            // opaque 504x504 tiles and a separately encoded 320x240 preview.
            // Unlike earlier metadata FourCC mutations, this is a valid grid.
            // All pixels are artificial; files can only be written to CI temp.
            constexpr int cols=8,rows=6,tw=kWidth/cols,th=kHeight/rows;
            heif_context* gc=heif_context_alloc();
            Require(gc!=nullptr,"No grid context");
            heif_encoder* ge=nullptr;
            heif_image_handle* grid=nullptr;
            heif_image_handle* thumbnail=nullptr;
            heif_image* full=nullptr;
            auto ok=[&](heif_error error,const char* step){
                Require(error.code==heif_error_Ok,step);
            };
            ok(heif_context_get_encoder_for_format(gc,heif_compression_HEVC,&ge),
                "No HEVC grid encoder");
            ok(heif_encoder_set_lossy_quality(ge,55),"Cannot set grid quality");
            ok(heif_context_add_grid_image(gc,kWidth,kHeight,cols,rows,nullptr,&grid),
                "Cannot create 8x6 HEIF grid");
            for(int ty=0;ty<rows;++ty)for(int tx=0;tx<cols;++tx){
                heif_image* tile=nullptr;
                ok(heif_image_create(tw,th,heif_colorspace_RGB,
                                     heif_chroma_interleaved_RGB,&tile),"Grid tile create failed");
                ok(heif_image_add_plane(tile,heif_channel_interleaved,tw,th,8),
                    "Grid tile plane failed");
                size_t ts=0;
                uint8_t* dest=heif_image_get_plane2(tile,heif_channel_interleaved,&ts);
                Require(dest&&ts>=size_t(tw)*3,"Grid tile RGB unavailable");
                for(int y=0;y<th;++y)
                    memcpy(dest+size_t(y)*ts,
                        pixels+size_t(ty*th+y)*stride+size_t(tx*tw)*3,
                        size_t(tw)*3);
                ok(heif_context_add_image_tile(gc,grid,tx,ty,tile,ge),
                    "Grid tile encoder failed");
                heif_image_release(tile);
            }
            // Only a separately allocated artificial full RGB image is used
            // to create an embedded preview; it is never encoded as a second
            // full-sized HEIC primary.
            ok(heif_image_create(kWidth,kHeight,heif_colorspace_RGB,
                    heif_chroma_interleaved_RGB,&full),"Grid thumb source failed");
            ok(heif_image_add_plane(full,heif_channel_interleaved,kWidth,kHeight,8),
                "Grid thumb source plane failed");
            size_t fs=0;
            uint8_t* fp=heif_image_get_plane2(full,heif_channel_interleaved,&fs);
            Require(fp&&fs>=size_t(kWidth)*3,"Grid thumb source pixels missing");
            for(int y=0;y<kHeight;++y)
                memcpy(fp+size_t(y)*fs,pixels+size_t(y)*stride,size_t(kWidth)*3);
            ok(heif_context_encode_thumbnail(gc,full,grid,ge,nullptr,
                  kThumbBound,&thumbnail),"Grid embedded thumbnail failed");
            Require(thumbnail!=nullptr,"No grid thumbnail handle");
            ok(heif_context_set_primary_image(gc,grid),"Grid primary mark failed");
            const std::string filename=std::filesystem::path(argv[1]).string();
            ok(heif_context_write_to_file(gc,filename.c_str()),
                "Grid file write failed");
            heif_image_release(full);
            heif_image_handle_release(thumbnail);
            heif_image_handle_release(grid);
            heif_encoder_release(ge);
            heif_context_free(gc);
            heif::Context verify;
            verify.read_from_file(filename);
            auto primary=verify.get_primary_image_handle();
            auto ids=primary.get_list_of_thumbnail_IDs();
            Require(primary.get_width()==kWidth&&primary.get_height()==kHeight,
                    "Grid master geometry invalid");
            Require(ids.size()==1,"Grid thumbnail association missing");
            auto thandle=primary.get_thumbnail(ids[0]);
            Require(thandle.get_width()==320&&thandle.get_height()==240,
                    "Grid thumb geometry invalid");
            std::printf("CI_VALID_GRID_48_TILES_EMBEDDED_THUMB_CREATED_PASS pattern=%s\n",
                        gridGradient?"gradient":gridDetail?"detail":"quadrants");
            return 0;
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