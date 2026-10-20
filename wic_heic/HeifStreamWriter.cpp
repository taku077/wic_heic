#include "stdafx.h"
#include "HeifStreamWriter.h"


CHeifStreamWriter::CHeifStreamWriter(IStream* stream)
	: m_Stream(stream)
{
	m_Stream->AddRef();
}


CHeifStreamWriter::~CHeifStreamWriter()
{
	m_Stream->Release();
}

heif_error CHeifStreamWriter::write(const void* data, size_t size)
{
	// Guard ULONG truncation and detect successful but incomplete IStream writes.
	if (size > static_cast<size_t>(0xFFFFFFFFu)) {
		return{heif_error_Encoding_error,heif_suberror_Cannot_write_output_data,
			"Output buffer exceeds IStream limit"};
	}
	ULONG written=0;
	HRESULT hr=m_Stream->Write(data,static_cast<ULONG>(size),&written);
	if (FAILED(hr) || written!=size) {
		return{
			heif_error_Encoding_error,
			heif_suberror_Cannot_write_output_data,
			"Unable to write"
		};
	}

	return{
		heif_error_Ok,
		heif_suberror_Unspecified,
		""
	};
}
