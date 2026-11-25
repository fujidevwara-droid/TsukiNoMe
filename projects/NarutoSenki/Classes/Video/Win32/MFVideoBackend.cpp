#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MFVideoBackend.h"
#include "MFRangeStream.h"
#include "ALPcmSink.h"
#include "Defines.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>

namespace
{
	template <typename T>
	void safeRelease(T *&object)
	{
		if (object)
		{
			object->Release();
			object = nullptr;
		}
	}

	std::string hresultText(HRESULT hr)
	{
		char text[16];
		std::snprintf(text, sizeof(text), "0x%08X", (unsigned)hr);
		return text;
	}

	std::wstring widen(const std::string &utf8)
	{
		if (utf8.empty())
			return std::wstring();

		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
		std::wstring wide((size_t)length, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], length);
		return wide;
	}

	HRESULT seekReader(IMFSourceReader *reader, int64_t positionHns)
	{
		PROPVARIANT position;
		PropVariantInit(&position);
		position.vt = VT_I8;
		position.hVal.QuadPart = positionHns;
		const HRESULT hr = reader->SetCurrentPosition(GUID_NULL, position);
		PropVariantClear(&position);
		return hr;
	}

	// Each decoding thread needs COM for Media Foundation's decoders.
	thread_local bool t_comInitialized = false;

	// Media Foundation as an IPipelineSource: two Source Readers on the same source, one
	// for the video stream and one for the audio stream.
	class MFPipelineSource : public IPipelineSource
	{
	public:
		struct Info
		{
			int width = 0;
			int height = 0;
			int64_t durationHns = 0;
			int64_t frameDurationHns = 333333;
			bool hasAudio = false;
			int audioChannels = 0;
			int audioRate = 0;
		};

		virtual ~MFPipelineSource()
		{
			safeRelease(_videoReader);
			safeRelease(_audioReader);
		}

		bool open(const VideoSource &source, int64_t offset, int64_t length, std::string &error)
		{
			if (!createReader(source, offset, length, true, &_videoReader, error) || !configureVideo(error))
				return false;

			// Sound is a bonus: whatever goes wrong with it, the video plays on the system clock.
			std::string audioError;
			if (!createReader(source, offset, length, false, &_audioReader, audioError) || !configureAudio())
			{
				safeRelease(_audioReader);
				_info.hasAudio = false;
				CCLOG("MFVideoBackend: playing without sound%s%s", audioError.empty() ? "" : ": ", audioError.c_str());
			}
			return true;
		}

		void dropAudio()
		{
			safeRelease(_audioReader);
			_info.hasAudio = false;
		}

		const Info &info() const { return _info; }

		virtual void onThreadStart()
		{
			t_comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
		}

		virtual void onThreadEnd()
		{
			if (t_comInitialized)
				CoUninitialize();
			t_comInitialized = false;
		}

		virtual bool seekVideo(int64_t hns, std::string &error);
		virtual Read readVideo(int64_t &ptsHns, int64_t &durationHns, uint8_t *bgra, std::string &error);
		virtual bool seekAudio(int64_t hns);
		virtual Read readAudio(std::vector<int16_t> &pcm, int64_t &startHns);

	private:
		bool createReader(const VideoSource &source, int64_t offset, int64_t length, bool forVideo, IMFSourceReader **reader, std::string &error);
		bool configureVideo(std::string &error);
		bool configureAudio();

		IMFSourceReader *_videoReader = nullptr;
		IMFSourceReader *_audioReader = nullptr;
		int _stride = 0; // bytes per row of the decoder's RGB32 frames; negative: bottom-up
		Info _info;
	};
}

// ---------------------------------------------------------------------------
// Opening the file
// ---------------------------------------------------------------------------

