// Tests of VideoPlayback, the state machine behind CCVideoSprite: what play(), pause() and
// seekTo() do in each state, how the lifecycle reasons (app in the background, node out of a
// running scene, window focus, GL context) suspend and resume the backend, how events become
// callbacks, errors, and releasing from inside a callback.
//
// Part 1 uses a scripted mock backend: exact calls, no threads, no waiting.
// Part 2 uses the real PipelineBackend on fake media, in real time.
#include "Fakes.h"
#include "Video/VideoPlayback.h"

// ------------------------------------------------------------------ part 1: a mock backend
struct MockBackend : IVideoBackend
{
	// shared, so a test can still read it after the backend was destroyed by release()
	std::shared_ptr<std::vector<std::string>> calls = std::make_shared<std::vector<std::string>>();
	std::deque<VideoEvent> events;
	VideoInfo info;
	bool openOk = true, glOk = true, restoreOk = true;
	std::string openError;
	int64_t position = 0;
	unsigned texture = 0;
	std::function<void()> onUpload;      // runs inside uploadFrame()
	std::function<void()> onPoll;        // runs before each pollEvent() result is returned

	MockBackend() { info.width = 16; info.height = 8; info.durationMs = 3000; }
	void log(const std::string &s) { calls->push_back(s); }
	int count(const std::string &s) const { int n = 0; for (auto &c : *calls) if (c == s) ++n; return n; }
	std::string joined() const { std::string r; for (auto &c : *calls) r += (r.empty() ? "" : " ") + c; return r; }
	void clear() { calls->clear(); }

	bool open(const VideoSource &, int64_t, int64_t, VideoInfo &i, std::string &error) override { log("open"); error = openError; i = info; return openOk; }
	void close() override { log("close"); }
	bool createGLResources() override { log("createGL"); texture = glOk ? 1 : 0; return glOk; }
	void destroyGLResources() override { log("destroyGL"); texture = 0; }
	void start() override { log("start"); }
	void pause() override { log("pause"); }
	void resume() override { log("resume"); }
	void seek(int64_t ms, uint32_t id) override { log("seek" + std::to_string(ms) + "/" + std::to_string(id)); }
	void setLoop(bool v) override { log(v ? "loop1" : "loop0"); }
	void setVolume(float v) override { log("volume" + std::to_string((int)(v * 100))); }
	int64_t getPositionMs() const override { return position; }
	bool uploadFrame() override { if (onUpload) onUpload(); return false; }
	unsigned int getTextureId() const override { return texture; }
	VideoTextureKind getTextureKind() const override { return VideoTextureKind::Texture2D; }
	const float *getTexMatrix() const override { static float m[16] = {}; return m; }
	bool pollEvent(VideoEvent &e) override { if (events.empty()) return false; e = events.front(); events.pop_front(); if (onPoll) onPoll(); return true; }
	void onContextLost() override { log("lost"); texture = 0; }
	bool onContextRestored() override { log("restored"); texture = restoreOk ? 1 : 0; return restoreOk; }

	void push(VideoEventType t, uint32_t id = 0, const std::string &msg = "") { VideoEvent e; e.type = t; e.seekId = id; e.message = msg; events.push_back(e); }
};

struct Callbacks
{
	int finished = 0, seekDone = 0, errors = 0;
	std::string lastError;
	std::function<void()> onFinished, onSeekDone;
	std::function<void(const std::string &)> onError;
	Callbacks()
	{
		onFinished = [this]() { ++finished; };
		onSeekDone = [this]() { ++seekDone; };
		onError = [this](const std::string &m) { ++errors; lastError = m; };
	}
};

struct Fixture
{
	MockBackend *mock = new MockBackend();
	std::shared_ptr<std::vector<std::string>> log = mock->calls;
	VideoPlayback pb;
	Callbacks cb;
	std::string error;
	bool ok = false;
	Fixture(bool init = true)
	{
		pb.setCallbackSources(&cb.onFinished, &cb.onSeekDone, &cb.onError);
		if (init) { ok = pb.init(std::unique_ptr<IVideoBackend>(mock), VideoSource::fromFile("x"), 0, 0, error); mock->clear(); }
	}
};

