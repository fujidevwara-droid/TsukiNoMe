package com.proj.narsen.video;

import android.graphics.SurfaceTexture;
import android.media.MediaCodecList;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.view.Surface;

import java.util.ArrayDeque;
import java.util.Iterator;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * The Android half of CCVideoSprite: everything the C++ AndroidVideoBackend calls.
 *
 * One instance per video. Two threads decode (VideoDecoder, AudioRenderer); the GL
 * thread (the one that calls this class) only latches the newest picture into the
 * external texture (uploadFrame) and collects events (pollEvent). Nothing is called back
 * into C++: the C++ side polls.
 *
 *   audio  : MediaExtractor -> MediaCodec -> AudioTrack      (also the master clock)
 *   video  : MediaExtractor -> MediaCodec -> Surface(SurfaceTexture) -> GL texture
 *
 * The bookkeeping mirrors the Windows backend: a seek bumps an epoch, both decoders notice
 * it, seek to the sync frame before the target and decode forward to the exact frame /
 * sample, and the seek is committed (and SeekDone sent) when both are ready and the GL
 * thread has latched the picture at the target.
 *
 * Methods without "synchronized" in their name are for the GL thread only, except those
 * the two decoders use (marked below).
 */
public final class VideoPlayerCore
{
	private static final String TAG = "VideoPlayerCore";

	public static final int EVENT_NONE = 0;
	public static final int EVENT_FINISHED = 1;
	public static final int EVENT_SEEK_DONE = 2;
	public static final int EVENT_ERROR = 3;

	private static final long SEEK_FRAME_FALLBACK_NS = 150000000L; // see uploadFrame()
	private static final long SEEK_GIVE_UP_NS = 750000000L;

	final Object lock = new Object();

	// ---- the source (set in open())
	private VideoSourceSpec source;
	private MediaExtractor videoExtractor;
	private MediaExtractor audioExtractor;
	private MediaFormat videoFormat;
	private MediaFormat audioFormat;
	private int width;
	private int height;
	private long durationUs;
	private boolean hasAudio;

	// ---- GL objects
	private SurfaceTexture surfaceTexture;
	private Surface surface;
	private HandlerThread frameThread;
	private Handler frameHandler;
	private final AtomicInteger pendingFrames = new AtomicInteger();
	private final AtomicInteger totalFramesAvailable = new AtomicInteger(); // since attachGl
	private boolean firstLatchLogged;
	private long latchedTimestampNs = -1;
	private final float[] latchedMatrix = new float[16];

	private VideoDecoder video;
	private volatile AudioRenderer audio;
	private boolean started;           // the decoders have been started once
	private long resumeUs;             // where playback was when the GL context was lost

	// ---- shared with the decoders (guarded by lock; the volatile ones may be read without it)
	private volatile int epoch;
	private volatile long seekTargetUs;
	private volatile boolean closed;
	private int seekId;
	private boolean seekNotify;
	private volatile boolean seekPending;
	private boolean seekAudioDone;
	private boolean seekVideoRendered;
	private long seekFrameTimestampNs = -1;
	private long seekRenderedAtNs;
	private long seekStartedAtNs;
	private boolean seekLatchedSinceRender;
	private boolean videoEos;
	private boolean audioEos;
	private volatile long lastVideoEndUs;
	private boolean errorReported;
	private final ArrayDeque<Event> events = new ArrayDeque<Event>();
	private Event currentEvent;

	// ---- GL thread state (read by the video thread through clockUs(), hence volatile)
	private boolean running;           // the user's wish: play
	private volatile boolean finished;
	private boolean loop;
	private float volume = 1.0f;
	private volatile boolean audioClockDone;
	private volatile long sysBaseUs;
	private volatile long sysStartNanos;
	private volatile boolean sysRunning;

	private static final class Event
	{
		final int type;
		final int seekId;
		final String message;

		Event(int type, int seekId, String message)
		{
			this.type = type;
			this.seekId = seekId;
			this.message = message;
		}
	}

	// ------------------------------------------------------------------
	// Opening
	// ------------------------------------------------------------------

