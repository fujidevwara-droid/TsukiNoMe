import android.graphics.SurfaceTexture;
import android.media.AudioTrack;
import android.media.MediaCodecList;
import android.view.Surface;
import com.proj.narsen.video.VideoPlayerCore;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.FutureTask;

/**
 * Runs the real VideoPlayerCore / VideoDecoder / AudioRenderer on a desktop JVM against
 * fake MediaExtractor / MediaCodec / AudioTrack / SurfaceTexture classes (see stubs/), in
 * real time. What it checks is the logic of sync and seeking, not that Android's codecs
 * behave: the fakes are written to behave like the real ones, which only a device can prove.
 *
 * The fake video is 30 fps with a sync frame every second; its frame i is at i*1e6/30 us.
 */
public class VideoCoreTest
{
	static int checks, failures;

	static void check(boolean ok, String what)
	{
		++checks;
		if (!ok)
		{
			++failures;
			System.out.println("  FAIL: " + what);
		}
	}

	static void section(String name)
	{
		System.out.println(name);
	}

	// ---------------------------------------------------------------- harness

	static final class Event
	{
		final int type, seekId;
		Event(int type, int seekId) { this.type = type; this.seekId = seekId; }
	}

	/** A player with its own "GL thread": every call into the core is made from that thread, like in the game. */
	static final class Player
	{
		final VideoPlayerCore core = new VideoPlayerCore();
		final List<Event> events = Collections.synchronizedList(new ArrayList<Event>());
		final List<long[]> renders = Collections.synchronizedList(new ArrayList<long[]>()); // {pts us, clock us}
		final ConcurrentLinkedQueue<FutureTask<Object>> commands = new ConcurrentLinkedQueue<FutureTask<Object>>();
		volatile boolean stop;
		Thread gl;
		String openError;

		Player(String spec)
		{
			SurfaceTexture.latched.clear();
			AudioTrack.firstSamplesAfterFlush.clear();
			AudioTrack.continuityBreaks = 0;
			Surface.renderListener = new Surface.RenderListener()
			{
				public void rendered(long ts) { renders.add(new long[] { ts / 1000, core.getPositionUs() }); }
			};

			gl = new Thread(new Runnable()
			{
				public void run()
				{
					float[] matrix = new float[16];
					while (!stop)
					{
						FutureTask<Object> task;
						while ((task = commands.poll()) != null)
							task.run();

						core.uploadFrame(matrix);
						int type;
						while ((type = core.pollEvent()) != 0)
							events.add(new Event(type, core.eventSeekId()));
						sleep(3);
					}
				}
			}, "fake-gl");
			gl.start();
			openError = on(new Callable<Object>() { public Object call() { return core.open(spec(spec), -1, 0, 0, null); } }) == null ? null : "x";
			String error = (String) lastResult;
			openError = error;
		}

		static String spec(String s) { return "fake:" + s; }

		Object lastResult;

		Object on(final Callable<Object> body)
		{
			FutureTask<Object> task = new FutureTask<Object>(body);
			commands.add(task);
			try { lastResult = task.get(); } catch (Exception e) { throw new RuntimeException(e); }
			return lastResult;
		}

		void run(final Runnable body)
		{
			on(new Callable<Object>() { public Object call() { body.run(); return null; } });
		}

		boolean attach() { return (Boolean) on(new Callable<Object>() { public Object call() { return core.attachGl(1); } }); }
		void start() { run(new Runnable() { public void run() { core.start(); } }); }
		void pause() { run(new Runnable() { public void run() { core.pause(); } }); }
		void resume() { run(new Runnable() { public void run() { core.resume(); } }); }
		void seek(final long us, final int id) { run(new Runnable() { public void run() { core.seek(us, id); } }); }
		void loop(final boolean v) { run(new Runnable() { public void run() { core.setLoop(v); } }); }
		void contextLost() { run(new Runnable() { public void run() { core.onContextLost(); } }); }
		long position() { return (Long) on(new Callable<Object>() { public Object call() { return core.getPositionUs(); } }); }

		int count(int type) { int n = 0; synchronized (events) { for (Event e : events) if (e.type == type) ++n; } return n; }
		Event last(int type) { synchronized (events) { Event r = null; for (Event e : events) if (e.type == type) r = e; return r; } }

		void close()
		{
			stop = true;
			try { gl.join(2000); } catch (InterruptedException ignored) { }
			core.close();
			Surface.renderListener = null;
		}
	}

	interface Condition { boolean ok(); }

