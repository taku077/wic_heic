#include "stdafx.h"
#include "wic_heic.h"
#include "HeifStreamReader.h"


CHeifStreamReader::CHeifStreamReader(IStream* stream)
	: m_Stream(stream)
	, m_LastPos(0)
	, m_Size(0)
{
	DbgLog("CHeifStreamReader");
	m_Stream->AddRef();

	STATSTG st;
	if (S_OK == m_Stream->Stat(&st, STATFLAG_NONAME)) {
		m_Size = st.cbSize.QuadPart;
	}

	ULARGE_INTEGER uli;
	m_Stream->Seek({ 0 }, STREAM_SEEK_SET, &uli);
}


CHeifStreamReader::~CHeifStreamReader()
{
	DbgLog("~CHeifStreamReader");
	m_Stream->Release();
}

int64_t CHeifStreamReader::get_position() const
{
	DbgLog("CHeifStreamReader::get_position() -> %zu", m_LastPos);
	return m_LastPos;
}

int CHeifStreamReader::read(void* data, size_t size)
{
    // ISequentialStream::Read MUST advance the seek pointer by precisely
    // pcbRead bytes. Our seek() also maintains m_LastPos, so a second COM
    // Seek(STREAM_SEEK_CUR,0) after EVERY libheif read is redundant.
    // Avoiding that call is particularly useful for metadata parsing through
    // the WIC-provided IStream, which often performs many small reads.
    // No speculative read-ahead, file caching or stream ownership changes.
    if (size > static_cast<size_t>(0xFFFFFFFFu)) return -1;
    ULONG nRead=0;
    const HRESULT hr=m_Stream->Read(data,static_cast<ULONG>(size),&nRead);
    if (hr != S_OK) return -1;
    // Strictly match Read's actual byte count, not the requested amount.
    m_LastPos += static_cast<int64_t>(nRead);
    DbgLog("CHeifStreamReader::read(readSize=%zu) -> totalRead=%lu, lastPos=%lld",
           size,static_cast<unsigned long>(nRead),
           static_cast<long long>(m_LastPos));
    return 0;
}

int CHeifStreamReader::seek(int64_t position)
{
	LARGE_INTEGER li;
	ULARGE_INTEGER uli;
	li.QuadPart = position;

	if (m_Size && (uint64_t)position > m_Size) {
		return -1;
	}

	HRESULT hr = m_Stream->Seek(li, STREAM_SEEK_SET, &uli);
	DbgLog("CHeifStreamReader::seek(%lld) -> %lld -> hr=%X", position, uli.QuadPart, hr);
	if (hr != S_OK) {
		return -1;
	}

	m_LastPos = static_cast<int64_t>(uli.QuadPart);
	return 0;
}

heif_reader_grow_status CHeifStreamReader::wait_for_file_size(int64_t target_size)
{
	DbgLog("CHeifStreamReader::wait_for_file_size()");
	if (m_Size && (uint64_t)target_size > m_Size) {
		return heif_reader_grow_status_size_beyond_eof;
	}

	return heif_reader_grow_status_size_reached;
}


HRESULT CHeifStreamReader::CopyOriginalBytes(std::vector<uint8_t>& output)
{
    output.clear();
    if (!m_Stream || m_Size==0 || m_Size>128ULL*1024*1024)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    LARGE_INTEGER zero{};
    ULARGE_INTEGER saved{};
    HRESULT hr=m_Stream->Seek(zero,STREAM_SEEK_CUR,&saved);
    if(FAILED(hr)) return hr;
    hr=m_Stream->Seek(zero,STREAM_SEEK_SET,nullptr);
    if(FAILED(hr)) return hr;
    try {
        output.resize(size_t(m_Size));
    } catch (...) {
        LARGE_INTEGER back{};back.QuadPart=saved.QuadPart;
        m_Stream->Seek(back,STREAM_SEEK_SET,nullptr);
        return E_OUTOFMEMORY;
    }
    size_t total=0;
    while(total<output.size()) {
        ULONG readCount=0;
        ULONG request=static_cast<ULONG>(
            std::min<size_t>(output.size()-total,1024*1024));
        hr=m_Stream->Read(output.data()+total,request,&readCount);
        if(FAILED(hr) || readCount==0) break;
        total+=readCount;
    }
    LARGE_INTEGER back{};back.QuadPart=saved.QuadPart;
    const HRESULT restore=m_Stream->Seek(back,STREAM_SEEK_SET,nullptr);
    if(FAILED(restore) || total!=output.size()) {
        output.clear();
        return WINCODEC_ERR_BADIMAGE;
    }
    return S_OK;
}