	/**
	 * Reads the container. Call before anything else.
	 *
	 * @param path a file path or "assets/..."; ignored if fd >= 0 or memory != null
	 * @param fd a file descriptor this object takes over, or -1
	 * @param memory the bytes of the video (cut to the range already), or null
	 * @return null on success, else why the video cannot be opened
	 */
	public String open(String path, int fd, long offset, long length, byte[] memory)
	{
		try
		{
			source = VideoSourceSpec.create(path, fd, offset, length, memory);

			videoExtractor = source.newExtractor();
			int videoTrack = findTrack(videoExtractor, "video/");
			if (videoTrack < 0)
				return fail0("the file has no video track");

			videoFormat = videoExtractor.getTrackFormat(videoTrack);
			videoExtractor.selectTrack(videoTrack);

			width = videoFormat.getInteger(MediaFormat.KEY_WIDTH);
			height = videoFormat.getInteger(MediaFormat.KEY_HEIGHT);
			if (videoFormat.containsKey("rotation-degrees"))
			{
				int rotation = videoFormat.getInteger("rotation-degrees");
				if (rotation == 90 || rotation == 270)
				{
					int swap = width;
					width = height;
					height = swap;
				}
			}

			if (videoFormat.containsKey(MediaFormat.KEY_DURATION))
				durationUs = videoFormat.getLong(MediaFormat.KEY_DURATION);

			String mime = videoFormat.getString(MediaFormat.KEY_MIME);
			if (!canDecode(mime, videoFormat.getInteger(MediaFormat.KEY_WIDTH), videoFormat.getInteger(MediaFormat.KEY_HEIGHT)))
				return fail0("this device cannot decode " + mime + " at " + width + "x" + height);

			// the sound is a bonus: whatever is wrong with it, the video plays on
			try
			{
				audioExtractor = source.newExtractor();
				int audioTrack = findTrack(audioExtractor, "audio/");
				if (audioTrack >= 0)
				{
					audioFormat = audioExtractor.getTrackFormat(audioTrack);
					audioExtractor.selectTrack(audioTrack);
					hasAudio = true;
				}
				else
				{
					audioExtractor.release();
					audioExtractor = null;
				}
			}
			catch (Exception e)
			{
				Log.w(TAG, "playing without sound: " + e);
				if (audioExtractor != null)
					audioExtractor.release();
				audioExtractor = null;
				hasAudio = false;
			}

			frameThread = new HandlerThread("VideoFrames");
			frameThread.start();
			frameHandler = new Handler(frameThread.getLooper());
			return null;
		}
		catch (Exception e)
		{
			String message = e.getMessage();
			release();
			return message != null ? message : e.toString();
		}
	}

	private String fail0(String message)
	{
		release();
		return message;
	}

	private static int findTrack(MediaExtractor extractor, String mimePrefix)
	{
		for (int i = 0; i < extractor.getTrackCount(); ++i)
		{
			String mime = extractor.getTrackFormat(i).getString(MediaFormat.KEY_MIME);
			if (mime != null && mime.startsWith(mimePrefix))
				return i;
		}
		return -1;
	}

	private static boolean canDecode(String mime, int w, int h)
	{
		try
		{
			// a plain format: the frame rate of a real one can make findDecoderForFormat throw
			MediaFormat probe = MediaFormat.createVideoFormat(mime, w, h);
			return new MediaCodecList(MediaCodecList.REGULAR_CODECS).findDecoderForFormat(probe) != null;
		}
		catch (Exception e)
		{
			return true; // cannot tell: let the decoder say
		}
	}

	public int getWidth()
	{
		return width;
	}

	public int getHeight()
	{
		return height;
	}

	public long getDurationUs()
	{
		return durationUs;
	}

	public boolean hasAudio()
	{
		return hasAudio;
	}

	// ------------------------------------------------------------------
	// GL objects and starting the decoders
	// ------------------------------------------------------------------

	/**
	 * Gives the video its GL texture (a GL_TEXTURE_EXTERNAL_OES the caller created on the
	 * GL thread) and starts decoding. Also used after the GL context was recreated.
	 */
	public boolean attachGl(int textureId)
	{
		if (closed || videoExtractor == null)
			return false;

		try
		{
			surfaceTexture = new SurfaceTexture(textureId);
			surfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener()
			{
				@Override
				public void onFrameAvailable(SurfaceTexture texture)
				{
					pendingFrames.incrementAndGet();
					totalFramesAvailable.incrementAndGet();
				}
			}, frameHandler);
			surface = new Surface(surfaceTexture);
		}
		catch (Exception e)
		{
			Log.w(TAG, "cannot create the SurfaceTexture: " + e);
			releaseGl();
			return false;
		}

		pendingFrames.set(0);
		totalFramesAvailable.set(0);
		firstLatchLogged = false;
		latchedTimestampNs = -1;

