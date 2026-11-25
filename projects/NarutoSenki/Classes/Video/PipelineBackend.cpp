#include <algorithm>
#include <cstring>

#include "PipelineBackend.h"
#include "Defines.h"

namespace
{
	const int64_t kHnsPerMs = 10000;
	const int64_t kPresentLeadHns = 50000; // a frame is shown up to 5 ms before its time
}

PipelineBackend::PipelineBackend()
{
}

PipelineBackend::~PipelineBackend()
{
	PipelineBackend::close();
}

// ---------------------------------------------------------------------------
// Starting and stopping
// ---------------------------------------------------------------------------

bool PipelineBackend::begin(std::unique_ptr<IPipelineSource> source, std::unique_ptr<IPipelineAudio> audio, const Config &config, VideoInfo &info, std::string &error)
{
	if (_open)
	{
		error = "the video is already open";
		return false;
	}

	if (!source || config.width <= 0 || config.height <= 0)
	{
		error = "the file has no video track";
		return false;
	}

	GLint maxTextureSize = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
	if (maxTextureSize > 0 && (config.width > maxTextureSize || config.height > maxTextureSize))
	{
		error = "the video is bigger than the largest texture this graphics card supports";
		return false;
	}

	_source = std::move(source);
	_audio = std::move(audio);
	_width = config.width;
	_height = config.height;
	_durationHns = config.durationHns;
	_frameDurationHns = config.frameDurationHns;
	_audioChannels = config.audioChannels;
	_audioRate = config.audioRate;
	_hasAudio = config.hasAudio && _audio && _audioChannels > 0 && _audioRate > 0;

	if (_hasAudio)
	{
		_audio->setVolume(_volume);
		_audio->flush(0);
	}

	_stop = false;
	_epoch = 0;
	_seekPending = false;
	_videoEos = false;
	_audioEos = false;
	_errorReported = false;
	_finished = false;
	_running = false;
	_audioClockDone = false;
	_sysBaseHns = 0;
	_sysRunning = false;
	_open = true;

	// Start with a silent seek to 0: it lines the audio up with the video on the time of
	// the first audio sample (AAC tracks often start a little after 0) instead of assuming 0.
	beginSeek(0, 0, false);

	_videoThread = std::thread(&PipelineBackend::videoLoop, this);
	if (_hasAudio)
		_audioThread = std::thread(&PipelineBackend::audioLoop, this);

	info.width = _width;
	info.height = _height;
	info.durationMs = _durationHns / kHnsPerMs;
	info.hasAudio = _hasAudio;
	return true;
}

void PipelineBackend::close()
{
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_stop = true;
	}
	_wake.notify_all();

	if (_videoThread.joinable())
		_videoThread.join();
	if (_audioThread.joinable())
		_audioThread.join();

	if (_audio)
		_audio->close();
	_audio.reset();
	_source.reset(); // releases the readers

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_frames.clear();
		_pool.clear();
		_seekFrame.reset();
		_events.clear();
	}

	_open = false;
	_running = false;
	_hasAudio = false;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

PipelineBackend::ReadResult PipelineBackend::readVideoFrame(FramePtr &out)
{
	FramePtr frame = acquireFrame();
	frame->pixels.resize((size_t)_width * (size_t)_height * 4);

	std::string error;
	int64_t duration = 0;
	switch (_source->readVideo(frame->ptsHns, duration, frame->pixels.data(), error))
	{
	case IPipelineSource::Read::Item:
		frame->durationHns = duration > 0 ? duration : _frameDurationHns;
		out = std::move(frame);
		return ReadResult::Frame;
	case IPipelineSource::Read::End:
		recycle(std::move(frame));
		return ReadResult::EndOfStream;
	default:
		recycle(std::move(frame));
		reportError(error.empty() ? "video decoding failed" : error);
		return ReadResult::Failed;
	}
}

