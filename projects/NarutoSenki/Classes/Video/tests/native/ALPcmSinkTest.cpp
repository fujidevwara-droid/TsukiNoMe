// Runs ALPcmSink (the Windows audio clock) against a simulated OpenAL: position accounting across buffer boundaries, pause, underrun and restart without replays, flush.
// Part of the CCVideoSprite tests: see README.md in this directory.
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>
#include <GL/glew.h>
#include "Defines.h"
#include "Video/Win32/ALPcmSink.h"

// ---- a simulated OpenAL: one source, a queue of buffers, playback driven by advance() ----
struct FakeBuf { ALuint id; size_t frames; bool processed; };
static std::deque<FakeBuf> q;
static std::vector<size_t> bufFrames(64, 0);
static ALint g_state = AL_INITIAL;
static size_t g_offset = 0;      // frames played from the head of the queue (incl. processed ones)
static int g_replays = 0;
static ALuint g_nextBuf = 1;

static void advance(size_t frames)
{
	if (g_state != AL_PLAYING) return;
	g_offset += frames;
	size_t acc = 0;
	bool all = true;
	for (auto &b : q) { acc += b.frames; b.processed = (g_offset >= acc); all = all && b.processed; }
	if (all && !q.empty()) { g_state = AL_STOPPED; g_offset = 0; }  // ran dry; offset 0 like OpenAL Soft
}

extern "C" {
ALCcontext *alcGetCurrentContext(void) { return (ALCcontext *)1; }
ALenum alGetError(void) { return AL_NO_ERROR; }
void alGenSources(ALsizei, ALuint *s) { *s = 1; }
void alGenBuffers(ALsizei n, ALuint *b) { for (int i = 0; i < n; ++i) b[i] = g_nextBuf++; }
void alDeleteSources(ALsizei, const ALuint *) {}
void alDeleteBuffers(ALsizei, const ALuint *) {}
void alSourcef(ALuint, ALenum, ALfloat) {}
void alSourcei(ALuint, ALenum p, ALint v) { if (p == AL_BUFFER && v == 0) { q.clear(); g_offset = 0; g_state = AL_INITIAL; } }
ALboolean alIsExtensionPresent(const ALchar *) { return AL_FALSE; }
void *alGetProcAddress(const ALchar *) { return nullptr; }
void alSourceStop(ALuint) { g_state = AL_STOPPED; }
void alSourcePause(ALuint) { if (g_state == AL_PLAYING) g_state = AL_PAUSED; }
void alSourcePlay(ALuint)
{
	if (g_state == AL_STOPPED) { g_offset = 0; bool replay = false; for (auto &b : q) { replay = replay || b.processed; b.processed = false; } if (replay) ++g_replays; }
	g_state = AL_PLAYING;
}
void alBufferData(ALuint b, ALenum, const ALvoid *, ALsizei size, ALsizei) { bufFrames[b] = (size_t)size / 4; }
void alSourceQueueBuffers(ALuint, ALsizei, const ALuint *b) { q.push_back({ *b, bufFrames[*b], false }); }
void alSourceUnqueueBuffers(ALuint, ALsizei, ALuint *b)
{
	*b = q.front().id; g_offset -= (g_offset >= q.front().frames) ? q.front().frames : g_offset; q.pop_front();
}
void alGetSourcei(ALuint, ALenum p, ALint *v)
{
	if (p == AL_BUFFERS_PROCESSED) { int n = 0; for (auto &b : q) if (b.processed) ++n; *v = n; }
	else if (p == AL_SOURCE_STATE) *v = g_state;
	else if (p == AL_SAMPLE_OFFSET) *v = (ALint)g_offset;
}
}

namespace CocosDenshion { SimpleAudioEngine *SimpleAudioEngine::sharedEngine() { return nullptr; } }
namespace cocos2d {
CCPoint::CCPoint() { x = y = 0; }
CCSize::CCSize() { width = height = 0; }
CCPoint::CCPoint(float a, float b) { x = a; y = b; }
CCSize::CCSize(float a, float b) { width = a; height = b; }
CCRect::CCRect(float a, float b, float c, float d) { origin.x = a; origin.y = b; size.width = c; size.height = d; }
}

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool near(int64_t a, int64_t b, int64_t tol = 2000) { return (a > b ? a - b : b - a) <= tol; } // 0.2 ms
static const int16_t kPcm[4800 * 2] = {};   // 100 ms stereo, silence
static const int64_t kHns = 10000000;

int main()
{
	ALPcmSink s;
	CHECK(!s.open(6, 48000));
	CHECK(s.open(2, 48000));
	s.flush(1 * kHns);
	CHECK(s.positionHns() == 1 * kHns);
	CHECK(s.isDrained());

	// three 50 ms chunks, not playing yet: position does not move
	for (int i = 0; i < 3; ++i) CHECK(s.submit(kPcm, 2400));
	advance(1000);
	CHECK(s.positionHns() == 1 * kHns);

	s.play();
	int64_t last = s.positionHns();
	for (int step = 0; step < 6; ++step)
	{
		advance(500);                                   // crosses buffer boundaries
		int64_t p = s.positionHns();
		CHECK(p >= last);                               // never backwards, also across unqueues
		last = p;
	}
	CHECK(near(s.positionHns(), 1 * kHns + 3000 * kHns / 48000));    // 3000 frames played
	CHECK(!s.isDrained());

	// pause freezes it
	s.pause();
	int64_t frozen = s.positionHns();
	advance(2000);
	CHECK(s.positionHns() == frozen);
	s.play();

	// play to the end: an underrun. Position = everything submitted, nothing replayed
	advance(10000);
	CHECK(near(s.positionHns(), 1 * kHns + 7200 * kHns / 48000));
	CHECK(s.isDrained());
	CHECK(g_replays == 0);

	// new data after the underrun restarts playback (and only plays the new data)
	CHECK(s.submit(kPcm, 2400));
	CHECK(g_state == AL_PLAYING);
	advance(1200);
	CHECK(near(s.positionHns(), 1 * kHns + (7200 + 1200) * kHns / 48000));
	CHECK(g_replays == 0);

	// free buffers: 8 in total
	s.flush(5 * kHns);
	CHECK(s.positionHns() == 5 * kHns && s.isDrained());
	int queued = 0;
	while (s.hasFreeBuffer() && s.submit(kPcm, 2400)) ++queued;
	CHECK(queued == 8);
	CHECK(!s.hasFreeBuffer());

	// flush and a new base
	s.flush(9 * kHns);
	CHECK(s.positionHns() == 9 * kHns);
	CHECK(s.hasFreeBuffer());

	s.close();
	std::printf(fails ? "%d FAILED\n" : "all ALPcmSink checks passed\n", fails);
	return fails != 0;
}