static void expect(const std::string &actual, const std::string &expected, const std::string &what)
{
	check(actual == expected, what + (actual == expected ? "" : ": expected [" + expected + "] got [" + actual + "]"));
}

static void testInit()
{
	section("init");
	{
		VideoPlayback pb; std::string error;
		check(!pb.init(nullptr, VideoSource::fromFile("x"), 0, 0, error) && !error.empty(), "no backend is refused");
	}
	{
		MockBackend *m = new MockBackend(); VideoPlayback pb; std::string error;
		check(!pb.init(std::unique_ptr<IVideoBackend>(m), VideoSource::fromFile("x"), -1, 0, error) && error.find("offset") != std::string::npos, "a negative offset is refused");
	}
	{
		MockBackend *m = new MockBackend(); m->openOk = false; m->openError = "cannot read it"; VideoPlayback pb; std::string error;
		check(!pb.init(std::unique_ptr<IVideoBackend>(m), VideoSource::fromFile("x"), 0, 0, error), "open fails");
		expect(error, "cannot read it", "with the backend's reason");
	}
	{
		MockBackend *m = new MockBackend(); m->openOk = false; VideoPlayback pb; std::string error;
		pb.init(std::unique_ptr<IVideoBackend>(m), VideoSource::fromFile("x"), 0, 0, error);
		check(!error.empty(), "a failure without a reason still has one: " + error);
	}
	{
		MockBackend *m = new MockBackend(); m->info.width = 0; VideoPlayback pb; std::string error;
		check(!pb.init(std::unique_ptr<IVideoBackend>(m), VideoSource::fromFile("x"), 0, 0, error) && error.find("no video") != std::string::npos, "no picture is refused");
	}
	{
		MockBackend *m = new MockBackend(); m->glOk = false; VideoPlayback pb; std::string error;
		check(!pb.init(std::unique_ptr<IVideoBackend>(m), VideoSource::fromFile("x"), 0, 0, error) && error.find("texture") != std::string::npos, "no texture is refused");
	}
	Fixture f;
	check(f.ok && f.pb.state() == VideoPlayback::State::Ready && f.pb.getDurationMs() == 3000, "a good one opens Ready");
	check(!f.pb.isPlaying() && !f.pb.isSeeking(), "not playing, not seeking");
}

static void testPlayPauseResume()
{
	section("play, pause, resume");
	Fixture f;
	f.pb.pause(); f.pb.resume();
	expect(f.mock->joined(), "", "pause and resume before playing do nothing");
	f.pb.play();
	expect(f.mock->joined(), "start", "play starts the backend");
	check(f.pb.isPlaying(), "playing");
	f.pb.play();
	expect(f.mock->joined(), "start", "play while playing does nothing");
	f.pb.resume();
	expect(f.mock->joined(), "start", "resume while playing does nothing");
	f.pb.pause();
	expect(f.mock->joined(), "start pause", "pause pauses");
	check(!f.pb.isPlaying() && f.pb.state() == VideoPlayback::State::Paused, "paused");
	f.pb.pause();
	expect(f.mock->joined(), "start pause", "pause again does nothing");
	f.pb.play();
	expect(f.mock->joined(), "start pause resume", "play while paused resumes (no second start)");
	f.pb.pause(); f.pb.resume();
	expect(f.mock->joined(), "start pause resume pause resume", "resume resumes");
}

static void testFinished()
{
	section("the end");
	Fixture f;
	f.mock->push(VideoEventType::Finished);
	f.pb.update();
	check(f.cb.finished == 0 && f.pb.state() == VideoPlayback::State::Ready, "a Finished that arrives before playing is ignored");
	f.pb.play();
	f.mock->clear();
	f.mock->push(VideoEventType::Finished);
	f.pb.update();
	check(f.cb.finished == 1 && f.pb.state() == VideoPlayback::State::Finished && !f.pb.isPlaying(), "playing -> Finished, callback once");
	check(f.pb.getCurrentTimeMs() == 3000, "the time is the duration");
	f.pb.pause(); f.pb.resume();
	expect(f.mock->joined(), "", "pause and resume do nothing when finished");
	f.pb.play();
	expect(f.mock->joined(), "start", "play restarts it (the backend starts from 0)");
	check(f.pb.isPlaying(), "playing again");
	f.pb.pause();
	f.mock->push(VideoEventType::Finished);
	f.pb.update();
	check(f.cb.finished == 2 && f.pb.state() == VideoPlayback::State::Finished, "a video that was paused just as it ended is finished too");
}