bool PipelineBackend::readAudioChunk(std::vector<int16_t> &pcm, int64_t &startHns, bool &endOfStream)
{
	pcm.clear();
	endOfStream = false;

	switch (_source->readAudio(pcm, startHns))
	{
	case IPipelineSource::Read::Item:
		return true;
	case IPipelineSource::Read::End:
		endOfStream = true;
		return true;
	default:
		CCLOG("PipelineBackend: audio decoding failed");
		return false;
	}
}

// ---------------------------------------------------------------------------
// Frames and buffers
// ---------------------------------------------------------------------------

PipelineBackend::FramePtr PipelineBackend::acquireFrame()
{
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (!_pool.empty())
		{
			FramePtr frame = std::move(_pool.back());
			_pool.pop_back();
			return frame;
		}
	}
	return FramePtr(new Frame());
}

void PipelineBackend::recycle(FramePtr frame)
{
	std::lock_guard<std::mutex> lock(_mutex);
	recycleLocked(std::move(frame));
}

void PipelineBackend::recycleLocked(FramePtr frame)
{
	if (frame && _pool.size() < kMaxQueuedFrames + 3)
		_pool.push_back(std::move(frame));
}

bool PipelineBackend::isCurrent(uint32_t epoch)
{
	std::lock_guard<std::mutex> lock(_mutex);
	return !_stop && _epoch == epoch;
}

void PipelineBackend::reportError(const std::string &message)
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (_errorReported)
		return;

	_errorReported = true;
	VideoEvent event;
	event.type = VideoEventType::Error;
	event.message = message;
	_events.push_back(event);
}

// Seeks the reader to the keyframe before the target and decodes forward until the
// picture that is on screen at the target: the first one that does not end before it.
// That picture is parked in _seekFrame for the GL thread to show.
bool PipelineBackend::seekVideo(int64_t targetHns, uint32_t epoch)
{
	std::string seekError;
	if (!_source->seekVideo(targetHns, seekError))
	{
		reportError(seekError.empty() ? "seeking failed" : seekError);
		return false;
	}

	FramePtr last;  // the newest picture that ended before the target
	FramePtr found;
	bool endReached = false;

	for (;;)
	{
		if (!isCurrent(epoch))
		{
			recycle(std::move(last));
			return true; // another seek (or closing) took over
		}

		FramePtr frame;
		const ReadResult result = readVideoFrame(frame);
		if (result == ReadResult::Failed)
			return false;

		if (result == ReadResult::EndOfStream)
		{
			// The target is behind the last picture: show that one.
			found = std::move(last);
			endReached = true;
			break;
		}

		if (frame->ptsHns + frame->durationHns <= targetHns)
		{
			recycle(std::move(last));
			last = std::move(frame);
			continue;
		}

		found = std::move(frame);
		recycle(std::move(last));
		break;
	}

	std::lock_guard<std::mutex> lock(_mutex);
	if (_epoch != epoch)
	{
		recycleLocked(std::move(found));
		return true;
	}

	if (found)
		_lastVideoEndHns = (std::max)(_lastVideoEndHns, found->ptsHns + found->durationHns);
	_seekFrame = std::move(found);
	_videoEos = endReached;
	_seekVideoDone = true;
	return true;
}

void PipelineBackend::videoLoop()
{
	_source->onThreadStart();

	uint32_t epoch = 0;
	for (;;)
	{
		bool seeking = false;
		int64_t seekTarget = 0;
		bool stop = false;
		{
			std::unique_lock<std::mutex> lock(_mutex);
			for (;;)
			{
				if (_stop)
				{
					stop = true;
					break;
				}
				if (_epoch != epoch)
				{
					epoch = _epoch;
					seekTarget = _seekTargetHns;
					seeking = true;
					break;
				}
				if (!_videoEos && _frames.size() < kMaxQueuedFrames)
					break;

				_wake.wait(lock);
			}
		}

		if (stop)
			break;

		if (seeking)
		{
			if (!seekVideo(seekTarget, epoch))
				break;
			continue;
		}

		FramePtr frame;
		const ReadResult result = readVideoFrame(frame);
		if (result == ReadResult::Failed)
			break;

		std::lock_guard<std::mutex> lock(_mutex);
		if (_epoch != epoch)
		{
			recycleLocked(std::move(frame)); // from before a seek: no use any more
			continue;
		}

		if (result == ReadResult::EndOfStream)
		{
			_videoEos = true;
		}
		else
		{
			_lastVideoEndHns = (std::max)(_lastVideoEndHns, frame->ptsHns + frame->durationHns);
			_frames.push_back(std::move(frame));
		}
	}

	_source->onThreadEnd();
}

