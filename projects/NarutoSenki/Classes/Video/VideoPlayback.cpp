#include <algorithm>

#include "VideoPlayback.h"
#include "Defines.h"

VideoPlayback::VideoPlayback()
{
}

VideoPlayback::~VideoPlayback()
{
	release();
}

bool VideoPlayback::init(std::unique_ptr<IVideoBackend> backend, const VideoSource &source, int64_t offset, int64_t length, std::string &error)
{
	if (!backend)
	{
		error = "no video backend";
		return false;
	}

	if (offset < 0)
	{
		error = "negative offset";
		return false;
	}

	VideoInfo info;
	if (!backend->open(source, offset, length, info, error))
	{
		if (error.empty())
			error = "cannot open the video";
		return false;
	}

	if (info.width <= 0 || info.height <= 0)
	{
		error = "the file has no video track";
		backend->close();
		return false;
	}

	if (!backend->createGLResources())
	{
		error = "cannot create the video texture";
		backend->close();
		return false;
	}

	_backend = std::move(backend);
	_info = info;
	_state = State::Ready;
	return true;
}

void VideoPlayback::setCallbackSources(const std::function<void()> *onFinished, const std::function<void()> *onSeekDone, const std::function<void(const std::string &)> *onError)
{
	_onFinished = onFinished;
	_onSeekDone = onSeekDone;
	_onError = onError;
}

void VideoPlayback::release()
{
	if (_backend)
	{
		_backend->close();
		_backend->destroyGLResources();
		_backend.reset();
	}

	_state = State::Released;
	_backendRunning = false;
	_seeking = false;
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void VideoPlayback::play()
{
	if (!_backend)
		return;

	switch (_state)
	{
	case State::Ready:
	case State::Finished:
		_state = State::Playing;
		_backend->start(); // from the current position, or from 0 after the end
		_backendRunning = true;
		syncBackend();
		break;
	case State::Paused:
		resume();
		break;
	default:
		break;
	}
}

void VideoPlayback::pause()
{
	if (!_backend || _state != State::Playing)
		return;

	_state = State::Paused;
	syncBackend();
}

void VideoPlayback::resume()
{
	if (!_backend || _state != State::Paused)
		return;

	_state = State::Playing;
	syncBackend();
}

// The backend runs while the video is playing and nothing has suspended it; this tells it
// when that changes.
void VideoPlayback::syncBackend()
{
	if (!_backend)
		return;

	const bool shouldRun = _state == State::Playing && _suspendMask == 0;
	if (shouldRun == _backendRunning)
		return;

	_backendRunning = shouldRun;
	if (shouldRun)
		_backend->resume();
	else
		_backend->pause();
}

void VideoPlayback::suspend(int reason)
{
	_suspendMask |= reason;
	syncBackend();
}

void VideoPlayback::unsuspend(int reason)
{
	_suspendMask &= ~reason;
	syncBackend();
}

void VideoPlayback::seekTo(int64_t milliseconds)
{
	if (!_backend || _state == State::Error)
		return;

	milliseconds = (std::max)((int64_t)0, (std::min)(milliseconds, _info.durationMs));

	_seeking = true;
	_seekTargetMs = milliseconds;
	++_seekSerial;

	// A finished video seeks to a paused picture; the backend is not running anyway.
	if (_state == State::Finished)
	{
		_state = State::Paused;
		_backendRunning = false;
	}

	_backend->seek(milliseconds, _seekSerial);
}

int64_t VideoPlayback::getCurrentTimeMs() const
{
	if (!_backend)
		return 0;

	if (_state == State::Finished)
		return _info.durationMs;

	if (_seeking)
		return _seekTargetMs;

	return (std::max)((int64_t)0, (std::min)(_backend->getPositionMs(), _info.durationMs));
}

void VideoPlayback::setLoop(bool loop)
{
	_loop = loop;
	if (_backend)
		_backend->setLoop(loop);
}

void VideoPlayback::setVolume(float volume)
{
	_volume = (std::max)(0.0f, (std::min)(volume, 1.0f));
	if (_backend)
		_backend->setVolume(_volume);
}

// ---------------------------------------------------------------------------
// Frame loop and events
// ---------------------------------------------------------------------------

void VideoPlayback::update()
{
	if (!_backend || _state == State::Error)
		return;

	_backend->uploadFrame();

	// A callback may release this video (remove it from its parent): look again each time.
	VideoEvent event;
	while (_backend && _state != State::Error && _backend->pollEvent(event))
		handleEvent(event);
}

void VideoPlayback::handleEvent(const VideoEvent &event)
{
	switch (event.type)
	{
	case VideoEventType::SeekDone:
	{
		// A newer seek has been started since: this one is out of date.
		if (!_seeking || event.seekId != _seekSerial)
			return;

		_seeking = false;
		auto callback = _onSeekDone ? *_onSeekDone : std::function<void()>(); // a copy: it may be replaced meanwhile
		if (callback)
			callback();
		break;
	}
	case VideoEventType::Finished:
	{
		if (_state != State::Playing && _state != State::Paused)
			return;

		_state = State::Finished;
		_backendRunning = false;
		_seeking = false;
		auto callback = _onFinished ? *_onFinished : std::function<void()>();
		if (callback)
			callback();
		break;
	}
	case VideoEventType::Error:
		fail(event.message);
		break;
	}
}

void VideoPlayback::fail(const std::string &message)
{
	if (_state == State::Error || _state == State::Released)
		return;

	CCLOG("CCVideoSprite: %s", message.c_str());

	_state = State::Error;
	_backendRunning = false;
	_seeking = false;

	// Stop decoding and the audio. The texture stays, so the last picture stays on screen.
	if (_backend)
		_backend->close();

	auto callback = _onError ? *_onError : std::function<void(const std::string &)>();
	if (callback)
		callback(message);
}

// ---------------------------------------------------------------------------
// GL context
// ---------------------------------------------------------------------------

void VideoPlayback::contextLost()
{
	if (!_backend || _state == State::Error)
		return;

	// Held until the context is back, so the sound does not run ahead of the picture.
	suspend(kSuspendContext);
	_backend->onContextLost();
}

void VideoPlayback::contextRestored()
{
	if (!_backend || _state == State::Error)
		return;

	if (!_backend->onContextRestored())
	{
		fail("cannot restore the video after the graphics context was recreated");
		return;
	}

	unsuspend(kSuspendContext);
}
