#pragma once
#include "IVideoBackend.h"

#include <chrono>
#include <deque>
#include <vector>

// A backend that plays a test pattern instead of a file: no decoding, no audio, no
// threads. It exists to exercise everything around the backend (CCVideoSprite's
// state machine, callbacks, shader, scene-graph behaviour, seeking, looping) on any
// platform, and it is what createPlatformVideoBackend() returns where there is no real
// backend. The source passed to open() is ignored.
//
// The picture is a colour gradient with a white bar that sweeps across every two
// seconds, a progress bar along the bottom and four corner markers (red top left,
// green top right, blue bottom left, white bottom right) so that a flipped or
// mirrored picture is easy to spot. It runs at 30 frames per second.
class NullVideoBackend : public IVideoBackend
{
public:
	explicit NullVideoBackend(int width = 320, int height = 180, int64_t durationMs = 10000);

	virtual bool open(const VideoSource &source, int64_t offset, int64_t length, VideoInfo &info, std::string &error);
	virtual void close();

	virtual bool createGLResources();
	virtual void destroyGLResources();

	virtual void start();
	virtual void pause();
	virtual void resume();
	virtual void seek(int64_t ms, uint32_t seekId);
	virtual void setLoop(bool loop);
	virtual void setVolume(float volume);

	virtual int64_t getPositionMs() const;

	virtual bool uploadFrame();
	virtual unsigned int getTextureId() const { return _texture; }
	virtual VideoTextureKind getTextureKind() const { return VideoTextureKind::Texture2D; }
	virtual const float *getTexMatrix() const;

	virtual bool pollEvent(VideoEvent &event);

	virtual void onContextLost();
	virtual bool onContextRestored();

private:
	static constexpr int kFramesPerSecond = 30;

	using Clock = std::chrono::steady_clock;

	int64_t elapsedMs() const;
	void tick();
	void renderPattern(int64_t positionMs);

	int _width;
	int _height;
	int64_t _durationMs;

	bool _open = false;
	bool _running = false;
	bool _finished = false;
	bool _loop = false;
	float _volume = 1.0f;

	int64_t _baseMs = 0;           // position when the clock was last (re)started
	Clock::time_point _startTime;  // when it was

	bool _seekPending = false;
	uint32_t _seekId = 0;

	std::deque<VideoEvent> _events;

	std::vector<unsigned char> _pixels; // RGBA
	unsigned int _texture = 0;
	int _lastFrame = -1;
	bool _dirty = true; // the texture has to be uploaded again
};
