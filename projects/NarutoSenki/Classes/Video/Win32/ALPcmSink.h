#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include <AL/al.h>
#include <AL/alc.h>

#include "../PipelineBackend.h"

// Plays 16-bit PCM through OpenAL and reports how much of it has been heard.
//
// It uses the OpenAL context SimpleAudioEngine already owns (making it exist if needed)
// with a source and buffers of its own, so SimpleAudioEngine's music and effects are not
// affected and do not affect it. The position it reports is the audio master clock of
// MFVideoBackend: baseHns + the time of all audio that has been played, minus the
// output latency where the AL_SOFT_source_latency extension says what that is.
//
// Thread safety: every method may be called from any thread.
class ALPcmSink : public IPipelineAudio
{
public:
	ALPcmSink();
	virtual ~ALPcmSink();

	// Fails when there is no OpenAL device or the format is not supported.
	bool open(int channels, int sampleRate);
	void close();
	bool isOpen() const { return _source != 0; }

	// Stops playing, throws away everything queued and makes baseHns the position
	// that the next sample submitted corresponds to. Playing is not resumed; call
	// play() when there is data (and the video is meant to be running).
	void flush(int64_t baseHns);

	// Queues frames of interleaved 16-bit samples. Returns false if there is no free
	// buffer right now (hasFreeBuffer()), nothing is queued then.
	bool submit(const int16_t *samples, size_t frames);
	bool hasFreeBuffer();

	// Starts (or continues) playing as soon as there is something to play, and keeps
	// doing so after an underrun, until pause().
	void play();
	void pause();

	void setVolume(float volume);

	// How far the audio has been played, in 100 ns units, starting at the base given
	// to flush(). Never goes backwards between two flush() calls.
	int64_t positionHns();

	// Everything that was submitted has been played.
	bool isDrained();

	int sampleRate() const { return _sampleRate; }

private:
	struct Queued
	{
		ALuint buffer;
		size_t frames;
	};

	static constexpr int kBufferCount = 8;

	void service(); // unqueue what was played, restart after an underrun. _mutex held.

	std::mutex _mutex;
	ALuint _source = 0;
	ALuint _buffers[kBufferCount] = {};
	std::vector<ALuint> _free;
	std::deque<Queued> _queued;
	ALenum _format = 0;
	int _channels = 0;
	int _sampleRate = 0;
	bool _wantPlaying = false;
	int64_t _baseHns = 0;
	int64_t _playedFrames = 0; // of the buffers that have been unqueued
	int64_t _lastPositionHns = 0;
	float _volume = 1.0f;

	typedef void (AL_APIENTRY *GetSourceDoubleVector)(ALuint source, ALenum param, ALdouble *values);
	GetSourceDoubleVector _getSourceDoubleVector = nullptr; // AL_SOFT_source_latency, if present
};
