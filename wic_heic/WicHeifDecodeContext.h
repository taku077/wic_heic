#pragma once
// Minimal, publicly supported libheif C-API context wrapper for WIC decode.
// Enables per-image grid tile CPU budgets without touching private C++ Context
// members, global libheif defaults, third-party codecs or HEIC original bytes.
// C++ heif::ImageHandle remains unchanged, retaining existing metadata/saves.
#include <libheif/heif.h>
#include <libheif/heif_cxx.h>
#include <vector>
#include <stdexcept>
#include <memory>

class CWicHeifDecodeContext final {
    heif_context* m_Ctx = nullptr;
    static int64_t Position(void* p) {
        return static_cast<heif::Context::Reader*>(p)->get_position();
    }
    static int Read(void* data,size_t size,void* p) {
        return static_cast<heif::Context::Reader*>(p)->read(data,size);
    }
    static int Seek(int64_t pos,void* p) {
        return static_cast<heif::Context::Reader*>(p)->seek(pos);
    }
    static heif_reader_grow_status Wait(int64_t size,void* p) {
        return static_cast<heif::Context::Reader*>(p)->wait_for_file_size(size);
    }
    static void Check(const heif_error& err) {
        if(err.code!=heif_error_Ok)throw heif::Error(err);
    }
public:
    CWicHeifDecodeContext() : m_Ctx(heif_context_alloc()) {
        if(!m_Ctx)throw std::bad_alloc();
    }
    ~CWicHeifDecodeContext() { if(m_Ctx)heif_context_free(m_Ctx); }
    CWicHeifDecodeContext(const CWicHeifDecodeContext&)=delete;
    CWicHeifDecodeContext& operator=(const CWicHeifDecodeContext&)=delete;

    void read_from_reader(heif::Context::Reader& stream) {
        // Preserve libheif's documented version-1 callbacks byte-for-byte
        // rather than copying or buffering the underlying WIC IStream.
        const heif_reader callbacks{1,&Position,&Read,&Seek,&Wait,
                                    nullptr,nullptr,nullptr,nullptr};
        Check(heif_context_read_from_reader(m_Ctx,&callbacks,&stream,nullptr));
    }
    int get_number_of_top_level_images() const noexcept {
        return heif_context_get_number_of_top_level_images(m_Ctx);
    }
    std::vector<heif_item_id> get_list_of_top_level_image_IDs() const {
        const int count=get_number_of_top_level_images();
        if(count<0 || count>100000)throw std::runtime_error("Invalid HEIF image count");
        std::vector<heif_item_id> ids(static_cast<size_t>(count));
        const int got=heif_context_get_list_of_top_level_image_IDs(
            m_Ctx,ids.data(),count);
        if(got!=count)throw std::runtime_error("HEIF top-level ID count changed");
        return ids;
    }
    heif::ImageHandle get_primary_image_handle() const {
        heif_image_handle* raw=nullptr;
        Check(heif_context_get_primary_image_handle(m_Ctx,&raw));
        if(!raw)throw std::runtime_error("Missing primary HEIF image");
        return heif::ImageHandle(raw);
    }
    heif::ImageHandle get_image_handle(heif_item_id id) const {
        heif_image_handle* raw=nullptr;
        Check(heif_context_get_image_handle(m_Ctx,id,&raw));
        if(!raw)throw std::runtime_error("Missing requested HEIF image");
        return heif::ImageHandle(raw);
    }
    void set_max_decoding_threads(int workers) {
        if(workers<0 || workers>4)throw std::invalid_argument("tile workers");
        heif_context_set_max_decoding_threads(m_Ctx,workers);
        if(heif_context_get_max_decoding_threads(m_Ctx)!=workers)
            throw std::runtime_error("HEIF grid tile budget not applied");
    }
    int get_max_decoding_threads() const noexcept {
        return heif_context_get_max_decoding_threads(m_Ctx);
    }
};
