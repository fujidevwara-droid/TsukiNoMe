#pragma once
// Fakes and helpers shared by the native tests (include from one translation unit only):
//  - GL entry points that record which picture was uploaded (its frame index is in the
//    first four bytes of the fake pictures);
//  - FakeSource: a fake media file, 30 fps with a sync frame every second, whose frame i
//    is at i*1e7/30 (100 ns) and whose sound counts its samples, with options for the
//    length, a late start, slow seeks and a decoder that fails;
//  - FakeAudio: an audio output that plays in real time with 8 buffers and a position;
//  - TestBackend: a PipelineBackend over those two;
//  - check(), section(), waitFor(), sleepMs().
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <thread>

#include <GL/glew.h>
#include "Defines.h"
#include "Video/PipelineBackend.h"

// ------------------------------------------------------------------ GL stubs
static std::mutex g_glMutex;
static std::vector<int64_t> g_uploaded;       // frame index of every upload (first 4 bytes of the picture)
static std::atomic<int> g_fullUploads(0);     // glTexImage2D: creating or restoring the texture
static int g_nextTexture = 1;
static int g_maxTextureSize = 4096;

static void record(const void *pixels)
{
	if (!pixels) return;
	uint32_t index;
	std::memcpy(&index, pixels, 4);
	std::lock_guard<std::mutex> lock(g_glMutex);
	g_uploaded.push_back(index);
}

extern "C" {
void glGenTextures(GLsizei n, GLuint *t) { for (int i = 0; i < n; ++i) t[i] = g_nextTexture++; }
void glTexParameteri(GLenum, GLenum, GLint) {}
void glPixelStorei(GLenum, GLint) {}
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *pixels) { ++g_fullUploads; record(pixels); }
void glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *pixels) { record(pixels); }
void glGetIntegerv(GLenum pname, GLint *v) { *v = g_maxTextureSize; }
}
namespace cocos2d {
CCPoint::CCPoint() { x = y = 0; }
CCSize::CCSize() { width = height = 0; }
CCPoint::CCPoint(float a, float b) { x = a; y = b; }
CCSize::CCSize(float a, float b) { width = a; height = b; }
CCRect::CCRect(float a, float b, float c, float d) { origin.x = a; origin.y = b; size.width = c; size.height = d; }
void ccGLBindTexture2D(GLuint) {}
void ccGLDeleteTexture(GLuint) {}
}

static int64_t lastUploaded()
{
	std::lock_guard<std::mutex> lock(g_glMutex);
	return g_uploaded.empty() ? -1 : (int64_t)g_uploaded.back();
}
static size_t uploadCount() { std::lock_guard<std::mutex> lock(g_glMutex); return g_uploaded.size(); }

// ------------------------------------------------------------------ the fake media
static const int64_t HNS = 10000000;
static const int kRate = 48000, kUnit = 1024;

struct Spec
{
	int64_t durationHns = 3 * HNS, audioDurationHns = -1, audioStartHns = 0, videoStartHns = 0;
	int fps = 30, key = 30;
	bool audio = true;
	int64_t audioSeekDelayMs = 0, videoSeekDelayMs = 0;
	int width = 16, height = 8;
	int failAtFrame = -1;   // the video reader fails when it reaches this frame
};

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

class FakeSource : public IPipelineSource
{
public:
	explicit FakeSource(const Spec &s) : _s(s) { if (_s.audioDurationHns < 0) _s.audioDurationHns = _s.durationHns; }

	int videoCount() const { return (int)((_s.durationHns * _s.fps + HNS - 1) / HNS); }
	int64_t videoPts(int i) const { return _s.videoStartHns + (int64_t)i * HNS / _s.fps; }
	int audioCount() const { return (int)(((_s.audioDurationHns * kRate / HNS) + kUnit - 1) / kUnit); }
	int64_t audioPts(int j) const { return _s.audioStartHns + (int64_t)j * kUnit * HNS / kRate; }

	bool seekVideo(int64_t hns, std::string &) override
	{
		if (_s.videoSeekDelayMs) sleepMs((int)_s.videoSeekDelayMs);
		int idx = 0;
		for (int i = 0; i < videoCount(); ++i) if (videoPts(i) <= hns) idx = i;
		_vcursor = idx - idx % _s.key;
		return true;
	}
	Read readVideo(int64_t &pts, int64_t &duration, uint8_t *bgra, std::string &error) override
	{
		if (_vcursor >= videoCount()) return Read::End;
		sleepMs(1); // decoding takes time
		if (_vcursor == _s.failAtFrame) { error = "fake decoding failure"; return Read::Failed; }
		uint32_t index = (uint32_t)_vcursor;
		std::memcpy(bgra, &index, 4);
		pts = videoPts(_vcursor);
		duration = HNS / _s.fps;
		++_vcursor;
		return Read::Item;
	}
	bool seekAudio(int64_t hns) override
	{
		if (_s.audioSeekDelayMs) sleepMs((int)_s.audioSeekDelayMs);
		int idx = 0;
		for (int j = 0; j < audioCount(); ++j) if (audioPts(j) <= hns) idx = j;
		_acursor = idx;
		return true;
	}
	Read readAudio(std::vector<int16_t> &pcm, int64_t &start) override
	{
		if (_acursor >= audioCount()) return Read::End;
		pcm.resize((size_t)kUnit * 2);
		for (int k = 0; k < kUnit; ++k)
		{
			int16_t v = (int16_t)(((int64_t)_acursor * kUnit + k) & 0x7fff);
			pcm[(size_t)k * 2] = v;
			pcm[(size_t)k * 2 + 1] = (int16_t)(v ^ 0x1234);
		}
		start = audioPts(_acursor);
		++_acursor;
		return Read::Item;
	}
private:
	Spec _s;
	int _vcursor = 0, _acursor = 0;
};

