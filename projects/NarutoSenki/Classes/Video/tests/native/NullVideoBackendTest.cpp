// Runs NullVideoBackend (the test-pattern backend) in real time with GL stubbed out: play, finish, pause, seek, loop, context loss.
// Part of the CCVideoSprite tests: see README.md in this directory.
#include <cstdio>
#include <thread>
#include <chrono>
#include <GL/glew.h>
#include "Defines.h"
#include "Video/NullVideoBackend.h"

// stand-ins for the GL entry points the backend uses
static unsigned g_uploads = 0, g_next = 7;
extern "C" {
void glGenTextures(GLsizei n, GLuint *t) { for (int i = 0; i < n; ++i) t[i] = g_next++; }
void glTexParameteri(GLenum, GLenum, GLint) {}
void glPixelStorei(GLenum, GLint) {}
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *) { ++g_uploads; }
void glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *) { ++g_uploads; }
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

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static int drain(NullVideoBackend &b, VideoEventType type, uint32_t *id = nullptr)
{
	int n = 0; VideoEvent e;
	while (b.pollEvent(e)) if (e.type == type) { ++n; if (id) *id = e.seekId; }
	return n;
}

int main()
{
	NullVideoBackend b(64, 48, 300);
	VideoInfo info; std::string err;
	CHECK(b.open(VideoSource::fromFile("x"), 0, 0, info, err));
	CHECK(info.width == 64 && info.height == 48 && info.durationMs == 300);
	CHECK(b.createGLResources() && b.getTextureId() != 0);
	CHECK(b.getPositionMs() == 0);

	// plays and finishes once
	b.start();
	sleepMs(120);
	b.uploadFrame();
	CHECK(b.getPositionMs() >= 100 && b.getPositionMs() < 300);
	CHECK(g_uploads > 1);
	sleepMs(250);
	b.uploadFrame();
	CHECK(b.getPositionMs() == 300);
	CHECK(drain(b, VideoEventType::Finished) == 1);
	b.uploadFrame();
	CHECK(drain(b, VideoEventType::Finished) == 0);

	// pause freezes, resume continues
	b.start();                       // restarts from 0 after the end
	CHECK(b.getPositionMs() < 50);
	sleepMs(60); b.pause();
	int64_t p = b.getPositionMs(); sleepMs(60);
	CHECK(b.getPositionMs() == p && p >= 40);
	b.resume(); sleepMs(40);
	CHECK(b.getPositionMs() > p);

	// seek: SeekDone only with the newest id, Finished is discarded, state kept (running)
	b.seek(100, 1); b.seek(200, 2);
	CHECK(b.getPositionMs() >= 200);
	b.uploadFrame();
	uint32_t id = 0;
	CHECK(drain(b, VideoEventType::SeekDone, &id) == 1 && id == 2);
	sleepMs(150); b.uploadFrame();   // runs into the end
	b.seek(10, 3);                   // seek right after the end: stale Finished must go
	b.uploadFrame();
	CHECK(drain(b, VideoEventType::Finished) == 0);
	CHECK(b.getPositionMs() >= 10 && b.getPositionMs() < 100);

	// seek while paused stays paused
	b.pause(); b.seek(150, 4); b.uploadFrame();
	CHECK(b.getPositionMs() == 150);
	sleepMs(50); CHECK(b.getPositionMs() == 150);
	CHECK(drain(b, VideoEventType::SeekDone, &id) == 1 && id == 4);

	// loop never finishes
	b.setLoop(true); b.resume();
	sleepMs(450); b.uploadFrame();
	CHECK(drain(b, VideoEventType::Finished) == 0);
	CHECK(b.getPositionMs() < 300);

	// context loss and restore
	b.onContextLost(); CHECK(b.getTextureId() == 0);
	CHECK(b.onContextRestored() && b.getTextureId() != 0);

	b.close(); b.destroyGLResources();
	std::printf(fails ? "%d FAILED\n" : "all NullVideoBackend checks passed\n", fails);
	return fails != 0;
}