static void testSeek()
{
	section("seeking");
	Fixture f;
	f.pb.seekTo(-50); f.pb.seekTo(99999);
	expect(f.mock->joined(), "seek0/1 seek3000/2", "the target is clamped to 0 .. duration");
	check(f.pb.isSeeking() && f.pb.getCurrentTimeMs() == 3000, "while seeking the time is the target");
	f.mock->push(VideoEventType::SeekDone, 1);
	f.pb.update();
	check(f.pb.isSeeking() && f.cb.seekDone == 0, "a SeekDone of an older seek is ignored");
	f.mock->push(VideoEventType::SeekDone, 2);
	f.pb.update();
	check(!f.pb.isSeeking() && f.cb.seekDone == 1, "the one of the latest completes it, callback once");
	f.mock->push(VideoEventType::SeekDone, 2);
	f.pb.update();
	check(f.cb.seekDone == 1, "a repeated one is ignored");
	f.mock->position = 1234;
	check(f.pb.getCurrentTimeMs() == 1234, "afterwards the time is the backend's");
	f.mock->position = 99999;
	check(f.pb.getCurrentTimeMs() == 3000, "clamped to the duration");

	// seeking a finished video leaves it paused at the target
	f.pb.play();
	f.mock->push(VideoEventType::Finished);
	f.pb.update();
	f.mock->clear();
	f.pb.seekTo(500);
	check(f.pb.state() == VideoPlayback::State::Paused && !f.pb.isPlaying(), "a finished video seeks to Paused");
	expect(f.mock->joined(), "seek500/3", "and only the seek is sent");
	f.pb.play();
	expect(f.mock->joined(), "seek500/3 resume", "play then resumes from there (not start from 0)");
	f.pb.seekTo(10);
	f.mock->push(VideoEventType::Finished);
	f.pb.update();
	check(!f.pb.isSeeking(), "finishing ends a pending seek");
}

static void testSuspend()
{
	section("suspend reasons");
	Fixture f;
	// playing
	f.pb.play(); f.mock->clear();
	f.pb.suspend(VideoPlayback::kSuspendApp);
	expect(f.mock->joined(), "pause", "the app goes to the background: the backend is paused");
	check(f.pb.isPlaying(), "but the video is still 'playing'");
	f.pb.suspend(VideoPlayback::kSuspendApp);
	expect(f.mock->joined(), "pause", "the same reason twice changes nothing");
	f.pb.suspend(VideoPlayback::kSuspendExit);
	expect(f.mock->joined(), "pause", "a second reason changes nothing");
	f.pb.unsuspend(VideoPlayback::kSuspendApp);
	expect(f.mock->joined(), "pause", "one reason less: still held");
	f.pb.unsuspend(VideoPlayback::kSuspendExit);
	expect(f.mock->joined(), "pause resume", "the last one ends: it goes on");

	// the user pauses in the middle of a suspension
	f.mock->clear();
	f.pb.suspend(VideoPlayback::kSuspendFocus);
	f.pb.pause();
	f.pb.unsuspend(VideoPlayback::kSuspendFocus);
	expect(f.mock->joined(), "pause", "paused by the user during a suspension: stays paused afterwards");
	f.mock->clear();
	f.pb.suspend(VideoPlayback::kSuspendFocus);
	f.pb.resume();
	expect(f.mock->joined(), "", "resumed by the user during a suspension: waits");
	f.pb.unsuspend(VideoPlayback::kSuspendFocus);
	expect(f.mock->joined(), "resume", "and goes on when it ends");

	// suspended before starting: play() starts, then holds
	Fixture g;
	g.pb.suspend(VideoPlayback::kSuspendExit);
	g.pb.play();
	expect(g.mock->joined(), "start pause", "play() while suspended starts and holds at once");
	check(g.pb.isPlaying(), "and counts as playing");
	g.pb.unsuspend(VideoPlayback::kSuspendExit);
	expect(g.mock->joined(), "start pause resume", "it runs when the suspension ends");

	// suspending something that is not playing changes nothing
	Fixture h;
	h.pb.suspend(VideoPlayback::kSuspendApp); h.pb.unsuspend(VideoPlayback::kSuspendApp);
	expect(h.mock->joined(), "", "a video that was not started is not touched");
	h.pb.play(); h.pb.pause(); h.mock->clear();
	h.pb.suspend(VideoPlayback::kSuspendApp); h.pb.unsuspend(VideoPlayback::kSuspendApp);
	expect(h.mock->joined(), "", "nor is a paused one");

	// seeking while suspended
	Fixture s;
	s.pb.play(); s.pb.suspend(VideoPlayback::kSuspendApp); s.mock->clear();
	s.pb.seekTo(700);
	expect(s.mock->joined(), "seek700/1", "a seek while suspended is passed on (the backend keeps its state)");
	s.mock->push(VideoEventType::SeekDone, 1);
	s.pb.update();
	check(s.cb.seekDone == 1, "and completes");
}