// An audio output that plays in real time, like an OpenAL source: 8 buffers, a position
// that is the master clock, an underrun stops the position.
static std::mutex g_audioMutex;
static std::vector<int> g_firstSamples;   // the left sample of the first frame after each flush
static std::atomic<int> g_breaks(0);      // gaps or repeats in what was submitted

class FakeAudio : public IPipelineAudio
{
public:
	void close() override {}
	void flush(int64_t base) override
	{
		std::lock_guard<std::mutex> l(_m);
		update();
		_chunks.clear(); _played = 0; _consumed = 0; _carry = 0; _base = base; _first = true; _expected = -1;
	}
	bool hasFreeBuffer() override { std::lock_guard<std::mutex> l(_m); update(); return _chunks.size() < 8; }
	bool submit(const int16_t *s, size_t frames) override
	{
		std::lock_guard<std::mutex> l(_m);
		update();
		if (_chunks.size() >= 8) return false;
		for (size_t i = 0; i < frames; ++i)
		{
			int left = s[i * 2] & 0x7fff;
			if (_first) { std::lock_guard<std::mutex> g(g_audioMutex); g_firstSamples.push_back(left); _first = false; }
			else if (left != _expected) ++g_breaks;
			_expected = (left + 1) & 0x7fff;
		}
		_chunks.push_back((int64_t)frames);
		return true;
	}
	void play() override { std::lock_guard<std::mutex> l(_m); update(); _playing = true; }
	void pause() override { std::lock_guard<std::mutex> l(_m); update(); _playing = false; }
	void setVolume(float) override {}
	int64_t positionHns() override { std::lock_guard<std::mutex> l(_m); update(); return _base + _played * HNS / kRate; }
	bool isDrained() override { std::lock_guard<std::mutex> l(_m); update(); return _chunks.empty(); }
private:
	void update()
	{
		auto now = std::chrono::steady_clock::now();
		if (_playing && !_chunks.empty())
		{
			double frames = std::chrono::duration<double>(now - _last).count() * kRate + _carry;
			int64_t whole = (int64_t)frames;
			while (whole > 0 && !_chunks.empty())
			{
				int64_t take = std::min(whole, _chunks.front() - _consumed);
				_consumed += take; _played += take; whole -= take;
				if (_consumed >= _chunks.front()) { _chunks.pop_front(); _consumed = 0; }
			}
			_carry = whole > 0 ? 0 : frames - (double)(int64_t)frames;   // an underrun does not bank time
		}
		_last = now;
	}
	std::mutex _m;
	std::deque<int64_t> _chunks;
	int64_t _consumed = 0, _played = 0, _base = 0;
	double _carry = 0;
	bool _playing = false, _first = true;
	int _expected = -1;
	std::chrono::steady_clock::time_point _last = std::chrono::steady_clock::now();
};

static Spec g_nextSpec;

class TestBackend : public PipelineBackend
{
public:
	bool open(const VideoSource &, int64_t, int64_t, VideoInfo &info, std::string &error) override
	{
		Spec s = g_nextSpec;
		Config config;
		config.width = s.width; config.height = s.height;
		config.durationHns = s.durationHns; config.frameDurationHns = HNS / s.fps;
		config.hasAudio = s.audio; config.audioChannels = 2; config.audioRate = kRate;
		return begin(std::unique_ptr<IPipelineSource>(new FakeSource(s)), s.audio ? std::unique_ptr<IPipelineAudio>(new FakeAudio()) : nullptr, config, info, error);
	}
};

// ------------------------------------------------------------------ shared helpers
static int checks = 0, failures = 0;
static void check(bool ok, const std::string &what)
{
	++checks;
	if (!ok) { ++failures; std::printf("  FAIL: %s\n", what.c_str()); }
}
static void section(const char *name) { std::printf("%s\n", name); }
static std::string num(int64_t v) { return std::to_string(v); }

static bool waitFor(std::function<bool()> c, int timeoutMs)
{
	auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while (std::chrono::steady_clock::now() < end) { if (c()) return true; sleepMs(5); }
	return c();
}

static int64_t framePts(int i, int fps = 30) { return (int64_t)i * HNS / fps; }
static int frameIndexAt(int64_t timeHns, int fps = 30, int64_t start = 0)
{
	int best = 0; int count = (int)((3 * HNS * fps + HNS - 1) / HNS);
	for (int i = 0; i < count; ++i) if (start + framePts(i, fps) <= timeHns) best = i;
	return best;
}