	static boolean waitFor(Condition c, long timeoutMs)
	{
		long end = System.currentTimeMillis() + timeoutMs;
		while (System.currentTimeMillis() < end)
		{
			if (c.ok()) return true;
			sleep(5);
		}
		return c.ok();
	}

	static void sleep(long ms)
	{
		try { Thread.sleep(ms); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
	}

	// ---------------------------------------------------------------- expectations

	static final int FPS = 30;
	static final long FRAME_COUNT = 90;

	static long framePts(long i) { return i * 1000000L / FPS; }

	/** The frame that is on screen at timeUs: the last one that does not start after it. */
	static long frameAt(long timeUs, long startUs)
	{
		long best = 0;
		for (long i = 0; i < FRAME_COUNT; ++i)
			if (startUs + framePts(i) <= timeUs) best = i;
		return startUs + framePts(best);
	}

	static long lastLatchedUs()
	{
		synchronized (SurfaceTexture.latched)
		{
			return SurfaceTexture.latched.isEmpty() ? -1 : SurfaceTexture.latched.get(SurfaceTexture.latched.size() - 1) / 1000;
		}
	}

	static boolean latchedUs(long us)
	{
		return lastLatchedUs() == us;
	}

	static int lastFirstSample()
	{
		synchronized (AudioTrack.firstSamplesAfterFlush)
		{
			return AudioTrack.firstSamplesAfterFlush.isEmpty() ? -1 : AudioTrack.firstSamplesAfterFlush.get(AudioTrack.firstSamplesAfterFlush.size() - 1);
		}
	}

	// the sample the audio must start with after a seek to timeUs (+- 1: the fake pts are rounded)
	static boolean audioStartsAt(long timeUs)
	{
		long expected = timeUs * 48000L / 1000000L;
		int diff = (int) (((lastFirstSample() - expected) % 32768 + 32768) % 32768);
		return diff <= 1 || diff >= 32767;
	}

	static Player playing(String spec)
	{
		Player p = new Player(spec);
		check(p.openError == null, "open " + spec + (p.openError != null ? ": " + p.openError : ""));
		check(p.attach(), "attachGl");
		return p;
	}

	// ---------------------------------------------------------------- tests

	static void testOpen()
	{
		section("open and errors");
		Player p = new Player("duration=3000000");
		check(p.openError == null, "opens");
		check(p.core.getWidth() == 320 && p.core.getHeight() == 180, "size");
		check(p.core.getDurationUs() == 3000000, "duration");
		check(p.core.hasAudio(), "has audio");
		p.close();

		Player none = new Player("novideo=1");
		check(none.openError != null && none.openError.contains("no video"), "no video track is reported: " + none.openError);
		none.close();

		MediaCodecList.supported = false;
		Player unsupported = new Player("duration=3000000");
		check(unsupported.openError != null && unsupported.openError.contains("cannot decode"), "an unsupported codec is reported: " + unsupported.openError);
		unsupported.close();
		MediaCodecList.supported = true;

		Player bad = new Player("bogus=1");
		check(bad.openError != null, "a file that cannot be read is reported");
		bad.close();
	}

	static void testInitialFrame()
	{
		section("the first frame shows without playing");
		Player p = playing("duration=3000000");
		check(waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500), "frame 0 is latched");
		check(p.position() == 0, "position 0");
		check(p.count(VideoPlayerCore.EVENT_SEEK_DONE) == 0, "the initial seek is silent");
		sleep(300);
		check(p.position() == 0, "nothing moves while not playing");
		check(lastLatchedUs() == 0, "no further frame");
		p.close();
	}