// Seeks the audio reader and drops everything before the target, down to the sample.
// What is left of the first chunk goes to staging; the sink's clock starts at the time
// of its first sample.
bool PipelineBackend::seekAudio(int64_t targetHns, uint32_t epoch, std::vector<int16_t> &staging, bool &readerEnded)
{
	_audio->pause();
	_audio->flush(targetHns); // silence right away

	if (!_source->seekAudio(targetHns))
	{
		CCLOG("PipelineBackend: audio seek failed");
		return false;
	}

	int64_t base = targetHns;
	readerEnded = false;

	for (;;)
	{
		if (!isCurrent(epoch))
			return true;

		std::vector<int16_t> pcm;
		int64_t start = 0;
		bool ended = false;
		if (!readAudioChunk(pcm, start, ended))
			return false;

		if (ended)
		{
			readerEnded = true; // the audio ends before the target: nothing to play
			break;
		}

		const size_t frames = pcm.size() / (size_t)_audioChannels;
		const int64_t end = start + (int64_t)frames * 10000000 / _audioRate;
		if (end <= targetHns)
			continue;

		size_t skip = 0;
		if (start < targetHns)
		{
			skip = (size_t)((targetHns - start) * _audioRate / 10000000);
			skip = (std::min)(skip, frames);
			start += (int64_t)skip * 10000000 / _audioRate;
		}

		base = (std::max)(start, targetHns);
		staging.assign(pcm.begin() + skip * _audioChannels, pcm.end());
		break;
	}

	_audio->flush(base); // nothing is queued yet: this only sets where the clock starts

	std::lock_guard<std::mutex> lock(_mutex);
	if (_epoch == epoch)
	{
		_audioEos = readerEnded && staging.empty();
		_seekAudioDone = true;
	}
	return true;
}

void PipelineBackend::audioLoop()
{
	_source->onThreadStart();

	std::vector<int16_t> staging;  // PCM waiting for a free buffer
	bool readerEnded = false;      // the reader has no more audio (for this seek epoch)
	uint32_t epoch = 0;
	const size_t chunkFrames = (size_t)(_audioRate / 20); // about 50 ms per buffer

	for (;;)
	{
		bool seeking = false;
		int64_t seekTarget = 0;
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_stop)
				break;
			if (_epoch != epoch)
			{
				epoch = _epoch;
				seekTarget = _seekTargetHns;
				seeking = true;
			}
		}

		if (seeking)
		{
			staging.clear();
			readerEnded = false;
			if (!seekAudio(seekTarget, epoch, staging, readerEnded))
				break;
			continue;
		}

		_audio->hasFreeBuffer(); // also what restarts the sound after an underrun

		bool didWork = false;
		const size_t staged = staging.size() / (size_t)_audioChannels;
		if (staged >= chunkFrames || (readerEnded && staged > 0))
		{
			if (_audio->submit(staging.data(), staged))
			{
				staging.clear();
				didWork = true;
			}
		}
		else if (!readerEnded)
		{
			std::vector<int16_t> pcm;
			int64_t start = 0;
			bool ended = false;
			if (!readAudioChunk(pcm, start, ended))
				break;

			if (ended)
				readerEnded = true;
			else
				staging.insert(staging.end(), pcm.begin(), pcm.end());
			didWork = true;
		}

		if (readerEnded && staging.empty())
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_epoch == epoch)
				_audioEos = true; // all of it is with the sink
		}

		if (!didWork)
		{
			std::unique_lock<std::mutex> lock(_mutex);
			if (!_stop && _epoch == epoch)
				_wake.wait_for(lock, std::chrono::milliseconds(8));
		}
	}

	// A broken audio track must not hold the video up: the clock moves to the system clock.
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (!_stop)
		{
			_audioEos = true;
			if (_seekPending && !_seekAudioDone)
				_seekAudioDone = true;
		}
	}

	_source->onThreadEnd();
}

