#pragma once
#include <cstdint>
#include <memory>

// The engine (CCStdC.h) defines _WINSOCKAPI_ before Windows.h and then includes WinSock2.h.
// A plain <windows.h> that comes first (this header is included before any engine header)
// pulls in the old winsock.h, and the two collide: sockaddr, fd_set, accept() ... redefined.
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <objidl.h>

// A read-only IStream over a byte range of a file handle or of a memory buffer.
// Media Foundation wraps it (MFCreateMFByteStreamOnStream) into the IMFByteStream a
// Source Reader reads from, which is how a video that is not a plain file on disk
// (a range of a bigger file, a handle somebody else opened, a buffer in memory) is
// played.
//
// Reads from a handle use explicit offsets, so several streams can share one handle
// and the shared file pointer does not matter to them. A handle that is passed in is
// borrowed: it stays open, and the caller keeps it valid while the video exists.
class MFRangeStream : public IStream
{
public:
	// length <= 0 means "from offset to the end". Returns nullptr if the range does
	// not fit in the file or the buffer.
	static MFRangeStream *createFromHandle(HANDLE file, bool ownsHandle, int64_t offset, int64_t length);
	static MFRangeStream *createFromMemory(const void *data, size_t size, int64_t offset, int64_t length);

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void **object);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// ISequentialStream
	STDMETHODIMP Read(void *buffer, ULONG bytes, ULONG *bytesRead);
	STDMETHODIMP Write(const void *buffer, ULONG bytes, ULONG *bytesWritten);

	// IStream
	STDMETHODIMP Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *newPosition);
	STDMETHODIMP SetSize(ULARGE_INTEGER size);
	STDMETHODIMP CopyTo(IStream *target, ULARGE_INTEGER bytes, ULARGE_INTEGER *read, ULARGE_INTEGER *written);
	STDMETHODIMP Commit(DWORD flags);
	STDMETHODIMP Revert();
	STDMETHODIMP LockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER bytes, DWORD lockType);
	STDMETHODIMP UnlockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER bytes, DWORD lockType);
	STDMETHODIMP Stat(STATSTG *stat, DWORD flags);
	STDMETHODIMP Clone(IStream **clone);

private:
	// owns the handle when it was opened for us
	struct FileHolder
	{
		HANDLE handle = INVALID_HANDLE_VALUE;
		bool owns = false;
		~FileHolder()
		{
			if (owns && handle != INVALID_HANDLE_VALUE)
				CloseHandle(handle);
		}
	};

	MFRangeStream(std::shared_ptr<FileHolder> file, const uint8_t *memory, uint64_t offset, uint64_t length);
	virtual ~MFRangeStream();

	LONG _refs = 1;
	std::shared_ptr<FileHolder> _file;
	const uint8_t *_memory;
	uint64_t _offset;
	uint64_t _length;
	uint64_t _position = 0;
	HANDLE _event = nullptr; // completes reads from handles opened for overlapped I/O
	CRITICAL_SECTION _lock;
};
