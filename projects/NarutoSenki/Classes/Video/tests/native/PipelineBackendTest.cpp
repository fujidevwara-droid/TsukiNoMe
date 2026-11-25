// Runs the real PipelineBackend (the platform-neutral half of the Windows backend) in real
// time against a fake media source and a fake audio output, with GL stubbed out. What it
// checks is the logic of sync and seeking; that Media Foundation or OpenAL behave like the
// fakes can only be proven on Windows. See Fakes.h for the fakes.
#include "Fakes.h"

// ------------------------------------------------------------------ harness
struct Ev { VideoEventType type; uint32_t seekId; };

struct Player
{
	TestBackend backend;
	VideoInfo info;
	std::string openError;
	std::mutex m;
	std::vector<Ev> events;
	std::vector<std::pair<int64_t, int64_t>> shown;   // {frame pts hns, clock hns right after the upload}
	std::mutex cm;
	std::deque<std::packaged_task<void()>> commands;
	std::atomic<bool> stop;
	std::thread gl;
	bool opened = false;

	explicit Player(const Spec &s, bool autoOpen = true) : stop(false)
	{
		{ std::lock_guard<std::mutex> l(g_glMutex); g_uploaded.clear(); }
		{ std::lock_guard<std::mutex> l(g_audioMutex); g_firstSamples.clear(); }
		g_breaks = 0; g_fullUploads = 0;
		g_nextSpec = s;
		gl = std::thread([this, s]() {
			while (!stop)
			{
				std::packaged_task<void()> task;
				{
					std::lock_guard<std::mutex> l(cm);
					if (!commands.empty()) { task = std::move(commands.front()); commands.pop_front(); }
				}
				if (task.valid()) { task(); continue; }

				if (opened)
				{
					if (backend.uploadFrame())
					{
						std::lock_guard<std::mutex> l(m);
						shown.push_back({ (int64_t)lastUploaded() * HNS / s.fps, backend.getPositionMs() * 10000 });
					}
					VideoEvent e;
					while (backend.pollEvent(e)) { std::lock_guard<std::mutex> l(m); events.push_back({ e.type, e.seekId }); }
				}
				sleepMs(3);
			}
		});
		if (autoOpen)
		{
			on([this]() { openError.clear(); opened = backend.open(VideoSource::fromFile("fake"), 0, 0, info, openError) && backend.createGLResources(); });
			if (!opened && openError.empty()) openError = "createGLResources failed";
		}
	}

	void on(std::function<void()> body)
	{
		std::packaged_task<void()> task(body);
		std::future<void> done = task.get_future();
		{ std::lock_guard<std::mutex> l(cm); commands.push_back(std::move(task)); }
		done.wait();
	}
	void start() { on([this]() { backend.start(); }); }
	void pause() { on([this]() { backend.pause(); }); }
	void resume() { on([this]() { backend.resume(); }); }
	void seek(int64_t ms, uint32_t id) { on([this, ms, id]() { backend.seek(ms, id); }); }
	void loop(bool v) { on([this, v]() { backend.setLoop(v); }); }
	int64_t positionMs() { int64_t p = 0; on([this, &p]() { p = backend.getPositionMs(); }); return p; }
	int count(VideoEventType t) { std::lock_guard<std::mutex> l(m); int n = 0; for (auto &e : events) if (e.type == t) ++n; return n; }
	Ev last(VideoEventType t) { std::lock_guard<std::mutex> l(m); Ev r{ t, 0 }; for (auto &e : events) if (e.type == t) r = e; return r; }
	void close()
	{
		on([this]() { backend.close(); opened = false; });
		stop = true;
		gl.join();
	}
};

static int lastFirstSample() { std::lock_guard<std::mutex> l(g_audioMutex); return g_firstSamples.empty() ? -1 : g_firstSamples.back(); }
static bool audioStartsAt(int64_t timeHns)
{
	int64_t expected = timeHns * kRate / HNS;
	int diff = (int)(((lastFirstSample() - expected) % 32768 + 32768) % 32768);
	return diff <= 1 || diff >= 32767;
}
static bool shownFrame(int index) { return lastUploaded() == index; }

static std::unique_ptr<Player> playing(const Spec &s, const char *name)
{
	std::unique_ptr<Player> p(new Player(s));
	check(p->openError.empty(), std::string("open ") + name + ": " + p->openError);
	return p;
}

// ------------------------------------------------------------------ tests
static void testOpen()
{
	section("open and errors");
	{
		Player p(Spec{});
		check(p.openError.empty(), "opens");
		check(p.info.width == 16 && p.info.height == 8 && p.info.durationMs == 3000 && p.info.hasAudio, "info");
		p.close();
	}
	{
		Spec s; s.width = 0;
		Player p(s);
		check(!p.openError.empty(), "a file without a picture is reported: " + p.openError);
		p.close();
	}
	{
		Spec s; s.width = 5000;
		Player p(s);
		check(p.openError.find("texture") != std::string::npos, "a picture bigger than the GL textures is reported: " + p.openError);
		p.close();
	}
}