bool PipelineBackend::usingAudioClock() const
{
	return _hasAudio && !_audioClockDone;
}

int64_t PipelineBackend::systemPositionHns() const
{
	if (!_sysRunning)
		return _sysBaseHns;

	const auto elapsed = std::chrono::steady_clock::now() - _sysStart;
	return _sysBaseHns + std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() * 10;
}

int64_t PipelineBackend::positionHns() const
{
	int64_t position;
	if (_seekPending)
		position = _seekTargetHns;
	else if (_finished)
		position = _durationHns;
	else if (usingAudioClock())
		position = _audio->positionHns();
	else
		position = systemPositionHns();

	if (_durationHns > 0)
		position = (std::min)(position, _durationHns);
	return (std::max)(position, (int64_t)0);
}

int64_t PipelineBackend::getPositionMs() const
{
	return positionHns() / kHnsPerMs;
}

// The clocks run while the user wants to play and no seek is in progress.
void PipelineBackend::applyRunning()
{
	const bool run = _open && _running && !_finished && !_seekPending;

	if (usingAudioClock())
	{
		if (run)
			_audio->play();
		else
			_audio->pause();
		return;
	}

	if (run && !_sysRunning)
	{
		_sysStart = std::chrono::steady_clock::now();
		_sysRunning = true;
	}
	else if (!run && _sysRunning)
	{
		_sysBaseHns = systemPositionHns();
		_sysRunning = false;
	}
}

void PipelineBackend::start()
{
	if (!_open)
		return;

	if (_finished)
		beginSeek(0, 0, false);

	_running = true;
	applyRunning();
}

void PipelineBackend::pause()
{
	_running = false;
	applyRunning();
}

void PipelineBackend::resume()
{
	_running = true;
	applyRunning();
}

void PipelineBackend::seek(int64_t ms, uint32_t seekId)
{
	beginSeek(ms * kHnsPerMs, seekId, true);
}

void PipelineBackend::beginSeek(int64_t targetHns, uint32_t seekId, bool notify)
{
	if (!_open)
		return;

	targetHns = (std::max)((int64_t)0, targetHns);
	if (_durationHns > 0)
		targetHns = (std::min)(targetHns, _durationHns);

	// The old picture and the old sound are no use any more. Pausing first makes sure
	// that audio the audio thread still hands over from before the seek is not played.
	_finished = false;
	_audioClockDone = false;
	if (_hasAudio)
	{
		_audio->pause();
		_audio->flush(targetHns);
	}
	_sysBaseHns = targetHns;
	_sysRunning = false;

	{
		std::lock_guard<std::mutex> lock(_mutex);
		++_epoch;
		_seekTargetHns = targetHns;
		_seekId = seekId;
		_seekNotify = notify;
		_seekPending = true;
		_seekVideoDone = false;
		_seekAudioDone = !_hasAudio;
		recycleLocked(std::move(_seekFrame));
		for (FramePtr &frame : _frames)
			recycleLocked(std::move(frame));
		_frames.clear();
		_videoEos = false;
		_audioEos = false;

		// a Finished event that was not picked up yet is out of date
		_events.erase(std::remove_if(_events.begin(), _events.end(),
			[](const VideoEvent &event) { return event.type == VideoEventType::Finished; }), _events.end());
	}

	_wake.notify_all();
}

void PipelineBackend::setLoop(bool loop)
{
	_loop = loop;
}

void PipelineBackend::setVolume(float volume)
{
	_volume = volume;
	if (_audio)
		_audio->setVolume(volume);
}

bool PipelineBackend::pollEvent(VideoEvent &event)
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (_events.empty())
		return false;

	event = _events.front();
	_events.pop_front();
	return true;
}

