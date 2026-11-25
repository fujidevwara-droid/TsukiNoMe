#include "ALPcmSink.h"

#include <algorithm>

#include "Defines.h"
#include <AL/alext.h>

namespace
{
	const ALenum kSecOffsetLatency = 0x1201; // AL_SEC_OFFSET_LATENCY_SOFT
}

ALPcmSink::ALPcmSink()
{
}

ALPcmSink::~ALPcmSink()
{
	close();
}

bool ALPcmSink::open(int channels, int sampleRate)
{
	close();

	if (channels != 1 && channels != 2)
		return false;
	if (sampleRate <= 0)
		return false;

	// SimpleAudioEngine creates the device and the context the first time it is asked for.
	CocosDenshion::SimpleAudioEngine::sharedEngine();
	if (!alcGetCurrentContext())
		return false;

	alGetError();

	std::lock_guard<std::mutex> lock(_mutex);

	alGenSources(1, &_source);
	if (alGetError() != AL_NO_ERROR)
	{
		_source = 0;
		return false;
	}

	alGenBuffers(kBufferCount, _buffers);
	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteSources(1, &_source);
		_source = 0;
		return false;
	}

	_format = channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	_channels = channels;
	_sampleRate = sampleRate;
	_free.assign(_buffers, _buffers + kBufferCount);
	_queued.clear();
	_wantPlaying = false;
	_baseHns = 0;
	_playedFrames = 0;
	_lastPositionHns = 0;

	alSourcef(_source, AL_GAIN, _volume);
	alSourcei(_source, AL_LOOPING, AL_FALSE);
	alSourcei(_source, AL_SOURCE_RELATIVE, AL_TRUE); // plain stereo, not positioned in space

	if (alIsExtensionPresent("AL_SOFT_source_latency"))
		_getSourceDoubleVector = (GetSourceDoubleVector)alGetProcAddress("alGetSourcedvSOFT");

	return true;
}

void ALPcmSink::close()
{
	std::lock_guard<std::mutex> lock(_mutex);

	if (!_source)
		return;

	alSourceStop(_source);
	alSourcei(_source, AL_BUFFER, 0);
	alDeleteSources(1, &_source);
	alDeleteBuffers(kBufferCount, _buffers);
	_source = 0;
	_free.clear();
	_queued.clear();
	_getSourceDoubleVector = nullptr;
}

void ALPcmSink::flush(int64_t baseHns)
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (!_source)
		return;

	alSourceStop(_source);
	alSourcei(_source, AL_BUFFER, 0); // unqueues everything

	_queued.clear();
	_free.assign(_buffers, _buffers + kBufferCount);
	_playedFrames = 0;
	_baseHns = baseHns;
	_lastPositionHns = baseHns;
}

bool ALPcmSink::hasFreeBuffer()
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (!_source)
		return false;

	service();
	return !_free.empty();
}

bool ALPcmSink::submit(const int16_t *samples, size_t frames)
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (!_source || frames == 0)
		return false;

	service();
	if (_free.empty())
		return false;

	const ALuint buffer = _free.back();
	_free.pop_back();

	alBufferData(buffer, _format, samples, (ALsizei)(frames * _channels * sizeof(int16_t)), _sampleRate);
	alSourceQueueBuffers(_source, 1, &buffer);
	_queued.push_back({ buffer, frames });

	if (_wantPlaying)
	{
		ALint state = 0;
		alGetSourcei(_source, AL_SOURCE_STATE, &state);
		if (state != AL_PLAYING)
			alSourcePlay(_source);
	}
	return true;
}

void ALPcmSink::play()
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (!_source)
		return;

	_wantPlaying = true;
	if (!_queued.empty())
		alSourcePlay(_source);
}

void ALPcmSink::pause()
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (!_source)
		return;

	_wantPlaying = false;
	alSourcePause(_source);
}

void ALPcmSink::setVolume(float volume)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_volume = volume;
	if (_source)
		alSourcef(_source, AL_GAIN, volume);
}

// Buffers that have been played are taken off the source and counted. If the source ran
// dry (an underrun) and the video is meant to be running, it is started again, which
// only replays what is still queued because everything played was unqueued before.
void ALPcmSink::service()
{
	ALint processed = 0;
	alGetSourcei(_source, AL_BUFFERS_PROCESSED, &processed);

	while (processed > 0 && !_queued.empty())
	{
		ALuint buffer = 0;
		alSourceUnqueueBuffers(_source, 1, &buffer);

		const Queued done = _queued.front();
		_queued.pop_front();
		_playedFrames += (int64_t)done.frames;
		_free.push_back(done.buffer);
		--processed;
	}

	if (_wantPlaying && !_queued.empty())
	{
		ALint state = 0;
		alGetSourcei(_source, AL_SOURCE_STATE, &state);
		if (state != AL_PLAYING)
			alSourcePlay(_source);
	}
}

int64_t ALPcmSink::positionHns()
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (!_source)
		return _lastPositionHns;

	service();

	// The offset counts from the start of what is still queued; what has been unqueued
	// is in _playedFrames. Both are read under one lock, so they agree.
	double seconds = 0.0;
	if (_getSourceDoubleVector)
	{
		ALdouble values[2] = { 0.0, 0.0 }; // offset in the queue, output latency
		_getSourceDoubleVector(_source, kSecOffsetLatency, values);
		seconds = values[0] - values[1];
	}
	else
	{
		ALint offset = 0;
		alGetSourcei(_source, AL_SAMPLE_OFFSET, &offset);
		seconds = (double)offset / _sampleRate;
	}

	const double playedSeconds = (double)_playedFrames / _sampleRate + (std::max)(seconds, 0.0);
	const int64_t position = _baseHns + (int64_t)(playedSeconds * 10000000.0);

	_lastPositionHns = (std::max)(_lastPositionHns, position);
	return _lastPositionHns;
}

bool ALPcmSink::isDrained()
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (!_source)
		return true;

	service();
	return _queued.empty();
}