static void testInitialFrame()
{
	section("the first frame shows without playing");
	auto p = playing(Spec{}, "initial");
	check(waitFor([&]() { return shownFrame(0); }, 1500), "frame 0 is uploaded");
	check(p->positionMs() == 0, "position 0");
	check(p->count(VideoEventType::SeekDone) == 0, "the initial seek is silent");
	sleepMs(300);
	check(p->positionMs() == 0, "nothing moves while not playing");
	p->close();
}

static void syncStats(Player &p, int64_t &minD, int64_t &maxD, size_t &n)
{
	std::lock_guard<std::mutex> l(p.m);
	minD = INT64_MAX; maxD = INT64_MIN; n = p.shown.size();
	for (auto &s : p.shown) { minD = std::min(minD, s.first - s.second); maxD = std::max(maxD, s.first - s.second); }
}

static void testPlaybackAndSync(const char *label, const Spec &spec)
{
	section(label);
	auto p = playing(spec, "sync");
	waitFor([&]() { return shownFrame(0); }, 1500);
	{ std::lock_guard<std::mutex> l(p->m); p->shown.clear(); }
	auto t0 = std::chrono::steady_clock::now();
	p->start();
	sleepMs(1500);
	int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	int64_t position = p->positionMs();
	check(std::llabs(position - elapsedMs) < 120, "the position follows real time: " + num(position) + " vs " + num(elapsedMs));
	int64_t minD, maxD; size_t n; syncStats(*p, minD, maxD, n);
	std::printf("  shown %zu frames, pts - audio clock: %lld .. %lld ms\n", n, (long long)(minD / 10000), (long long)(maxD / 10000));
	check(n >= 35 && n <= 50, "about 1.5 s of frames were shown: " + num((int64_t)n));
	check(maxD <= 20 * 10000, "no frame is shown early");
	check(minD >= -45 * 10000, "no frame is shown more than a frame late");
	check(g_breaks == 0, "the audio has no gaps or repeats");
	p->close();
}

static void testSeekAccuracy(bool whilePlaying)
{
	section(whilePlaying ? "frame- and sample-accurate seeks, while playing" : "frame- and sample-accurate seeks, while paused");
	auto p = playing(Spec{}, "seek");
	waitFor([&]() { return shownFrame(0); }, 1500);
	if (whilePlaying) p->start();

	const int64_t targetsUs[] = { 0, 1000000, 1234567, 1033333, 1033334, 1016000, 2999000, 500, 2000000, 1999999 };
	uint32_t id = 10;
	for (int64_t us : targetsUs)
	{
		int64_t target = us * 10;
		++id;
		int before = p->count(VideoEventType::SeekDone);
		p->seek(us / 1000, id);       // the backend takes milliseconds: use the matching hns
		target = (us / 1000) * 10000;
		bool got = waitFor([&]() { return p->count(VideoEventType::SeekDone) > before; }, 2000);
		check(got, "seek to " + num(us / 1000) + " ms completes");
		if (!got) continue;

		check(p->last(VideoEventType::SeekDone).seekId == id, "SeekDone carries the id for " + num(us / 1000));
		int expected = frameIndexAt(target);
		if (!whilePlaying)
		{
			sleepMs(80); // a paused video must stay on that frame
			check(lastUploaded() == expected, "seek to " + num(us / 1000) + " ms shows frame " + num(expected) + " (got " + num(lastUploaded()) + ")");
			check(std::llabs(p->positionMs() - target / 10000) < 2, "position is the target (got " + num(p->positionMs()) + ")");
		}
		else
		{
			check(lastUploaded() >= expected, "seek to " + num(us / 1000) + " ms is not behind frame " + num(expected) + " (got " + num(lastUploaded()) + ")");
		}
		check(audioStartsAt(target), "the sound starts at the target sample for " + num(us / 1000) + " ms (first " + num(lastFirstSample()) + ")");
	}
	check(g_breaks == 0, "no gaps or repeats in the audio: " + num(g_breaks));
	p->close();
}

static void testSuperseded()
{
	section("a newer seek replaces the one in progress");
	auto p = playing(Spec{}, "superseded");
	waitFor([&]() { return shownFrame(0); }, 1500);
	p->on([&]() { p->backend.seek(2000, 1); p->backend.seek(1200, 2); p->backend.seek(700, 3); });
	check(waitFor([&]() { return p->count(VideoEventType::SeekDone) >= 1; }, 2000), "a seek completes");
	sleepMs(400);
	check(p->count(VideoEventType::SeekDone) == 1, "only one SeekDone: " + num(p->count(VideoEventType::SeekDone)));
	check(p->last(VideoEventType::SeekDone).seekId == 3, "and it is for the last seek");
	check(lastUploaded() == frameIndexAt(700 * 10000), "the picture is the last target's");
	check(audioStartsAt(700 * 10000), "so is the sound");
	p->close();
}

