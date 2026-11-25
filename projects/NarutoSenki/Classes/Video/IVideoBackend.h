#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// The platform half of CCVideoSprite. A backend decodes the video and its audio and
// keeps the newest frame in a GL texture; CCVideoSprite draws that texture as a quad
// and owns everything else (state, callbacks, scene graph).
//
// Threading: CCVideoSprite calls every method on the GL thread, and the GL thread is
// the only thread that may touch GL (createGLResources, uploadFrame, ...). A backend
// may decode on threads of its own, but it hands frames over in uploadFrame() and
// events over in pollEvent(), so nothing reaches the game from a foreign thread.

// Where the video comes from. offset/length (see CCVideoSprite::create) select a byte
// range of the file, descriptor, handle or buffer.
struct VideoSource
{
	enum class Kind
	{
		File,   // path
		Fd,     // POSIX file descriptor (Android). Not closed by the backend.
		Handle, // Win32 HANDLE. Not closed by the backend.
		Memory  // buffer, which the caller keeps alive for as long as the video exists
	};

	Kind kind = Kind::File;
	std::string path;
	int fd = -1;
	void *handle = nullptr;
	const void *data = nullptr;
	size_t size = 0;

	static VideoSource fromFile(const std::string &path)
	{
		VideoSource source;
		source.kind = Kind::File;
		source.path = path;
		return source;
	}

	static VideoSource fromFd(int fd)
	{
		VideoSource source;
		source.kind = Kind::Fd;
		source.fd = fd;
		return source;
	}

	static VideoSource fromHandle(void *handle)
	{
		VideoSource source;
		source.kind = Kind::Handle;
		source.handle = handle;
		return source;
	}

	static VideoSource fromMemory(const void *data, size_t size)
	{
		VideoSource source;
		source.kind = Kind::Memory;
		source.data = data;
		source.size = size;
		return source;
	}
};

// What the texture the backend renders into is.
enum class VideoTextureKind
{
	Texture2D,   // GL_TEXTURE_2D, sampled with sampler2D
	ExternalOES  // GL_TEXTURE_EXTERNAL_OES (Android SurfaceTexture), needs getTexMatrix()
};

struct VideoInfo
{
	int width = 0;  // pixels
	int height = 0; // pixels
	int64_t durationMs = 0;
	bool hasAudio = false;
};

enum class VideoEventType
{
	Finished, // the end was reached (never sent while looping)
	SeekDone, // the frame at the seek target is ready (see seek())
	Error     // playback failed, message says why. The video is dead afterwards.
};

struct VideoEvent
{
	VideoEventType type = VideoEventType::Finished;
	uint32_t seekId = 0;  // SeekDone: the id the seek was started with
	std::string message;  // Error: what went wrong
};

class IVideoBackend
{
public:
	virtual ~IVideoBackend() {}

	// Opens the container and reads its header (size, duration, tracks). Must be
	// quick: it runs inside CCVideoSprite::create(). Decoders may start later.
	// offset/length select a byte range of the source, length <= 0 meaning "to the end".
	// On failure returns false and says why in error.
	virtual bool open(const VideoSource &source, int64_t offset, int64_t length, VideoInfo &info, std::string &error) = 0;

	// Stops every thread and releases the decoders and the audio. Safe to call twice.
	virtual void close() = 0;

	// (Re)creates the texture the frames go to. Needs the GL context. After this
	// getTextureId() is not 0, and the first frame appears with the next uploadFrame().
	virtual bool createGLResources() = 0;
	virtual void destroyGLResources() = 0;

	// Begins playing from the current position, or from 0 if the end had been reached.
	virtual void start() = 0;
	virtual void pause() = 0;
	virtual void resume() = 0;

	// Frame-accurate seek: afterwards the frame shown is the one on screen at ms.
	// Keeps the running/paused state it found. Exactly one SeekDone event carrying
	// seekId follows, unless another seek() supersedes this one first. A seek also
	// discards a Finished event that has not been polled yet.
	virtual void seek(int64_t ms, uint32_t seekId) = 0;

	virtual void setLoop(bool loop) = 0;
	virtual void setVolume(float volume) = 0; // 0..1

	// Playback position. Audio is the master clock, so this is the audio position
	// when there is audio. While a seek is pending it is the seek target.
	virtual int64_t getPositionMs() const = 0;

	// GL thread: makes the newest decoded frame current in the texture. Returns true
	// when the picture changed.
	virtual bool uploadFrame() = 0;
	virtual unsigned int getTextureId() const = 0;
	virtual VideoTextureKind getTextureKind() const = 0;
	// 4x4 column-major texture matrix (ExternalOES only; identity otherwise).
	virtual const float *getTexMatrix() const = 0;

	// Next pending event, if any.
	virtual bool pollEvent(VideoEvent &event) = 0;

	// The GL context is gone: every GL object the backend owned is invalid.
	// Forget the ids, do not delete them.
	virtual void onContextLost() = 0;
	// A new GL context exists: recreate the GL objects and restore the frame at the
	// position playback had reached.
	virtual bool onContextRestored() = 0;
};

// The backend for this platform (see VideoBackendFactory.cpp).
std::unique_ptr<IVideoBackend> createPlatformVideoBackend();