bool MFPipelineSource::createReader(const VideoSource &source, int64_t offset, int64_t length, bool forVideo, IMFSourceReader **reader, std::string &error)
{
	*reader = nullptr;

	IMFAttributes *attributes = nullptr;
	HRESULT hr = MFCreateAttributes(&attributes, 2);
	if (FAILED(hr))
	{
		error = "cannot create the video reader (" + hresultText(hr) + ")";
		return false;
	}

	// The video reader converts to RGB and the colour space of the screen for us.
	if (forVideo)
		attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

	const bool wholeFile = source.kind == VideoSource::Kind::File && offset == 0 && length <= 0;
	if (wholeFile)
	{
		hr = MFCreateSourceReaderFromURL(widen(source.path).c_str(), attributes, reader);
	}
	else
	{
		MFRangeStream *stream = nullptr;
		switch (source.kind)
		{
		case VideoSource::Kind::File:
		{
			HANDLE file = CreateFileW(widen(source.path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				error = "cannot open " + source.path;
				attributes->Release();
				return false;
			}
			stream = MFRangeStream::createFromHandle(file, true, offset, length);
			break;
		}
		case VideoSource::Kind::Handle:
			stream = MFRangeStream::createFromHandle((HANDLE)source.handle, false, offset, length);
			break;
		case VideoSource::Kind::Memory:
			stream = MFRangeStream::createFromMemory(source.data, source.size, offset, length);
			break;
		case VideoSource::Kind::Fd:
			error = "file descriptors are not used on Windows, pass a HANDLE or a path";
			attributes->Release();
			return false;
		}

		if (!stream)
		{
			error = "the offset and length do not fit in the source";
			attributes->Release();
			return false;
		}

		IMFByteStream *byteStream = nullptr;
		hr = MFCreateMFByteStreamOnStream(stream, &byteStream);
		stream->Release();
		if (SUCCEEDED(hr))
		{
			// There is no file name to tell the container: say it is MP4.
			IMFAttributes *streamAttributes = nullptr;
			if (SUCCEEDED(byteStream->QueryInterface(IID_IMFAttributes, (void **)&streamAttributes)))
			{
				streamAttributes->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"video/mp4");
				streamAttributes->SetString(MF_BYTESTREAM_ORIGIN_NAME, L"video.mp4");
				streamAttributes->Release();
			}

			hr = MFCreateSourceReaderFromByteStream(byteStream, attributes, reader);
			byteStream->Release();
		}
	}

	attributes->Release();

	if (FAILED(hr))
	{
		error = "cannot open the video (" + hresultText(hr) + ")";
		*reader = nullptr;
		return false;
	}
	return true;
}

bool MFPipelineSource::configureVideo(std::string &error)
{
	const DWORD stream = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;

	_videoReader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
	HRESULT hr = _videoReader->SetStreamSelection(stream, TRUE);
	if (FAILED(hr))
	{
		error = "the file has no video track";
		return false;
	}

	IMFMediaType *wanted = nullptr;
	hr = MFCreateMediaType(&wanted);
	if (FAILED(hr))
	{
		error = "out of memory (" + hresultText(hr) + ")";
		return false;
	}
	wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	wanted->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	hr = _videoReader->SetCurrentMediaType(stream, nullptr, wanted);
	wanted->Release();
	if (FAILED(hr))
	{
		if (hr == MF_E_TOPO_CODEC_NOT_FOUND)
			error = "no decoder for this video on this PC (Windows N editions need the Media Feature Pack)";
		else
			error = "cannot decode this video (" + hresultText(hr) + ")";
		return false;
	}

	IMFMediaType *current = nullptr;
	hr = _videoReader->GetCurrentMediaType(stream, &current);
	if (FAILED(hr))
	{
		error = "cannot read the video format (" + hresultText(hr) + ")";
		return false;
	}

	UINT32 width = 0;
	UINT32 height = 0;
	MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &width, &height);

	LONG stride = 0;
	UINT32 defaultStride = 0;
	if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &defaultStride)))
		stride = (LONG)(INT32)defaultStride;
	else
		MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride);

	UINT32 numerator = 0;
	UINT32 denominator = 0;
	if (SUCCEEDED(MFGetAttributeRatio(current, MF_MT_FRAME_RATE, &numerator, &denominator)) && numerator > 0 && denominator > 0)
		_info.frameDurationHns = (int64_t)10000000 * denominator / numerator;

	current->Release();

	if (width == 0 || height == 0)
	{
		error = "the video has no size";
		return false;
	}

	_info.width = (int)width;
	_info.height = (int)height;
	_stride = (int)stride;

	PROPVARIANT duration;
	PropVariantInit(&duration);
	if (SUCCEEDED(_videoReader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration)) && duration.vt == VT_UI8)
		_info.durationHns = (int64_t)duration.uhVal.QuadPart;
	PropVariantClear(&duration);

	return true;
}