		// Silent seek: on the very first start to 0 (it lines the audio up with the video),
		// after a lost GL context to where playback was. The decoders start by following it.
		seekInternal(started ? resumeUs : 0, 0, false);

		video = new VideoDecoder(this, videoExtractor, videoFormat);
		String error = video.start(surface);
		if (error != null)
		{
			Log.w(TAG, error);
			video = null;
			releaseGl();
			synchronized (lock)
			{
				queueError(error);
			}
			return false;
		}

		if (hasAudio && audio == null)
		{
			audio = new AudioRenderer(this, audioExtractor, audioFormat);
			audio.setVolume(volume);
			if (!audio.start())
			{
				audio = null;
				hasAudio = false;
				synchronized (lock)
				{
					seekAudioDone = true;
					lock.notifyAll();
				}
			}
		}

		started = true;
		return true;
	}

	/** Pictures the SurfaceTexture has been told about since the GL texture was attached. */
	int framesAvailable()
	{
		return totalFramesAvailable.get();
	}

	/** The GL context is gone: stop the video side and remember where it was. */
	public void onContextLost()
	{
		resumeUs = clockUs();
		detachVideo();
	}

	/** Releases the SurfaceTexture and the video decoder (the sound plays on). */
	public void detachGl()
	{
		resumeUs = clockUs();
		detachVideo();
	}

	private void detachVideo()
	{
		if (video != null)
		{
			video.shutdown();
			video = null;
		}
		releaseGl();
	}

	private void releaseGl()
	{
		if (surface != null)
		{
			surface.release();
			surface = null;
		}
		if (surfaceTexture != null)
		{
			surfaceTexture.setOnFrameAvailableListener(null);
			surfaceTexture.release();
			surfaceTexture = null;
		}
	}

	// ------------------------------------------------------------------
	// Control
	// ------------------------------------------------------------------

	public void start()
	{
		if (closed)
			return;

		if (finished)
			seekInternal(0, 0, false);

		running = true;
		applyRunning();
	}

	public void pause()
	{
		running = false;
		applyRunning();
	}

	public void resume()
	{
		running = true;
		applyRunning();
	}

	public void seek(long targetUs, int id)
	{
		if (closed)
			return;

		seekInternal(targetUs, id, true);
	}

	public void setLoop(boolean value)
	{
		loop = value;
	}

	public void setVolume(float value)
	{
		volume = value;
		AudioRenderer renderer = audio;
		if (renderer != null)
			renderer.setVolume(value);
	}

	private void seekInternal(long targetUs, int id, boolean notify)
	{
		targetUs = Math.max(0, targetUs);
		if (durationUs > 0)
			targetUs = Math.min(targetUs, durationUs);

		// The old sound is no use any more. Pausing first makes sure that audio the audio
		// thread still writes from before the seek cannot be heard.
		finished = false;
		audioClockDone = false;
		if (audio != null)
			audio.pauseAndFlush(targetUs);
		sysBaseUs = targetUs;
		sysRunning = false;

		synchronized (lock)
		{
			epoch++;
			seekTargetUs = targetUs;
			seekId = id;
			seekNotify = notify;
			seekPending = true;
			seekVideoRendered = false;
			seekFrameTimestampNs = -1;
			seekLatchedSinceRender = false;
			seekStartedAtNs = System.nanoTime();
			seekAudioDone = audio == null && !hasAudioPending();
			videoEos = false;
			audioEos = false;

			// a Finished event nobody has picked up is out of date
			for (Iterator<Event> it = events.iterator(); it.hasNext();)
			{
				if (it.next().type == EVENT_FINISHED)
					it.remove();
			}

			lock.notifyAll();
		}
	}

	// the audio renderer is created after the first seek (in attachGl): it will do the seek then
	private boolean hasAudioPending()
	{
		return hasAudio && !started;
	}

	// ------------------------------------------------------------------
	// Clocks
	// ------------------------------------------------------------------

	private boolean usingAudioClock()
	{
		return audio != null && !audioClockDone;
	}

	private long systemPositionUs()
	{
		if (!sysRunning)
			return sysBaseUs;

		return sysBaseUs + (System.nanoTime() - sysStartNanos) / 1000L;
	}

	/** The time of what the user sees and hears. Also called by the video thread. */
	long clockUs()
	{
		long position;
		if (seekPending)
			position = seekTargetUs;
		else if (finished)
			position = durationUs;
		else if (usingAudioClock())
			position = audio.getPositionUs();
		else
			position = systemPositionUs();

		if (durationUs > 0)
			position = Math.min(position, durationUs);
		return Math.max(position, 0);
	}

	public long getPositionUs()
	{
		return clockUs();
	}

	/** Time is passing: playing, and no seek in progress. Also called by the video thread. */
	boolean clockRunning()
	{
		if (seekPending || finished || closed)
			return false;

		AudioRenderer renderer = audio;
		return usingAudioClock() && renderer != null ? renderer.isPlaying() : sysRunning;
	}

	// The clocks run while the user wants to play and no seek is in progress.
	private void applyRunning()
	{
		boolean run = running && !finished && !seekPending && !closed;

		if (usingAudioClock())
		{
			if (run)
				audio.play();
			else
				audio.pause();
			return;
		}

		if (run && !sysRunning)
		{
			sysStartNanos = System.nanoTime();
			sysRunning = true;
		}
		else if (!run && sysRunning)
		{
			sysBaseUs = systemPositionUs();
			sysRunning = false;
		}
	}

	// ------------------------------------------------------------------
	// Frames and events (GL thread)
	// ------------------------------------------------------------------

	/**
	 * Latches the newest decoded picture into the texture, commits a finished seek and
	 * notices the end of the video.
	 *
	 * @param matrix receives the SurfaceTexture's 4x4 texture matrix when the result is true
	 * @return true if the picture changed
	 */
	public boolean uploadFrame(float[] matrix)
	{
		SurfaceTexture texture = surfaceTexture;
		if (texture == null || closed)
			return false;

		boolean latched = false;
		int count = pendingFrames.getAndSet(0);
		for (int i = 0; i < count; ++i)
		{
			try
			{
				texture.updateTexImage();
				latchedTimestampNs = texture.getTimestamp();
				latched = true;
				if (!firstLatchLogged)
				{
					firstLatchLogged = true;
					Log.e(TAG, "first picture latched, timestamp " + latchedTimestampNs + " ns"); // diagnostics, see VideoDecoder.DIAG
				}
			}
			catch (RuntimeException e)
			{
				synchronized (lock)
				{
					queueError("cannot update the video texture: " + e);
				}
				return false;
			}
		}

		if (latched)
		{
			texture.getTransformMatrix(latchedMatrix);
			System.arraycopy(latchedMatrix, 0, matrix, 0, 16);
		}

		boolean committed = false;
		synchronized (lock)
		{
			if (latched && seekPending && seekVideoRendered)
				seekLatchedSinceRender = true;

			if (seekPending && seekAudioDone && seekReadyToCommit())
			{
				seekPending = false;
				committed = true;
				if (seekNotify)
					events.add(new Event(EVENT_SEEK_DONE, seekId, null));
				lock.notifyAll(); // lets the video thread go on
			}
		}

		if (committed)
			applyRunning();

		checkEnd();
		return latched;
	}

	// The picture at the target has been rendered by the decoder and latched by us. The
	// decoder names the picture by its time; the SurfaceTexture reports the same time for
	// a frame from MediaCodec. Should a device not do that, fall back to "a frame arrived
	// after the render", and if nothing arrives at all, do not hold the seek up forever.
	private boolean seekReadyToCommit()
	{
		if (!seekVideoRendered)
		{
			// the decoder found no frame (the target is past the end): nothing to wait for
			return seekFrameTimestampNs == -2;
		}

		if (seekFrameTimestampNs >= 0 && latchedTimestampNs == seekFrameTimestampNs)
			return true;

		long since = System.nanoTime() - seekRenderedAtNs;
		if (seekLatchedSinceRender && since > SEEK_FRAME_FALLBACK_NS)
			return true;

		return since > SEEK_GIVE_UP_NS;
	}

	private void checkEnd()
	{
		if (!running || finished || seekPending || closed)
			return;

		boolean videoDone;
		boolean audioEnded;
		synchronized (lock)
		{
			videoDone = videoEos;
			audioEnded = audioEos;
		}

		if (audio != null && !audioClockDone && audioEnded && audio.isDrained())
		{
			// the sound has played out: the system clock carries on from where it stopped
			sysBaseUs = audio.getPositionUs();
			sysRunning = false;
			audioClockDone = true;
			applyRunning();
		}

		boolean audioDone = audio == null || audioClockDone;
		if (!videoDone || !audioDone || clockUs() < lastVideoEndUs)
			return;

		if (loop)
		{
			seekInternal(0, 0, false); // back to the start without telling anybody
			return;
		}

		finished = true;
		running = false;
		applyRunning();
		synchronized (lock)
		{
			events.add(new Event(EVENT_FINISHED, 0, null));
		}
	}

	/** The next event's type (EVENT_*), or EVENT_NONE. Its details follow. */
	public int pollEvent()
	{
		synchronized (lock)
		{
			currentEvent = events.poll();
			return currentEvent != null ? currentEvent.type : EVENT_NONE;
		}
	}

	public int eventSeekId()
	{
		return currentEvent != null ? currentEvent.seekId : 0;
	}

	public String eventMessage()
	{
		return currentEvent != null && currentEvent.message != null ? currentEvent.message : "";
	}

	private void queueError(String message) // lock held
	{
		if (errorReported)
			return;

		errorReported = true;
		events.add(new Event(EVENT_ERROR, 0, message));
	}

	// ------------------------------------------------------------------
	// What the decoders use (any thread)
	// ------------------------------------------------------------------

	int epoch()
	{
		return epoch;
	}

	long seekTargetUs()
	{
		return seekTargetUs;
	}

	boolean isClosed()
	{
		return closed;
	}

	void wake()
	{
		synchronized (lock)
		{
			lock.notifyAll();
		}
	}

	void sleepMs(long milliseconds)
	{
		if (milliseconds <= 0)
			return;

		synchronized (lock)
		{
			if (closed)
				return;

			try
			{
				lock.wait(milliseconds);
			}
			catch (InterruptedException e)
			{
				Thread.currentThread().interrupt();
			}
		}
	}

	/** The video thread is about to render the frame at the target, whose time is tsNs. */
	void videoSeekFrame(int forEpoch, long timestampNs)
	{
		synchronized (lock)
		{
			if (epoch != forEpoch)
				return;

			seekFrameTimestampNs = timestampNs;
			seekRenderedAtNs = System.nanoTime();
			seekVideoRendered = true;
		}
	}

	/** The target is past the last frame and no frame can be shown for it. */
	void videoSeekNoFrame(int forEpoch)
	{
		synchronized (lock)
		{
			if (epoch != forEpoch)
				return;

			seekFrameTimestampNs = -2;
		}
	}

	/** Blocks until the GL thread has committed the seek. @return false if a newer seek or closing took over. */
	boolean awaitSeekCommit(int forEpoch, VideoDecoder decoder)
	{
		synchronized (lock)
		{
			while (!closed && !decoder.isStopping() && epoch == forEpoch && seekPending)
			{
				try
				{
					lock.wait(20);
				}
				catch (InterruptedException e)
				{
					Thread.currentThread().interrupt();
					return false;
				}
			}
			return !closed && !decoder.isStopping() && epoch == forEpoch;
		}
	}

	void videoEnded(int forEpoch, long endUs)
	{
		synchronized (lock)
		{
			if (epoch != forEpoch)
				return;

			videoEos = true;
			lastVideoEndUs = Math.max(lastVideoEndUs, endUs);
		}
	}

	void audioSeekReady(int forEpoch)
	{
		synchronized (lock)
		{
			if (epoch == forEpoch)
			{
				seekAudioDone = true;
				lock.notifyAll();
			}
		}
	}

	void audioEnded(int forEpoch)
	{
		synchronized (lock)
		{
			if (epoch == forEpoch)
				audioEos = true;
		}
	}

	/** The audio is broken: the video goes on on the system clock. */
	void audioFailed(int forEpoch, String reason)
	{
		Log.w(TAG, "continuing without sound: " + reason);
		synchronized (lock)
		{
			if (epoch == forEpoch)
			{
				audioEos = true;
				seekAudioDone = true;
				lock.notifyAll();
			}
		}
	}

	void fail(String message)
	{
		synchronized (lock)
		{
			queueError(message);
		}
	}

	// ------------------------------------------------------------------
	// Closing
	// ------------------------------------------------------------------

	public void close()
	{
		release();
	}

	private void release()
	{
		synchronized (lock)
		{
			closed = true;
			lock.notifyAll();
		}

		if (video != null)
		{
			video.shutdown();
			video = null;
		}
		if (audio != null)
		{
			audio.shutdown();
			audio = null;
		}

		releaseGl();

		if (videoExtractor != null)
		{
			videoExtractor.release();
			videoExtractor = null;
		}
		if (audioExtractor != null)
		{
			audioExtractor.release();
			audioExtractor = null;
		}
		if (frameThread != null)
		{
			frameThread.quitSafely();
			frameThread = null;
		}
		if (source != null)
		{
			source.close();
			source = null;
		}
	}
}
