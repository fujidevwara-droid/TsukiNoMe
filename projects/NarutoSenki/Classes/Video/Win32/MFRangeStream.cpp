#include "MFRangeStream.h"

#include <algorithm>
#include <cstring>

MFRangeStream::MFRangeStream(std::shared_ptr<FileHolder> file, const uint8_t *memory, uint64_t offset, uint64_t length)
	: _file(std::move(file))
	, _memory(memory)
	, _offset(offset)
	, _length(length)
{
	InitializeCriticalSection(&_lock);
	_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

MFRangeStream::~MFRangeStream()
{
	if (_event)
		CloseHandle(_event);
	DeleteCriticalSection(&_lock);
}

MFRangeStream *MFRangeStream::createFromHandle(HANDLE file, bool ownsHandle, int64_t offset, int64_t length)
{
	auto holder = std::make_shared<FileHolder>();
	holder->handle = file;
	holder->owns = ownsHandle; // closed with the last stream, also when we return nullptr below

	LARGE_INTEGER size;
	if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size) || offset < 0 || offset > size.QuadPart)
		return nullptr;

	const uint64_t available = (uint64_t)size.QuadPart - (uint64_t)offset;
	if (length > 0 && (uint64_t)length > available)
		return nullptr;

	return new MFRangeStream(holder, nullptr, (uint64_t)offset, length > 0 ? (uint64_t)length : available);
}

MFRangeStream *MFRangeStream::createFromMemory(const void *data, size_t size, int64_t offset, int64_t length)
{
	if (!data || offset < 0 || (uint64_t)offset > size)
		return nullptr;

	const uint64_t available = (uint64_t)size - (uint64_t)offset;
	if (length > 0 && (uint64_t)length > available)
		return nullptr;

	return new MFRangeStream(nullptr, (const uint8_t *)data, (uint64_t)offset, length > 0 ? (uint64_t)length : available);
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

STDMETHODIMP MFRangeStream::QueryInterface(REFIID riid, void **object)
{
	if (!object)
		return E_POINTER;

	if (riid == IID_IUnknown || riid == IID_ISequentialStream || riid == IID_IStream)
	{
		*object = static_cast<IStream *>(this);
		AddRef();
		return S_OK;
	}

	*object = nullptr;
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) MFRangeStream::AddRef()
{
	return (ULONG)InterlockedIncrement(&_refs);
}

STDMETHODIMP_(ULONG) MFRangeStream::Release()
{
	const LONG refs = InterlockedDecrement(&_refs);
	if (refs == 0)
		delete this;
	return (ULONG)refs;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

STDMETHODIMP MFRangeStream::Read(void *buffer, ULONG bytes, ULONG *bytesRead)
{
	if (bytesRead)
		*bytesRead = 0;
	if (!buffer)
		return STG_E_INVALIDPOINTER;

	EnterCriticalSection(&_lock);

	HRESULT result = S_OK;
	ULONG done = 0;
	const uint64_t remaining = _position < _length ? _length - _position : 0;
	const ULONG wanted = (ULONG)(std::min)((uint64_t)bytes, remaining);

	if (wanted > 0)
	{
		if (_memory)
		{
			std::memcpy(buffer, _memory + _offset + _position, wanted);
			done = wanted;
		}
		else
		{
			const uint64_t at = _offset + _position;
			OVERLAPPED overlapped = {};
			overlapped.Offset = (DWORD)(at & 0xFFFFFFFFu);
			overlapped.OffsetHigh = (DWORD)(at >> 32);
			overlapped.hEvent = _event;
			ResetEvent(_event);

			DWORD read = 0;
			if (!ReadFile(_file->handle, buffer, wanted, &read, &overlapped))
			{
				if (GetLastError() == ERROR_IO_PENDING && GetOverlappedResult(_file->handle, &overlapped, &read, TRUE))
					done = read;
				else
					result = HRESULT_FROM_WIN32(GetLastError());
			}
			else
				done = read;
		}
	}

	_position += done;
	LeaveCriticalSection(&_lock);

	if (bytesRead)
		*bytesRead = done;
	return result;
}

STDMETHODIMP MFRangeStream::Write(const void *, ULONG, ULONG *bytesWritten)
{
	if (bytesWritten)
		*bytesWritten = 0;
	return STG_E_ACCESSDENIED;
}

STDMETHODIMP MFRangeStream::Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *newPosition)
{
	EnterCriticalSection(&_lock);

	int64_t base = 0;
	switch (origin)
	{
	case STREAM_SEEK_SET: base = 0; break;
	case STREAM_SEEK_CUR: base = (int64_t)_position; break;
	case STREAM_SEEK_END: base = (int64_t)_length; break;
	default:
		LeaveCriticalSection(&_lock);
		return STG_E_INVALIDFUNCTION;
	}

	const int64_t target = base + move.QuadPart;
	if (target < 0)
	{
		LeaveCriticalSection(&_lock);
		return STG_E_INVALIDFUNCTION;
	}

	_position = (uint64_t)target; // beyond the end is allowed, reads then return nothing
	if (newPosition)
		newPosition->QuadPart = _position;

	LeaveCriticalSection(&_lock);
	return S_OK;
}

STDMETHODIMP MFRangeStream::SetSize(ULARGE_INTEGER)
{
	return E_NOTIMPL;
}

STDMETHODIMP MFRangeStream::CopyTo(IStream *, ULARGE_INTEGER, ULARGE_INTEGER *, ULARGE_INTEGER *)
{
	return E_NOTIMPL;
}

STDMETHODIMP MFRangeStream::Commit(DWORD)
{
	return S_OK;
}

STDMETHODIMP MFRangeStream::Revert()
{
	return S_OK;
}

STDMETHODIMP MFRangeStream::LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
{
	return STG_E_INVALIDFUNCTION;
}

STDMETHODIMP MFRangeStream::UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
{
	return STG_E_INVALIDFUNCTION;
}

STDMETHODIMP MFRangeStream::Stat(STATSTG *stat, DWORD)
{
	if (!stat)
		return STG_E_INVALIDPOINTER;

	std::memset(stat, 0, sizeof(*stat));
	stat->type = STGTY_STREAM;
	stat->cbSize.QuadPart = _length;
	stat->grfMode = STGM_READ;
	return S_OK;
}

STDMETHODIMP MFRangeStream::Clone(IStream **clone)
{
	if (!clone)
		return STG_E_INVALIDPOINTER;

	// A second stream over the same range, with a position of its own.
	*clone = new MFRangeStream(_file, _memory, _offset, _length);
	return S_OK;
}