const float *PipelineBackend::getTexMatrix() const
{
	static const float identity[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1 };
	return identity;
}

bool PipelineBackend::createTexture()
{
	if (_texture != 0)
		return true;

	glGenTextures(1, &_texture);
	if (_texture == 0)
		return false;

	ccGLBindTexture2D(_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	// the picture that was on screen, or black
	if (_current)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _width, _height, 0, GL_BGRA, GL_UNSIGNED_BYTE, _current->pixels.data());
	}
	else
	{
		const std::vector<uint8_t> black((size_t)_width * _height * 4, 0);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _width, _height, 0, GL_BGRA, GL_UNSIGNED_BYTE, black.data());
	}
	return true;
}

bool PipelineBackend::createGLResources()
{
	return createTexture();
}

void PipelineBackend::destroyGLResources()
{
	if (_texture != 0)
	{
		ccGLDeleteTexture(_texture);
		_texture = 0;
	}
}

void PipelineBackend::uploadToTexture(const Frame &frame)
{
	ccGLBindTexture2D(_texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _width, _height, GL_BGRA, GL_UNSIGNED_BYTE, frame.pixels.data());
}

bool PipelineBackend::uploadFrame()
{
	if (!_open || _texture == 0)
		return false;

	FramePtr toShow;
	bool seekCommitted = false;
	bool audioDrained = false;
	bool finishNow = false;
	bool loopNow = false;

	{
		std::lock_guard<std::mutex> lock(_mutex);

		if (_seekPending)
		{
			// Both decoders have reached the target: show its picture and let time run.
			if (_seekVideoDone && _seekAudioDone)
			{
				_seekPending = false;
				seekCommitted = true;
				toShow = std::move(_seekFrame);

				if (_seekNotify)
				{
					VideoEvent event;
					event.type = VideoEventType::SeekDone;
					event.seekId = _seekId;
					_events.push_back(event);
				}
			}
		}
		else
		{
			// Show the newest picture whose time has come; older ones are late, drop them.
			// A frame is shown a little early to make up for scheduling, but only while time is
			// passing: a paused video must not show a frame that is ahead of its position.
			const int64_t clock = positionHns();
			const int64_t lead = _running ? kPresentLeadHns : 0;
			while (!_frames.empty() && _frames.front()->ptsHns <= clock + lead)
			{
				recycleLocked(std::move(toShow));
				toShow = std::move(_frames.front());
				_frames.pop_front();
			}

			if (_running && !_finished)
			{
				if (_hasAudio && !_audioClockDone && _audioEos && _audio->isDrained())
					audioDrained = true;

				const bool audioDone = !_hasAudio || _audioClockDone || audioDrained;
				if (_videoEos && _frames.empty() && audioDone && clock >= _lastVideoEndHns)
				{
					if (_loop)
						loopNow = true;
					else
						finishNow = true;
				}
			}
		}
	}

	if (seekCommitted)
		applyRunning();

	// The sound has played out: from here on the system clock carries on from where
	// the audio clock stopped.
	if (audioDrained)
	{
		_sysBaseHns = _audio->positionHns();
		_sysRunning = false;
		_audioClockDone = true;
		applyRunning();
	}

	if (finishNow)
	{
		_finished = true;
		_running = false;
		applyRunning();

		std::lock_guard<std::mutex> lock(_mutex);
		VideoEvent event;
		event.type = VideoEventType::Finished;
		_events.push_back(event);
	}
	else if (loopNow)
	{
		beginSeek(0, 0, false); // back to the start without telling anybody
	}

	if (!toShow)
		return false;

	uploadToTexture(*toShow);
	recycle(std::move(_current));
	_current = std::move(toShow);
	_wake.notify_all(); // the video thread may have been waiting for room
	return true;
}

void PipelineBackend::onContextLost()
{
	_texture = 0; // gone with the context, nothing to delete
}

bool PipelineBackend::onContextRestored()
{
	_texture = 0;
	return createTexture(); // shows the picture that was on screen
}