// 16-bit PCM, mono or stereo, at the track's own sample rate: MF's audio resampler does
// the decoding and the conversion, OpenAL does the rest.
bool MFPipelineSource::configureAudio()
{
	if (!_audioReader)
		return false;

	const DWORD stream = (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM;

	_audioReader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
	if (FAILED(_audioReader->SetStreamSelection(stream, TRUE)))
		return false; // no audio track

	UINT32 channels = 2;
	UINT32 rate = 44100;
	IMFMediaType *native = nullptr;
	if (SUCCEEDED(_audioReader->GetNativeMediaType(stream, 0, &native)))
	{
		const UINT32 nativeChannels = MFGetAttributeUINT32(native, MF_MT_AUDIO_NUM_CHANNELS, 2);
		const UINT32 nativeRate = MFGetAttributeUINT32(native, MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
		channels = nativeChannels == 1 ? 1 : 2;
		rate = nativeRate > 0 ? nativeRate : 44100;
		native->Release();
	}

	IMFMediaType *wanted = nullptr;
	if (FAILED(MFCreateMediaType(&wanted)))
		return false;

	wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	wanted->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
	wanted->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
	wanted->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	wanted->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
	wanted->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2);
	wanted->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	const HRESULT hr = _audioReader->SetCurrentMediaType(stream, nullptr, wanted);
	wanted->Release();
	if (FAILED(hr))
		return false;

	// what the reader actually delivers
	IMFMediaType *current = nullptr;
	if (SUCCEEDED(_audioReader->GetCurrentMediaType(stream, &current)))
	{
		channels = MFGetAttributeUINT32(current, MF_MT_AUDIO_NUM_CHANNELS, channels);
		rate = MFGetAttributeUINT32(current, MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
		current->Release();
	}

	_info.audioChannels = (int)channels;
	_info.audioRate = (int)rate;
	_info.hasAudio = true;
	return true;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

bool MFPipelineSource::seekVideo(int64_t hns, std::string &error)
{
	const HRESULT hr = seekReader(_videoReader, hns);
	if (FAILED(hr))
	{
		error = "seeking failed (" + hresultText(hr) + ")";
		return false;
	}
	return true;
}

// Reads and converts the next picture: BGRA, top row first.
IPipelineSource::Read MFPipelineSource::readVideo(int64_t &ptsHns, int64_t &durationHns, uint8_t *bgra, std::string &error)
{
	for (;;)
	{
		DWORD flags = 0;
		LONGLONG timestamp = 0;
		IMFSample *sample = nullptr;
		HRESULT hr = _videoReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample);
		if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR))
		{
			safeRelease(sample);
			error = "video decoding failed (" + hresultText(FAILED(hr) ? hr : E_FAIL) + ")";
			return Read::Failed;
		}

		if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			safeRelease(sample);
			return Read::End;
		}

		if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
		{
			// The size is fixed for the texture; only the row layout may change.
			IMFMediaType *changed = nullptr;
			if (SUCCEEDED(_videoReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &changed)))
			{
				UINT32 width = 0;
				UINT32 height = 0;
				MFGetAttributeSize(changed, MF_MT_FRAME_SIZE, &width, &height);
				if ((int)width != _info.width || (int)height != _info.height)
				{
					changed->Release();
					safeRelease(sample);
					error = "the size of the video changes while it plays, which is not supported";
					return Read::Failed;
				}

				UINT32 stride = 0;
				if (SUCCEEDED(changed->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)))
					_stride = (int)(INT32)stride;
				changed->Release();
			}
		}

		if (!sample)
			continue; // a gap or a stream tick: nothing to show

		LONGLONG duration = 0;
		if (FAILED(sample->GetSampleDuration(&duration)) || duration <= 0)
			duration = 0; // the caller uses the frame rate

		IMFMediaBuffer *buffer = nullptr;
		hr = sample->ConvertToContiguousBuffer(&buffer);
		if (FAILED(hr))
		{
			sample->Release();
			error = "video decoding failed (" + hresultText(hr) + ")";
			return Read::Failed;
		}

		const int height = _info.height;
		const size_t rowBytes = (size_t)_info.width * 4;

		bool copied = false;
		IMF2DBuffer *buffer2d = nullptr;
		if (SUCCEEDED(buffer->QueryInterface(IID_IMF2DBuffer, (void **)&buffer2d)))
		{
			// scan0 is the top row of the picture, and the pitch says how to get to the
			// next one down (negative if the rows are stored bottom-up).
			BYTE *scan0 = nullptr;
			LONG pitch = 0;
			if (SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch)))
			{
				for (int row = 0; row < height; ++row)
					std::memcpy(bgra + (size_t)row * rowBytes, scan0 + (ptrdiff_t)row * pitch, rowBytes);
				buffer2d->Unlock2D();
				copied = true;
			}
			buffer2d->Release();
		}

		if (!copied)
		{
			BYTE *data = nullptr;
			DWORD length = 0;
			if (SUCCEEDED(buffer->Lock(&data, nullptr, &length)))
			{
				const size_t stride = (size_t)(_stride < 0 ? -_stride : _stride);
				if (stride >= rowBytes && length >= stride * (size_t)height)
				{
					for (int row = 0; row < height; ++row)
					{
						// a negative stride means the bottom row comes first in memory
						const int memoryRow = _stride < 0 ? height - 1 - row : row;
						std::memcpy(bgra + (size_t)row * rowBytes, data + (size_t)memoryRow * stride, rowBytes);
					}
					copied = true;
				}
				buffer->Unlock();
			}
		}

		buffer->Release();
		sample->Release();

		if (!copied)
		{
			error = "video decoding failed (unexpected frame layout)";
			return Read::Failed;
		}

		ptsHns = timestamp;
		durationHns = duration;
		return Read::Item;
	}
}