static void testContext()
{
	section("GL context");
	Fixture f;
	f.pb.play(); f.mock->clear();
	f.pb.contextLost();
	expect(f.mock->joined(), "pause lost", "lost: held, then told");
	f.pb.contextRestored();
	expect(f.mock->joined(), "pause lost restored resume", "restored: told, then it goes on");
	check(f.pb.state() == VideoPlayback::State::Playing, "still playing");

	Fixture g;
	g.pb.play(); g.pb.pause(); g.mock->clear();
	g.pb.contextLost(); g.pb.contextRestored();
	expect(g.mock->joined(), "lost restored", "a paused video stays paused through it");

	Fixture h;
	h.pb.play(); h.pb.suspend(VideoPlayback::kSuspendApp); h.mock->clear();
	h.pb.contextLost(); h.pb.contextRestored();
	expect(h.mock->joined(), "lost restored", "a video held by the app lifecycle stays held");
	h.pb.unsuspend(VideoPlayback::kSuspendApp);
	expect(h.mock->joined(), "lost restored resume", "until that ends");

	Fixture r;
	r.pb.play(); r.mock->restoreOk = false; r.mock->clear();
	r.pb.contextLost(); r.pb.contextRestored();
	check(r.pb.state() == VideoPlayback::State::Error && r.cb.errors == 1, "a restore that fails is an error");
	check(r.mock->count("close") == 1, "and the backend is closed");
}

static void testErrors()
{
	section("errors");
	Fixture f;
	f.pb.play(); f.mock->clear();
	f.mock->push(VideoEventType::Error, 0, "the decoder broke");
	f.pb.update();
	check(f.pb.state() == VideoPlayback::State::Error && f.cb.errors == 1 && f.cb.lastError == "the decoder broke", "Error state and callback with the message");
	check(f.mock->count("close") == 1, "the backend is closed");
	f.mock->clear();
	f.pb.play(); f.pb.pause(); f.pb.resume(); f.pb.seekTo(5); f.pb.setLoop(true);
	check(f.mock->count("start") + f.mock->count("pause") + f.mock->count("resume") + f.mock->count("seek5/2") == 0, "nothing reaches the backend afterwards");
	f.pb.fail("again");
	check(f.cb.errors == 1, "a second error is not reported");
	int polls = 0; f.mock->onUpload = [&]() { ++polls; };
	f.pb.update();
	check(polls == 0, "update does nothing");

	Fixture g;
	g.pb.fail("shader");
	check(g.pb.state() == VideoPlayback::State::Error && g.cb.lastError == "shader", "fail() from outside works");
}