static void testSlowSeek(bool audioSlow)
{
	section(audioSlow ? "a seek waits for the slower side (sound is slow)" : "a seek waits for the slower side (picture is slow)");
	Spec s; (audioSlow ? s.audioSeekDelayMs : s.videoSeekDelayMs) = 300;
	auto p = playing(s, "slow");
	waitFor([&]() { return shownFrame(0); }, 2500);
	auto t0 = std::chrono::steady_clock::now();
	p->seek(1234, 9);
	check(waitFor([&]() { return p->count(VideoEventType::SeekDone) == 1; }, 3000), "completes");
	int64_t took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	check(took >= 250, "not before the slow side is ready: " + num(took) + " ms");
	check(lastUploaded() == frameIndexAt(1234 * 10000), "the picture is the target's when SeekDone arrives");
	check(audioStartsAt(1234 * 10000), "the sound is at the target when SeekDone arrives");
	p->close();
}

static void testPausedJustBeforeAFrame()
{
	section("paused 1 ms before the next frame: that frame must not show yet");
	Spec s; s.fps = 100; s.key = 100;
	auto p = playing(s, "boundary");
	waitFor([&]() { return shownFrame(0); }, 1500);
	p->seek(1999, 4);
	check(waitFor([&]() { return p->count(VideoEventType::SeekDone) == 1; }, 2000), "completes");
	sleepMs(150);
	check(lastUploaded() == 199, "frame 199 (at 1990 ms) stays on screen: " + num(lastUploaded()));
	p->resume();
	check(waitFor([&]() { return lastUploaded() >= 200; }, 500), "and frame 200 comes with the clock");
	p->close();
}

static void testPauseResume()
{
	section("pause and resume");
	auto p = playing(Spec{}, "pause");
	waitFor([&]() { return shownFrame(0); }, 1500);
	p->start(); sleepMs(500); p->pause(); sleepMs(100);
	int64_t frozen = p->positionMs();
	size_t uploads = uploadCount();
	sleepMs(500);
	check(std::llabs(p->positionMs() - frozen) < 4, "the position is frozen: " + num(frozen) + " -> " + num(p->positionMs()));
	check(uploadCount() <= uploads + 1, "no new pictures while paused");
	p->resume(); sleepMs(500);
	int64_t moved = p->positionMs() - frozen;
	check(moved > 400 && moved < 650, "it moves on after resume: " + num(moved));
	check(g_breaks == 0, "the audio has no gaps or repeats");
	p->close();
}

static void testFinishAndRestart()
{
	section("the end, and playing again");
	auto p = playing(Spec{}, "finish");
	waitFor([&]() { return shownFrame(0); }, 1500);
	p->seek(2300, 5);
	waitFor([&]() { return p->count(VideoEventType::SeekDone) == 1; }, 2000);
	auto t0 = std::chrono::steady_clock::now();
	p->start();
	check(waitFor([&]() { return p->count(VideoEventType::Finished) == 1; }, 3000), "Finished arrives");
	int64_t took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	check(took > 550 && took < 1100, "after about the remaining 0.7 s: " + num(took) + " ms");
	check(std::llabs(p->positionMs() - 3000) < 2, "the position is the duration: " + num(p->positionMs()));
	sleepMs(300);
	check(p->count(VideoEventType::Finished) == 1, "only once");
	p->start();
	sleepMs(400);
	int64_t position = p->positionMs();
	check(position > 250 && position < 600, "restarted from 0: " + num(position));
	check(p->count(VideoEventType::Finished) == 1, "not finished again yet");
	p->close();
}

static void testLoop()
{
	section("looping");
	Spec s; s.durationHns = 12 * HNS / 10;
	auto p = playing(s, "loop");
	waitFor([&]() { return shownFrame(0); }, 1500);
	p->loop(true); p->start(); sleepMs(2800);
	check(p->count(VideoEventType::Finished) == 0, "never finished");
	check(p->count(VideoEventType::SeekDone) == 0, "the loop seeks silently");
	check(p->positionMs() >= 0 && p->positionMs() < 1200, "still inside the video: " + num(p->positionMs()));
	p->loop(false);
	check(waitFor([&]() { return p->count(VideoEventType::Finished) == 1; }, 2500), "it finishes once looping is off");
	p->close();
}