bool MFPipelineSource::seekAudio(int64_t hns)
{
	const HRESULT hr = seekReader(_audioReader, hns);
	if (FAILED(hr))
	{
		CCLOG("MFVideoBackend: audio seek failed (%s)", hresultText(hr).c_str());
		return false;
	}
	return true;
}

IPipelineSource::Read MFPipelineSource::readAudio(std::vector<int16_t> &pcm, int64_t &startHns)
{
	pcm.clear();

	for (;;)
	{
		DWORD flags = 0;
		LONGLONG timestamp = 0;
		IMFSample *sample = nullptr;
		const HRESULT hr = _audioReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &timestamp, &sample);
		if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR))
		{
			safeRelease(sample);
			CCLOG("MFVideoBackend: audio decoding failed (%s)", hresultText(FAILED(hr) ? hr : E_FAIL).c_str());
			return Read::Failed;
		}

		if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			safeRelease(sample);
			return Read::End;
		}

		if (!sample)
			continue;

		IMFMediaBuffer *buffer = nullptr;
		if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
		{
			sample->Release();
			return Read::Failed;
		}

		BYTE *data = nullptr;
		DWORD length = 0;
		if (SUCCEEDED(buffer->Lock(&data, nullptr, &length)))
		{
			const size_t channels = (size_t)_info.audioChannels;
			const size_t samples = (length / sizeof(int16_t)) / channels * channels;
			pcm.assign((const int16_t *)data, (const int16_t *)data + samples);
			buffer->Unlock();
		}

		buffer->Release();
		sample->Release();

		startHns = timestamp;
		if (pcm.empty())
			continue;
		return Read::Item;
	}
}

// ---------------------------------------------------------------------------
// The backend
// ---------------------------------------------------------------------------

MFVideoBackend::MFVideoBackend()
{
}

MFVideoBackend::~MFVideoBackend()
{
	MFVideoBackend::close();
}

bool MFVideoBackend::open(const VideoSource &source, int64_t offset, int64_t length, VideoInfo &info, std::string &error)
{
	// Media Foundation wants COM. If this thread has none yet it gets it from us; if it
	// already has it in another mode, that is fine too and not ours to undo.
	const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	_comInitialized = SUCCEEDED(com);

	const HRESULT started = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	if (FAILED(started))
	{
		error = "Media Foundation cannot start (" + hresultText(started) + ")";
		MFVideoBackend::close();
		return false;
	}
	_mfStarted = true;

	std::unique_ptr<MFPipelineSource> reader(new MFPipelineSource());
	if (!reader->open(source, offset, length, error))
	{
		reader.reset();
		MFVideoBackend::close();
		return false;
	}

	MFPipelineSource::Info media = reader->info();

	std::unique_ptr<ALPcmSink> sink;
	if (media.hasAudio)
	{
		sink.reset(new ALPcmSink());
		if (!sink->open(media.audioChannels, media.audioRate))
		{
			// no usable OpenAL: no sound
			sink.reset();
			reader->dropAudio();
			media.hasAudio = false;
		}
	}

	Config config;
	config.width = media.width;
	config.height = media.height;
	config.durationHns = media.durationHns;
	config.frameDurationHns = media.frameDurationHns;
	config.hasAudio = media.hasAudio;
	config.audioChannels = media.audioChannels;
	config.audioRate = media.audioRate;

	if (!begin(std::move(reader), std::move(sink), config, info, error))
	{
		MFVideoBackend::close();
		return false;
	}
	return true;
}

void MFVideoBackend::close()
{
	PipelineBackend::close(); // stops the threads and releases the readers

	if (_mfStarted)
	{
		MFShutdown();
		_mfStarted = false;
	}
	if (_comInitialized)
	{
		CoUninitialize();
		_comInitialized = false;
	}
}