static void testCallbacksAndRelease()
{
	section("callbacks and release");
	// a callback that releases the video (removeFromParent)
	Fixture f;
	f.pb.play();
	f.cb.onFinished = [&]() { ++f.cb.finished; f.pb.release(); };
	f.mock->push(VideoEventType::Finished);
	f.mock->push(VideoEventType::Finished);
	f.pb.update();
	check(f.cb.finished == 1 && f.pb.released(), "released inside a callback: the loop stops cleanly");
	f.pb.update(); f.pb.play(); f.pb.seekTo(1); f.pb.suspend(1); f.pb.contextLost(); f.pb.contextRestored();
	check(f.pb.getCurrentTimeMs() == 0, "everything is a no-op afterwards");

	// a callback that replaces its own handler
	Fixture g;
	int first = 0, second = 0;
	g.cb.onSeekDone = [&]() { ++first; g.cb.onSeekDone = [&]() { ++second; }; };
	g.pb.seekTo(1); g.mock->push(VideoEventType::SeekDone, 1);
	g.pb.update();
	g.pb.seekTo(2); g.mock->push(VideoEventType::SeekDone, 2);
	g.pb.update();
	check(first == 1 && second == 1, "a callback may replace itself");

	// release frees the backend once, and the destructor does not do it again
	{
		Fixture h;
		h.pb.release();
		auto count = [&](const char *name) { return (int)std::count(h.log->begin(), h.log->end(), std::string(name)); };
		check(count("close") == 1 && count("destroyGL") == 1, "release closes and destroys");
		h.pb.release();
		check(count("close") == 1, "twice does no harm");
	}

	// volume and loop
	Fixture v;
	v.pb.setVolume(2.5f); v.pb.setVolume(-1); v.pb.setLoop(true);
	expect(v.mock->joined(), "volume100 volume0 loop1", "volume is clamped and loop is passed on");
	check(v.pb.isLoop() && v.pb.getVolume() == 0.0f, "and remembered");
}

// ------------------------------------------------------------------ part 2: the real pipeline
struct Run
{
	VideoPlayback pb;
	Callbacks cb;
	std::string error;
	std::mutex cm;
	std::deque<std::packaged_task<void()>> commands;
	std::atomic<bool> stop;
	std::thread gl;
	bool opened = false;

	explicit Run(const Spec &s) : stop(false)
	{
		{ std::lock_guard<std::mutex> l(g_glMutex); g_uploaded.clear(); }
		{ std::lock_guard<std::mutex> l(g_audioMutex); g_firstSamples.clear(); }
		g_breaks = 0; g_fullUploads = 0;
		g_nextSpec = s;
		pb.setCallbackSources(&cb.onFinished, &cb.onSeekDone, &cb.onError);
		gl = std::thread([this]() {
			while (!stop)
			{
				std::packaged_task<void()> task;
				{ std::lock_guard<std::mutex> l(cm); if (!commands.empty()) { task = std::move(commands.front()); commands.pop_front(); } }
				if (task.valid()) { task(); continue; }
				if (opened) pb.update();
				sleepMs(3);
			}
		});
		on([this]() { opened = pb.init(std::unique_ptr<IVideoBackend>(new TestBackend()), VideoSource::fromFile("fake"), 0, 0, error); });
	}
	void on(std::function<void()> body)
	{
		std::packaged_task<void()> task(body);
		std::future<void> done = task.get_future();
		{ std::lock_guard<std::mutex> l(cm); commands.push_back(std::move(task)); }
		done.wait();
	}
	int64_t timeMs() { int64_t t = 0; on([&]() { t = pb.getCurrentTimeMs(); }); return t; }
	void close() { on([this]() { pb.release(); opened = false; }); stop = true; gl.join(); }
};

