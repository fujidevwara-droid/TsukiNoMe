package android.media;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * An AudioTrack that plays in real time: once play() is called, queued frames are
 * consumed at the sample rate; with nothing queued it underruns (the position stops).
 * It remembers what was written: the left sample of the first frame after each flush and
 * whether the left channel then counted up without a gap (what the fake codec writes).
 */
public class AudioTrack {
	public static final int MODE_STREAM = 1, STATE_INITIALIZED = 1, PLAYSTATE_STOPPED = 1, PLAYSTATE_PAUSED = 2, PLAYSTATE_PLAYING = 3, WRITE_NON_BLOCKING = 1;

	public static volatile boolean timestampSupported = true;                  // test switch
	public static final List<Integer> firstSamplesAfterFlush = Collections.synchronizedList(new ArrayList<Integer>());
	public static volatile int continuityBreaks;                               // gaps or repeats in the written samples

	private final int rate, bytesPerFrame, capacityFrames;
	private long written, played;                // frames since the last flush
	private int playState = PLAYSTATE_STOPPED;
	private long lastUpdateNs;
	private double carry;
	private int expectedNext = -1;               // the left sample that must follow
	private boolean released;

	public AudioTrack(AudioAttributes a, AudioFormat f, int bufferBytes, int mode, int session) {
		rate = f.rate; bytesPerFrame = 2 * (f.mask == AudioFormat.CHANNEL_OUT_MONO ? 1 : 2); capacityFrames = bufferBytes / bytesPerFrame;
	}
	public static int getMinBufferSize(int rate, int mask, int enc) { return 4096; }
	public int getState() { return STATE_INITIALIZED; }
	public synchronized int getPlayState() { return playState; }

	private void update() {
		long now = System.nanoTime();
		if (playState == PLAYSTATE_PLAYING) {
			double frames = (now - lastUpdateNs) * (double) rate / 1e9 + carry;
			long whole = (long) frames;
			long consume = Math.min(whole, written - played);
			played += consume;
			carry = consume < whole ? 0 : frames - whole;   // an underrun does not bank time
		}
		lastUpdateNs = now;
	}

	public synchronized void play() { update(); playState = PLAYSTATE_PLAYING; }
	public synchronized void pause() { update(); playState = PLAYSTATE_PAUSED; }
	public synchronized void flush() {
		if (playState == PLAYSTATE_PLAYING) return;     // like the real one: ignored while playing
		written = 0; played = 0; carry = 0; expectedNext = -1;
		pendingFirst = true;
	}
	private boolean pendingFirst = true;
	public void setVolume(float v) {}
	public synchronized void release() { released = true; }

	public synchronized int getPlaybackHeadPosition() { update(); return (int) played; }
	public synchronized boolean getTimestamp(AudioTimestamp ts) {
		if (!timestampSupported || playState != PLAYSTATE_PLAYING) return false;
		update(); ts.framePosition = played; ts.nanoTime = lastUpdateNs; return true;
	}

	public synchronized int write(ByteBuffer data, int size, int mode) {
		update();
		long free = capacityFrames - (written - played);
		int frames = (int) Math.min(size / bytesPerFrame, free);
		if (frames <= 0) return 0;
		ByteBuffer le = data.duplicate().order(ByteOrder.LITTLE_ENDIAN);
		int start = data.position();
		for (int i = 0; i < frames; ++i) {
			int left = le.getShort(start + i * bytesPerFrame) & 0x7fff;
			if (pendingFirst) { firstSamplesAfterFlush.add(left); pendingFirst = false; }
			else if (left != expectedNext) continuityBreaks++;
			expectedNext = (left + 1) & 0x7fff;
		}
		data.position(start + frames * bytesPerFrame);
		written += frames;
		return frames * bytesPerFrame;
	}
}
