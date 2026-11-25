#include <algorithm>

#include "NullVideoBackend.h"
#include "Defines.h"

NullVideoBackend::NullVideoBackend(int width, int height, int64_t durationMs)
	: _width((std::max)(width, 32))
	, _height((std::max)(height, 32))
	, _durationMs((std::max)(durationMs, (int64_t)1))
{
}

bool NullVideoBackend::open(const VideoSource &source, int64_t offset, int64_t length, VideoInfo &info, std::string &error)
{
	_pixels.assign((size_t)_width * _height * 4, 0);
	_open = true;
	_running = false;
	_finished = false;
	_baseMs = 0;
	_seekPending = false;
	_events.clear();
	_lastFrame = -1;
	_dirty = true;

	info.width = _width;
	info.height = _height;
	info.durationMs = _durationMs;
	info.hasAudio = false;
	return true;
}

void NullVideoBackend::close()
{
	if (_running)
		_baseMs = getPositionMs();

	_open = false;
	_running = false;
	_seekPending = false;
	_events.clear();
}

bool NullVideoBackend::createGLResources()
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

	// The first picture is there from the start, so a video that is only added to the
	// scene already shows its first frame.
	const int64_t position = getPositionMs();
	renderPattern(position);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _width, _height, 0, GL_RGBA, GL_UNSIGNED_BYTE, _pixels.data());

	_lastFrame = (int)(position * kFramesPerSecond / 1000);
	_dirty = false;
	return true;
}

void NullVideoBackend::destroyGLResources()
{
	if (_texture != 0)
	{
		ccGLDeleteTexture(_texture);
		_texture = 0;
	}
}

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------

int64_t NullVideoBackend::elapsedMs() const
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - _startTime).count();
}

int64_t NullVideoBackend::getPositionMs() const
{
	if (_finished)
		return _durationMs;

	if (!_running)
		return _baseMs;

	const int64_t position = _baseMs + elapsedMs();
	return _loop ? position % _durationMs : (std::min)(position, _durationMs);
}

// Notices that the end was reached (looping simply wraps around, see getPositionMs()).
void NullVideoBackend::tick()
{
	if (!_running || _loop)
		return;

	if (_baseMs + elapsedMs() >= _durationMs)
	{
		_running = false;
		_finished = true;
		_baseMs = _durationMs;

		VideoEvent event;
		event.type = VideoEventType::Finished;
		_events.push_back(event);
	}
}

void NullVideoBackend::start()
{
	if (!_open)
		return;

	if (_finished || getPositionMs() >= _durationMs)
		_baseMs = 0;

	// a Finished event nobody has picked up yet no longer applies
	_events.erase(std::remove_if(_events.begin(), _events.end(),
		[](const VideoEvent &event) { return event.type == VideoEventType::Finished; }), _events.end());

	_finished = false;
	_running = true;
	_startTime = Clock::now();
}

void NullVideoBackend::pause()
{
	if (!_running)
		return;

	_baseMs = getPositionMs();
	_running = false;
}

void NullVideoBackend::resume()
{
	if (!_open || _running || _finished)
		return;

	_running = true;
	_startTime = Clock::now();
}

void NullVideoBackend::seek(int64_t ms, uint32_t seekId)
{
	if (!_open)
		return;

	_events.erase(std::remove_if(_events.begin(), _events.end(),
		[](const VideoEvent &event) { return event.type == VideoEventType::Finished; }), _events.end());

	_finished = false;
	_baseMs = (std::max)((int64_t)0, (std::min)(ms, _durationMs));
	_startTime = Clock::now(); // matters only while running
	_dirty = true;

	// SeekDone comes with the next uploadFrame(), like a real decoder's. A newer seek
	// before that replaces this one.
	_seekPending = true;
	_seekId = seekId;
}

void NullVideoBackend::setLoop(bool loop)
{
	if (_loop == loop)
		return;

	// keep the position when the way it is computed changes
	const int64_t position = getPositionMs();
	_loop = loop;
	if (_running)
	{
		_baseMs = position;
		_startTime = Clock::now();
	}
}

void NullVideoBackend::setVolume(float volume)
{
	_volume = volume;
}

// ---------------------------------------------------------------------------
// Frames and events
// ---------------------------------------------------------------------------

bool NullVideoBackend::uploadFrame()
{
	if (!_open)
		return false;

	tick();

	bool changed = false;
	if (_texture != 0)
	{
		const int64_t position = getPositionMs();
		const int frame = (int)(position * kFramesPerSecond / 1000);
		if (_dirty || frame != _lastFrame)
		{
			renderPattern(position);
			ccGLBindTexture2D(_texture);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _width, _height, GL_RGBA, GL_UNSIGNED_BYTE, _pixels.data());
			_lastFrame = frame;
			_dirty = false;
			changed = true;
		}
	}

	if (_seekPending)
	{
		_seekPending = false;

		VideoEvent event;
		event.type = VideoEventType::SeekDone;
		event.seekId = _seekId;
		_events.push_back(event);
	}

	return changed;
}

const float *NullVideoBackend::getTexMatrix() const
{
	static const float identity[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1 };
	return identity;
}

bool NullVideoBackend::pollEvent(VideoEvent &event)
{
	if (_events.empty())
		return false;

	event = _events.front();
	_events.pop_front();
	return true;
}

void NullVideoBackend::onContextLost()
{
	_texture = 0; // gone with the context, nothing to delete
}

bool NullVideoBackend::onContextRestored()
{
	_texture = 0;
	_dirty = true;
	return createGLResources();
}

void NullVideoBackend::renderPattern(int64_t positionMs)
{
	const int barX = (int)((positionMs % 2000) * _width / 2000);
	const int progressX = (int)(positionMs * _width / _durationMs);
	const int marker = 16;

	for (int y = 0; y < _height; ++y)
	{
		unsigned char *row = &_pixels[(size_t)y * _width * 4];
		for (int x = 0; x < _width; ++x)
		{
			unsigned char r = (unsigned char)(x * 255 / _width);
			unsigned char g = (unsigned char)(y * 255 / _height);
			unsigned char b = 128;

			if (x >= barX - 3 && x <= barX + 3)
				r = g = b = 255;

			const bool top = y < marker;
			const bool bottom = y >= _height - 22 && y < _height - 22 + marker;
			const bool left = x < marker;
			const bool right = x >= _width - marker;
			if (top && left)
			{
				r = 255; g = 0; b = 0;
			}
			else if (top && right)
			{
				r = 0; g = 255; b = 0;
			}
			else if (bottom && left)
			{
				r = 0; g = 0; b = 255;
			}
			else if (bottom && right)
			{
				r = g = b = 255;
			}

			if (y >= _height - 6)
			{
				if (x < progressX)
				{
					r = 255; g = 200; b = 0;
				}
				else
				{
					r = g = b = 40;
				}
			}

			row[x * 4 + 0] = r;
			row[x * 4 + 1] = g;
			row[x * 4 + 2] = b;
			row[x * 4 + 3] = 255;
		}
	}
}