static void testLifecycleEndToEnd()
{
	section("the real pipeline: app pause and resume");
	Run r(Spec{});
	check(r.opened, "opens: " + r.error);
	waitFor([&]() { return lastUploaded() == 0; }, 1500);
	r.on([&]() { r.pb.play(); });
	sleepMs(500);
	r.on([&]() { r.pb.suspend(VideoPlayback::kSuspendApp); });
	sleepMs(100);
	int64_t frozen = r.timeMs(); size_t uploads = uploadCount();
	sleepMs(600);
	check(std::llabs(r.timeMs() - frozen) < 4, "in the background the time stands still: " + num(frozen) + " -> " + num(r.timeMs()));
	check(uploadCount() <= uploads + 1, "and no pictures are shown");
	check(r.pb.isPlaying(), "but it is still 'playing'");
	r.on([&]() { r.pb.unsuspend(VideoPlayback::kSuspendApp); });
	sleepMs(500);
	int64_t moved = r.timeMs() - frozen;
	check(moved > 400 && moved < 650, "back in the foreground it goes on from where it stopped: " + num(moved));
	check(g_breaks == 0, "the sound has no gaps or repeats");
	check(r.cb.finished == 0 && r.cb.errors == 0, "no end, no errors");
	r.close();

	section("the real pipeline: scene exit, focus loss and the app together");
	Run s(Spec{});
	waitFor([&]() { return lastUploaded() == 0; }, 1500);
	s.on([&]() { s.pb.suspend(VideoPlayback::kSuspendExit); s.pb.play(); });
	sleepMs(400);
	check(s.timeMs() < 5, "started while out of a running scene: it waits: " + num(s.timeMs()));
	s.on([&]() { s.pb.suspend(VideoPlayback::kSuspendFocus); s.pb.unsuspend(VideoPlayback::kSuspendExit); });
	sleepMs(300);
	check(s.timeMs() < 5, "entering the scene is not enough while the window has no focus: " + num(s.timeMs()));
	s.on([&]() { s.pb.unsuspend(VideoPlayback::kSuspendFocus); });
	sleepMs(500);
	check(s.timeMs() > 400 && s.timeMs() < 650, "with the focus back it plays: " + num(s.timeMs()));
	s.close();

	section("the real pipeline: a seek in progress when the app goes to the background");
	Spec slow; slow.audioSeekDelayMs = 250;
	Run q(slow);
	waitFor([&]() { return lastUploaded() == 0; }, 2500);
	q.on([&]() { q.pb.play(); });
	sleepMs(200);
	q.on([&]() { q.pb.seekTo(1500); q.pb.suspend(VideoPlayback::kSuspendApp); });
	check(waitFor([&]() { return q.cb.seekDone == 1; }, 3000), "the seek still completes in the background");
	sleepMs(150);
	check(std::llabs(q.timeMs() - 1500) < 4, "and the time stays at the target: " + num(q.timeMs()));
	check(lastUploaded() == frameIndexAt(1500 * 10000), "on the right picture");
	q.on([&]() { q.pb.unsuspend(VideoPlayback::kSuspendApp); });
	sleepMs(400);
	check(q.timeMs() > 1800, "it plays on after the foreground: " + num(q.timeMs()));
	q.close();

	section("the real pipeline: GL context lost and restored while playing");
	Run c(Spec{});
	waitFor([&]() { return lastUploaded() == 0; }, 1500);
	c.on([&]() { c.pb.play(); });
	sleepMs(700);
	int before = g_fullUploads;
	int64_t where = 0;
	c.on([&]() { c.pb.contextLost(); where = c.pb.getCurrentTimeMs(); });
	sleepMs(300);
	check(std::llabs(c.timeMs() - where) < 4, "the time stands still while the context is gone");
	int shownBefore = (int)lastUploaded();
	c.on([&]() { c.pb.contextRestored(); });
	check(g_fullUploads == before + 1, "a new texture with the picture that was on screen");
	check(lastUploaded() == shownBefore || lastUploaded() == shownBefore + 1, "which is the same picture");
	sleepMs(500);
	check(c.timeMs() > where + 400 && c.timeMs() < where + 700, "and it plays on: " + num(c.timeMs() - where));
	check(c.cb.errors == 0, "no errors");
	c.close();

	section("the real pipeline: a decoder that fails");
	Spec bad; bad.failAtFrame = 20;
	Run e(bad);
	waitFor([&]() { return lastUploaded() == 0; }, 1500);
	e.on([&]() { e.pb.play(); });
	check(waitFor([&]() { return e.cb.errors == 1; }, 3000), "the error arrives once");
	check(e.cb.lastError.find("fake decoding failure") != std::string::npos, "with the decoder's message: " + e.cb.lastError);
	check(e.pb.state() == VideoPlayback::State::Error, "state Error");
	size_t uploads2 = uploadCount();
	sleepMs(300);
	check(uploadCount() == uploads2, "nothing more is shown");
	e.close();
}

int main()
{
	auto start = std::chrono::steady_clock::now();
	testInit();
	testPlayPauseResume();
	testFinished();
	testSeek();
	testSuspend();
	testContext();
	testErrors();
	testCallbacksAndRelease();
	testLifecycleEndToEnd();

	int secs = (int)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
	if (failures == 0) std::printf("all %d checks passed (%d s)\n", checks, secs);
	else std::printf("%d of %d checks FAILED (%d s)\n", failures, checks, secs);
	return failures != 0;
}