static void testSeekToEnd()
{
	section("seeking to the end");
	auto p = playing(Spec{}, "end");
	waitFor([&]() { return shownFrame(0); }, 1500);
	p->seek(99000, 7);
	check(waitFor([&]() { return p->count(VideoEventType::SeekDone) == 1; }, 2000), "completes");
	check(std::llabs(p->positionMs() - 3000) < 2, "clamped to the duration: " + num(p->positionMs()));
	check(lastUploaded() == frameIndexAt(3 * HNS), "the last frame is shown: " + num(lastUploaded()));
	p->start();
	check(waitFor([&]() { return p->count(VideoEventType::Finished) == 1; }, 2500), "and playing from there finishes");
	p->close();
}

static void testNoAudio()
{
	section("a video without sound");
	Spec s; s.audio = false; s.durationHns = 2 * HNS;
	auto p = playing(s, "noaudio");
	check(!p->info.hasAudio, "reports no audio");
	waitFor([&]() { return shownFrame(0); }, 1500);
	p->start(); sleepMs(700);
	check(std::llabs(p->positionMs() - 700) < 150, "runs on the system clock: " + num(p->positionMs()));
	p->seek(1500, 3);
	check(waitFor([&]() { return p->count(VideoEventType::SeekDone) == 1; }, 2000), "seeks");
	check(lastUploaded() >= frameIndexAt(1500 * 10000), "to the right frame");
	check(waitFor([&]() { return p->count(VideoEventType::Finished) == 1; }, 2500), "and finishes");
	p->close();
}

static void testShortAudio()
{
	section("sound that ends before the picture");
	Spec s; s.audioDurationHns = 15 * HNS / 10;
	auto p = playing(s, "shortaudio");
	waitFor([&]() { return shownFrame(0); }, 1500);
	auto t0 = std::chrono::steady_clock::now();
	p->start();
	int64_t last = 0; bool monotonic = true;
	for (int i = 0; i < 28; ++i) { sleepMs(100); int64_t pos = p->positionMs(); if (pos + 2 < last) monotonic = false; last = pos; }
	check(monotonic, "the position never goes backwards when the clock changes over");
	check(waitFor([&]() { return p->count(VideoEventType::Finished) == 1; }, 1500), "it finishes");
	int64_t took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	check(took > 2700 && took < 3500, "at the end of the picture, not of the sound: " + num(took) + " ms");
	p->close();
}

static void testContextLoss()
{
	section("GL context lost and restored");
	auto p = playing(Spec{}, "context");
	waitFor([&]() { return shownFrame(0); }, 1500);
	p->start(); sleepMs(800); p->pause(); sleepMs(50);
	int shownFrameIndex = (int)lastUploaded();
	int before = g_fullUploads;
	p->on([&]() { p->backend.onContextLost(); });
	check(p->backend.getTextureId() == 0, "the texture id is dropped");
	sleepMs(100);
	bool restored = false;
	p->on([&]() { restored = p->backend.onContextRestored(); });
	check(restored && p->backend.getTextureId() != 0, "a new texture");
	check(g_fullUploads == before + 1 && lastUploaded() == shownFrameIndex, "with the picture that was on screen again");
	p->resume(); sleepMs(400);
	check(p->positionMs() > 900, "and playing goes on: " + num(p->positionMs()));
	check(p->count(VideoEventType::Error) == 0, "no errors");
	p->close();
}

static void testClose()
{
	section("closing");
	{
		auto p = playing(Spec{}, "close1");
		p->start(); sleepMs(300);
		auto t0 = std::chrono::steady_clock::now();
		p->close();
		check(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() < 1500, "closing while playing returns promptly");
	}
	{
		Spec s; s.audioSeekDelayMs = 200; s.videoSeekDelayMs = 200;
		auto p = playing(s, "close2");
		p->on([&]() { p->backend.seek(1500, 1); p->backend.seek(2500, 2); });
		auto t0 = std::chrono::steady_clock::now();
		p->close();
		check(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() < 1500, "closing with a seek in progress returns promptly");
	}
}

int main()
{
	auto start = std::chrono::steady_clock::now();
	testOpen();
	testInitialFrame();
	testPlaybackAndSync("playback keeps audio and video in sync", Spec{});
	{ Spec s; s.audioStartHns = 400000; testPlaybackAndSync("sound that starts after 0", s); }
	testSeekAccuracy(false);
	testSeekAccuracy(true);
	testSuperseded();
	testSlowSeek(true);
	testSlowSeek(false);
	testPausedJustBeforeAFrame();
	testPauseResume();
	testFinishAndRestart();
	testLoop();
	testSeekToEnd();
	testNoAudio();
	testShortAudio();
	testContextLoss();
	testClose();

	int secs = (int)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
	if (failures == 0) std::printf("all %d checks passed (%d s)\n", checks, secs);
	else std::printf("%d of %d checks FAILED (%d s)\n", failures, checks, secs);
	return failures != 0;
}