	static void testPlaybackAndSync()
	{
		section("playback keeps audio and video in sync");
		Player p = playing("duration=3000000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		p.renders.clear();
		long before = System.nanoTime();
		p.start();
		sleep(1500);
		long elapsedUs = (System.nanoTime() - before) / 1000;
		long position = p.position();
		check(Math.abs(position - elapsedUs) < 120000, "the position follows real time: " + position + " vs " + elapsedUs);

		long maxDelta = Long.MIN_VALUE, minDelta = Long.MAX_VALUE;
		int n;
		synchronized (p.renders)
		{
			n = p.renders.size();
			for (long[] r : p.renders) { maxDelta = Math.max(maxDelta, r[0] - r[1]); minDelta = Math.min(minDelta, r[0] - r[1]); }
		}
		System.out.println("  rendered " + n + " frames, pts - audio clock: " + minDelta + " .. " + maxDelta + " us");
		check(n >= 35 && n <= 50, "about 1.5 s of frames were shown: " + n);
		check(maxDelta <= 20000, "no frame is shown early (max " + maxDelta + " us)");
		check(minDelta >= -45000, "no frame is shown more than a frame late (min " + minDelta + " us)");
		check(AudioTrack.continuityBreaks == 0, "the audio has no gaps or repeats");
		p.close();
	}

	static void testPlaybackWithoutTimestamps()
	{
		section("the same, on a device whose AudioTrack has no timestamps");
		AudioTrack.timestampSupported = false;
		Player p = playing("duration=3000000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		p.renders.clear();
		p.start();
		sleep(1000);
		long maxDelta = Long.MIN_VALUE, minDelta = Long.MAX_VALUE;
		synchronized (p.renders)
		{
			for (long[] r : p.renders) { maxDelta = Math.max(maxDelta, r[0] - r[1]); minDelta = Math.min(minDelta, r[0] - r[1]); }
		}
		check(maxDelta <= 20000 && minDelta >= -45000, "in sync: " + minDelta + " .. " + maxDelta);
		p.close();
		AudioTrack.timestampSupported = true;
	}

	static void testSeekAccuracy(boolean whilePlaying)
	{
		section("frame- and sample-accurate seeks, " + (whilePlaying ? "while playing" : "while paused"));
		Player p = playing("duration=3000000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		if (whilePlaying) p.start();

		long[] targets = { 0, 1000000, 1234567, 1033333, 1033334, 1016000, 2999000, 500, 2000000, 1999999 };
		int id = 10;
		for (final long target : targets)
		{
			final int expectedId = ++id;
			int before = p.count(VideoPlayerCore.EVENT_SEEK_DONE);
			p.seek(target, expectedId);
			final Player q = p;
			final int b = before;
			boolean got = waitFor(new Condition() { public boolean ok() { return q.count(VideoPlayerCore.EVENT_SEEK_DONE) > b; } }, 2000);
			check(got, "seek to " + target + " completes");
			if (!got) continue;

			check(p.last(VideoPlayerCore.EVENT_SEEK_DONE).seekId == expectedId, "SeekDone carries the id for " + target);
			long expectedFrame = frameAt(target, 0);
			if (!whilePlaying)
			{
				sleep(80); // a paused video must stay on that frame, also when the next one is a microsecond away
				check(lastLatchedUs() == expectedFrame, "seek to " + target + " shows frame " + expectedFrame + " (got " + lastLatchedUs() + ")");
				check(Math.abs(p.position() - target) < 1000, "position is the target " + target + " (got " + p.position() + ")");
			}
			else
			{
				// playing goes on right away, so the frame may already have moved on: it must never be before it
				check(lastLatchedUs() >= expectedFrame, "seek to " + target + " is not behind frame " + expectedFrame + " (got " + lastLatchedUs() + ")");
			}
			check(audioStartsAt(target), "the sound starts at the target sample for " + target + " (first sample " + lastFirstSample() + ")");
		}
		check(AudioTrack.continuityBreaks == 0, "no gaps or repeats in the audio written: " + AudioTrack.continuityBreaks);
		p.close();
	}

	static void testSupersededSeek()
	{
		section("a newer seek replaces the one in progress");
		Player p = playing("duration=3000000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		final Player q = p;
		p.run(new Runnable() { public void run() { q.core.seek(2000000, 1); q.core.seek(1200000, 2); q.core.seek(700000, 3); } });
		check(waitFor(new Condition() { public boolean ok() { return q.count(VideoPlayerCore.EVENT_SEEK_DONE) >= 1; } }, 2000), "a seek completes");
		sleep(400);
		check(p.count(VideoPlayerCore.EVENT_SEEK_DONE) == 1, "only one SeekDone: " + p.count(VideoPlayerCore.EVENT_SEEK_DONE));
		check(p.last(VideoPlayerCore.EVENT_SEEK_DONE).seekId == 3, "and it is for the last seek");
		check(lastLatchedUs() == frameAt(700000, 0), "the picture is the last target's: " + lastLatchedUs());
		check(audioStartsAt(700000), "so is the sound");
		p.close();
	}

	static void testSlowSeek(final String which)
	{
		section("a seek is held back until the slower of picture and sound is ready (" + which + " is slow)");
		final Player p = playing("duration=3000000;" + which + "seekdelay=300");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 2500);
		final long target = 1234567;
		long t0 = System.nanoTime();
		p.seek(target, 9);
		check(waitFor(new Condition() { public boolean ok() { return p.count(VideoPlayerCore.EVENT_SEEK_DONE) == 1; } }, 3000), "completes");
		long took = (System.nanoTime() - t0) / 1000000;
		// at the moment SeekDone arrives both must already be at the target
		check(took >= 250, "not before the slow side is ready: " + took + " ms");
		check(lastLatchedUs() == frameAt(target, 0), "the picture is the target's when SeekDone arrives: " + lastLatchedUs());
		check(audioStartsAt(target), "the sound is at the target when SeekDone arrives");
		check(Math.abs(p.position() - target) < 1000, "and so is the position: " + p.position());
		p.close();
	}

	static void testPauseResume()
	{
		section("pause and resume");
		Player p = playing("duration=3000000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		p.start();
		sleep(500);
		p.pause();
		sleep(100);
		long frozen = p.position();
		int frames = SurfaceTexture.latched.size();
		sleep(500);
		check(Math.abs(p.position() - frozen) < 3000, "the position is frozen: " + frozen + " -> " + p.position());
		check(SurfaceTexture.latched.size() <= frames + 1, "no new pictures while paused");
		p.resume();
		sleep(500);
		long moved = p.position();
		check(moved - frozen > 400000 && moved - frozen < 650000, "it moves on after resume: " + (moved - frozen));
		check(AudioTrack.continuityBreaks == 0, "the audio has no gaps or repeats");
		p.close();
	}

	static void testFinishAndRestart()
	{
		section("the end, and playing again");
		final Player p = playing("duration=3000000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		p.seek(2300000, 5);
		waitFor(new Condition() { public boolean ok() { return p.count(VideoPlayerCore.EVENT_SEEK_DONE) == 1; } }, 2000);
		long t0 = System.nanoTime();
		p.start();
		check(waitFor(new Condition() { public boolean ok() { return p.count(VideoPlayerCore.EVENT_FINISHED) == 1; } }, 3000), "Finished arrives");
		long took = (System.nanoTime() - t0) / 1000000;
		check(took > 550 && took < 1100, "after about the remaining 0.7 s: " + took + " ms");
		check(Math.abs(p.position() - 3000000) < 1000, "the position is the duration: " + p.position());
		sleep(300);
		check(p.count(VideoPlayerCore.EVENT_FINISHED) == 1, "only once");

		p.start(); // from the end: plays from 0
		sleep(400);
		long position = p.position();
		check(position > 250000 && position < 600000, "restarted from 0: " + position);
		check(p.count(VideoPlayerCore.EVENT_FINISHED) == 1, "not finished again yet");
		p.close();
	}

	static void testLoop()
	{
		section("looping");
		final Player p = playing("duration=1200000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		p.loop(true);
		p.start();
		sleep(2800);
		check(p.count(VideoPlayerCore.EVENT_FINISHED) == 0, "never finished");
		check(p.count(VideoPlayerCore.EVENT_SEEK_DONE) == 0, "the loop seeks silently");
		long position = p.position();
		check(position >= 0 && position < 1200000, "still inside the video: " + position);
		p.loop(false);
		check(waitFor(new Condition() { public boolean ok() { return p.count(VideoPlayerCore.EVENT_FINISHED) == 1; } }, 2500), "it finishes once looping is off");
		p.close();
	}

	static void testSeekToEnd()
	{
		section("seeking to the end");
		final Player p = playing("duration=3000000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		p.seek(99000000L, 7); // far beyond
		check(waitFor(new Condition() { public boolean ok() { return p.count(VideoPlayerCore.EVENT_SEEK_DONE) == 1; } }, 2000), "completes");
		check(Math.abs(p.position() - 3000000) < 1000, "clamped to the duration: " + p.position());
		check(lastLatchedUs() == frameAt(3000000, 0), "the last frame is shown: " + lastLatchedUs());
		p.start();
		check(waitFor(new Condition() { public boolean ok() { return p.count(VideoPlayerCore.EVENT_FINISHED) == 1; } }, 2500), "and playing from there finishes");
		p.close();
	}

	static void testNoAudio()
	{
		section("a video without sound");
		final Player p = playing("duration=2000000;audio=0");
		check(!p.core.hasAudio(), "reports no audio");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		p.start();
		sleep(700);
		check(Math.abs(p.position() - 700000) < 150000, "runs on the system clock: " + p.position());
		p.seek(1500000, 3);
		check(waitFor(new Condition() { public boolean ok() { return p.count(VideoPlayerCore.EVENT_SEEK_DONE) == 1; } }, 2000), "seeks");
		check(lastLatchedUs() >= frameAt(1500000, 0), "to the right frame");
		check(waitFor(new Condition() { public boolean ok() { return p.count(VideoPlayerCore.EVENT_FINISHED) == 1; } }, 2500), "and finishes");
		p.close();
	}

	static void testShortAudio()
	{
		section("sound that ends before the picture");
		final Player p = playing("duration=3000000;adur=1500000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		long t0 = System.nanoTime();
		p.start();
		long last = 0;
		boolean monotonic = true;
		for (int i = 0; i < 28; ++i)
		{
			sleep(100);
			long position = p.position();
			if (position + 2000 < last) monotonic = false;
			last = position;
		}
		check(monotonic, "the position never goes backwards when the clock changes over");
		check(waitFor(new Condition() { public boolean ok() { return p.count(VideoPlayerCore.EVENT_FINISHED) == 1; } }, 1500), "it finishes");
		long took = (System.nanoTime() - t0) / 1000000;
		check(took > 2700 && took < 3500, "at the end of the picture, not of the sound: " + took + " ms");
		p.close();
	}

	static void testAudioOffset()
	{
		section("sound that starts after 0");
		Player p = playing("duration=3000000;astart=40000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		p.renders.clear();
		p.start();
		sleep(1000);
		long maxDelta = Long.MIN_VALUE, minDelta = Long.MAX_VALUE;
		synchronized (p.renders)
		{
			for (long[] r : p.renders) { maxDelta = Math.max(maxDelta, r[0] - r[1]); minDelta = Math.min(minDelta, r[0] - r[1]); }
		}
		check(maxDelta <= 20000 && minDelta >= -45000, "still in sync: " + minDelta + " .. " + maxDelta);
		p.close();
	}

	static void testContextLoss()
	{
		section("GL context lost and restored");
		final Player p = playing("duration=3000000");
		waitFor(new Condition() { public boolean ok() { return latchedUs(0); } }, 1500);
		p.start();
		sleep(800);
		p.pause();
		sleep(50);
		long where = p.position();
		p.contextLost();
		sleep(200);
		check(p.attach(), "attach again");
		check(waitFor(new Condition() { public boolean ok() { return lastLatchedUs() >= 0 && Math.abs(p.position() - where) < 60000; } }, 2000), "back where it was: " + p.position() + " vs " + where);
		check(lastLatchedUs() == frameAt(where, 0) || lastLatchedUs() == frameAt(p.position(), 0), "with the picture of that position: " + lastLatchedUs());
		p.resume();
		sleep(400);
		check(p.position() > where + 250000, "and plays on: " + p.position());
		check(p.count(VideoPlayerCore.EVENT_ERROR) == 0 && p.count(VideoPlayerCore.EVENT_FINISHED) == 0, "no errors");
		p.close();
	}

	static void testClose()
	{
		section("closing");
		Player p = playing("duration=3000000");
		p.start();
		sleep(300);
		long t0 = System.nanoTime();
		p.close();
		check((System.nanoTime() - t0) / 1000000 < 1500, "closing while playing returns promptly");

		Player q = playing("duration=3000000");
		final Player qq = q;
		q.run(new Runnable() { public void run() { qq.core.seek(1500000, 1); qq.core.seek(2500000, 2); } });
		t0 = System.nanoTime();
		q.close();
		check((System.nanoTime() - t0) / 1000000 < 1500, "closing with a seek in progress returns promptly");
	}

	public static void main(String[] args) throws Exception
	{
		long start = System.currentTimeMillis();
		testOpen();
		testInitialFrame();
		testPlaybackAndSync();
		testPlaybackWithoutTimestamps();
		testSeekAccuracy(false);
		testSeekAccuracy(true);
		testSupersededSeek();
		testSlowSeek("a");
		testSlowSeek("v");
		testPauseResume();
		testFinishAndRestart();
		testLoop();
		testSeekToEnd();
		testNoAudio();
		testShortAudio();
		testAudioOffset();
		testContextLoss();
		testClose();

		System.out.println((failures == 0 ? "all " + checks + " checks passed" : failures + " of " + checks + " checks FAILED")
			+ " (" + (System.currentTimeMillis() - start) / 1000 + " s)");
		System.exit(failures == 0 ? 0 : 1);
	}
}
