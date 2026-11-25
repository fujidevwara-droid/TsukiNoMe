package com.proj.narsen.video;

import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.util.Log;
import android.view.Surface;

import java.nio.ByteBuffer;

/**
 * The picture of a video: MediaExtractor -> MediaCodec -> Surface (the player's
 * SurfaceTexture), on a thread of its own.
 *
 * It paces the frames itself: an output buffer is released to the surface ("rendered")
 * when the player's clock (the audio position) has reached its time, and dropped if it is
 * more than a frame late. The GL thread only has to latch what arrives.
 *
 * Seeking is frame-accurate: seek the extractor to the sync frame before the target,
 * flush, decode forward, and render the last frame whose time is not after the target.
 * That needs one frame of look-ahead (the "candidate"), because only the next frame's time
 * tells that the candidate is the one on screen at the target.
 */
final class VideoDecoder implements Runnable
{
	private static final String TAG = "VideoDecoder";
	private static final long PRESENT_LEAD_US = 5000;

	private final VideoPlayerCore core;
	private final MediaExtractor extractor;
	private final MediaFormat format;
	private final String mime;
	private final long frameDurationUs;

	private MediaCodec codec;
	private Thread thread;
	private volatile boolean stop;

	VideoDecoder(VideoPlayerCore core, MediaExtractor extractor, MediaFormat format)
	{
		this.core = core;
		this.extractor = extractor;
		this.format = format;
		this.mime = format.getString(MediaFormat.KEY_MIME);

		long duration = 33333;
		try
		{
			int rate = format.getInteger(MediaFormat.KEY_FRAME_RATE);
			if (rate > 0)
				duration = 1000000L / rate;
		}
		catch (Exception ignored)
		{
			// absent, or a float on this device: 30 fps is a good enough guess for dropping
		}
		frameDurationUs = duration;
	}

	/** @return null on success, else why the decoder cannot be started. */
	String start(Surface surface)
	{
		try
		{
			codec = MediaCodec.createDecoderByType(mime);
			codec.configure(format, surface, null, 0);
			codec.start();
		}
		catch (Exception e)
		{
			releaseCodec();
			return "cannot start the video decoder for " + mime + ": " + e;
		}

		thread = new Thread(this, "VideoDecoder");
		thread.start();
		return null;
	}

	boolean isStopping()
	{
		return stop;
	}

	void shutdown()
	{
		stop = true;
		core.wake();

		if (thread != null)
		{
			try
			{
				thread.join();
			}
			catch (InterruptedException ignored)
			{
				Thread.currentThread().interrupt();
			}
			thread = null;
		}

		releaseCodec();
	}

	private void releaseCodec()
	{
		if (codec != null)
		{
			try
			{
				codec.stop();
			}
			catch (Exception ignored)
			{
			}
			codec.release();
			codec = null;
		}
	}

	@Override
	public void run()
	{
		final MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
		int epoch = -1;            // not in sync with the player yet: the first thing is a seek
		boolean inputEos = false;
		boolean outputEos = false;
		boolean seeking = false;
		long targetUs = 0;
		int candidate = -1;        // the newest frame at or before the target, held back
		long candidatePts = 0;
		long lastEndUs = 0;

		try
		{
			while (!stop && !core.isClosed())
			{
				int current = core.epoch();
				if (current != epoch)
				{
					epoch = current;
					targetUs = core.seekTargetUs();

					extractor.seekTo(targetUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC);
					codec.flush(); // invalidates every buffer index, the candidate included
					candidate = -1;
					inputEos = false;
					outputEos = false;
					seeking = true;
				}

				if (outputEos)
				{
					core.sleepMs(10);
					continue;
				}

				// feed
				for (int i = 0; i < 4 && !inputEos; ++i)
				{
					int in = codec.dequeueInputBuffer(0);
					if (in < 0)
						break;

					ByteBuffer buffer = codec.getInputBuffer(in);
					int size = extractor.readSampleData(buffer, 0);
					if (size < 0)
					{
						codec.queueInputBuffer(in, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
						inputEos = true;
					}
					else
					{
						codec.queueInputBuffer(in, 0, size, extractor.getSampleTime(), 0);
						extractor.advance();
					}
				}

				// drain
				int out = codec.dequeueOutputBuffer(info, 5000);
				if (out < 0)
					continue; // nothing yet, or the output format changed (the surface copes)

				boolean eos = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
				boolean picture = info.size > 0 || !eos;
				long pts = info.presentationTimeUs;

				if (!picture)
				{
					codec.releaseOutputBuffer(out, false);
				}
				else if (seeking)
				{
					if (candidate >= 0 && pts > targetUs)
					{
						// the candidate was the one on screen at the target
						if (!showSeekFrame(candidate, candidatePts, epoch))
						{
							codec.releaseOutputBuffer(out, false);
							candidate = -1;
							continue;
						}
						candidate = -1;
						seeking = false;
						if (!present(out, pts, epoch))
							continue;
						lastEndUs = pts + frameDurationUs;
					}
					else if (candidate < 0 && pts > targetUs)
					{
						// nothing before the target (it is before the first frame): this is it
						if (!showSeekFrame(out, pts, epoch))
							continue;
						seeking = false;
						lastEndUs = pts + frameDurationUs;
					}
					else
					{
						if (candidate >= 0)
							codec.releaseOutputBuffer(candidate, false);
						candidate = out;
						candidatePts = pts;
					}
				}
				else
				{
					if (present(out, pts, epoch))
						lastEndUs = pts + frameDurationUs;
				}

				if (eos)
				{
					if (seeking)
					{
						// the target is behind the last frame: the held one is on screen
						if (candidate >= 0 && showSeekFrame(candidate, candidatePts, epoch))
							lastEndUs = candidatePts + frameDurationUs;
						else
							core.videoSeekNoFrame(epoch);
						candidate = -1;
						seeking = false;
					}
					core.videoEnded(epoch, lastEndUs);
					outputEos = true;
				}
			}
		}
		catch (Exception e)
		{
			if (!stop && !core.isClosed())
			{
				Log.w(TAG, "video stopped: " + e);
				core.fail("video decoding failed: " + e);
			}
		}
	}

	/**
	 * Renders the frame at the seek target and waits until the GL thread has picked it up
	 * (the player commits the seek then). Frames after it must not reach the surface
	 * before that, or the picture the GL thread latches would be the wrong one.
	 *
	 * @return false if a newer seek (or closing) took over meanwhile
	 */
	private boolean showSeekFrame(int index, long pts, int epoch)
	{
		core.videoSeekFrame(epoch, pts * 1000L);
		codec.releaseOutputBuffer(index, true);
		return core.awaitSeekCommit(epoch, this);
	}

	/** Shows an output buffer when its time has come. @return false if a seek took over. */
	private boolean present(int index, long pts, int epoch)
	{
		while (!stop && core.epoch() == epoch)
		{
			// A frame goes out a little early to make up for scheduling, but only while time
			// is passing: a paused video must not show a frame that is ahead of its position.
			long lead = core.clockRunning() ? PRESENT_LEAD_US : 0;
			long delta = pts - core.clockUs();
			if (delta <= lead)
				break;

			core.sleepMs(Math.min(delta / 1000, 8));
		}

		if (stop || core.epoch() != epoch)
		{
			codec.releaseOutputBuffer(index, false);
			return false;
		}

		boolean late = core.clockUs() - pts > frameDurationUs;
		codec.releaseOutputBuffer(index, !late);
		return true;
	}
}
