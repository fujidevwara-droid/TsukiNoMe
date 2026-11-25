#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "IVideoBackend.h"

// A platform's media API, reduced to what PipelineBackend needs: an independent video
// reader and audio reader over the same file. The video reader is used by the video
// thread only, the audio reader by the audio thread only, so neither needs to be
// thread-safe, and both are seeked and read strictly one call at a time.
class IPipelineSource
{
public:
	enum class Read
	{
		Item,  // something was read
		End,   // the end of the stream
		Failed
	};

	virtual ~IPipelineSource() {}

	// Called first and last on each decoding thread (e.g. to set up and tear down COM).
	virtual void onThreadStart() {}
	virtual void onThreadEnd() {}

	// Moves the video reader to the sync frame at or before hns.
	virtual bool seekVideo(int64_t hns, std::string &error) = 0;

	// Decodes the next picture into bgra (width * height * 4 bytes, top row first) and
	// tells its time and how long it lasts, in 100 ns units.
	virtual Read readVideo(int64_t &ptsHns, int64_t &durationHns, uint8_t *bgra, std::string &error) = 0;

	virtual bool seekAudio(int64_t hns) = 0;

	// Decodes the next chunk of interleaved 16-bit PCM (whole frames) and tells the
	// time of its first sample.
	virtual Read readAudio(std::vector<int16_t> &pcm, int64_t &startHns) = 0;
};

// The sound output: a queue of PCM that is played in real time, with a position that is
// the master clock (see ALPcmSink for the contract).
class IPipelineAudio
{
public:
	virtual ~IPipelineAudio() {}

	virtual void close() = 0;
	virtual void flush(int64_t baseHns) = 0;
	virtual bool submit(const int16_t *samples, size_t frames) = 0;
	virtual bool hasFreeBuffer() = 0;
	virtual void play() = 0;
	virtual void pause() = 0;
	virtual void setVolume(float volume) = 0;
	virtual int64_t positionHns() = 0;
	virtual bool isDrained() = 0;
};

// Everything about playing a video that does not depend on the platform: the decoding
// threads and their queue, the master clock and its fallback, frame pacing, frame-accurate
// seeking, looping, the end of the video, and getting the pictures into a GL texture.
// A platform backend only provides an IPipelineSource and an IPipelineAudio (and opens
// the file); see MFVideoBackend.
//
//   video thread: readVideo -> BGRA frame -> small queue (kMaxQueuedFrames)
//   audio thread: readAudio -> 16-bit PCM -> IPipelineAudio
//   GL thread:    uploadFrame() shows the newest frame whose time has come
//
// Audio is the master clock: a frame is shown when the audio position has reached its
// time, frames that are already late are dropped. A file without a usable audio track,
// and the stretch after the audio has ended, run on the system clock instead.
//
// Seeking is frame-accurate: each thread seeks its reader to the keyframe before the
// target and decodes forward; the video drops frames that end before the target, the
// audio drops samples before it. The seek is completed (and SeekDone sent) when both are
// ready.
class PipelineBackend : public IVideoBackend
{
public:
	PipelineBackend();
	virtual ~PipelineBackend();

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

protected:
	struct Config
	{
		int width = 0;
		int height = 0;
		int64_t durationHns = 0;
		int64_t frameDurationHns = 333333; // when a picture does not say how long it lasts
		bool hasAudio = false;
		int audioChannels = 0;
		int audioRate = 0;
	};

	// Takes over the readers and starts the decoding threads. audio may be null (no
	// sound). Fills info. Call from open(), on the GL thread.
	bool begin(std::unique_ptr<IPipelineSource> source, std::unique_ptr<IPipelineAudio> audio, const Config &config, VideoInfo &info, std::string &error);

private:
	struct Frame
	{
		int64_t ptsHns = 0;
		int64_t durationHns = 0;
		std::vector<uint8_t> pixels; // BGRA, top row first, width * height * 4
	};

	typedef std::unique_ptr<Frame> FramePtr;

	enum class ReadResult
	{
		Frame,
		EndOfStream,
		Failed
	};

	static constexpr size_t kMaxQueuedFrames = 4;

	// threads
	void videoLoop();
	void audioLoop();
	ReadResult readVideoFrame(FramePtr &frame);
	bool readAudioChunk(std::vector<int16_t> &pcm, int64_t &startHns, bool &endOfStream);
	bool seekVideo(int64_t targetHns, uint32_t epoch);
	bool seekAudio(int64_t targetHns, uint32_t epoch, std::vector<int16_t> &staging, bool &readerEnded);
	bool isCurrent(uint32_t epoch);
	void reportError(const std::string &message);

	// frames and buffers
	FramePtr acquireFrame();
	void recycle(FramePtr frame);
	void recycleLocked(FramePtr frame); // _mutex held

	// control
	void beginSeek(int64_t targetHns, uint32_t seekId, bool notify);
	void applyRunning();
	int64_t positionHns() const;
	int64_t systemPositionHns() const;
	bool usingAudioClock() const;

	void uploadToTexture(const Frame &frame);
	bool createTexture();

	std::unique_ptr<IPipelineSource> _source;
	std::unique_ptr<IPipelineAudio> _audio;
	int _width = 0;
	int _height = 0;
	int64_t _durationHns = 0;
	int64_t _frameDurationHns = 333333;
	bool _hasAudio = false;
	int _audioChannels = 0;
	int _audioRate = 0;
	bool _open = false;

	std::thread _videoThread;
	std::thread _audioThread;

	// shared between the GL thread and the two decode threads
	std::mutex _mutex;
	std::condition_variable _wake;
	bool _stop = false;
	uint32_t _epoch = 0;               // grows with every seek; threads notice and restart
	int64_t _seekTargetHns = 0;
	uint32_t _seekId = 0;
	bool _seekNotify = false;
	bool _seekPending = false;         // a seek was asked for and has not been completed yet
	bool _seekVideoDone = false;
	bool _seekAudioDone = false;
	FramePtr _seekFrame;               // the picture at the seek target
	std::deque<FramePtr> _frames;      // decoded, not shown yet
	std::vector<FramePtr> _pool;       // buffers to reuse
	bool _videoEos = false;
	bool _audioEos = false;            // everything has been handed to the sink
	int64_t _lastVideoEndHns = 0;
	std::deque<VideoEvent> _events;
	bool _errorReported = false;

	// GL thread only
	bool _running = false;             // the user's wish: play
	bool _finished = false;
	bool _loop = false;
	float _volume = 1.0f;
	bool _audioClockDone = false;      // the audio has played out: the system clock takes over
	int64_t _sysBaseHns = 0;
	std::chrono::steady_clock::time_point _sysStart;
	bool _sysRunning = false;
	unsigned int _texture = 0;
	FramePtr _current;                 // what the texture shows (kept to restore it)
};
