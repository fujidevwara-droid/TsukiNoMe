package com.proj.narsen.video;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTimestamp;
import android.media.AudioTrack;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.util.Log;

import java.nio.ByteBuffer;

/**
 * The sound of a video: MediaExtractor -> MediaCodec -> AudioTrack, on a thread of its
 * own. It is also the master clock of the player: getPositionUs() is the time of the
 * audio that has actually been played, so everything else (the video frames, the system
 * clock after the sound has ended) follows what the user hears.
 *
 * The AudioTrack is written without blocking, so a seek is never held up by a full
 * buffer. After a seek the first samples before the target are dropped down to the
 * sample, and the clock starts at the time of the first sample that is kept.
 */
final class AudioRenderer implements Runnable
{
	private static final String TAG = "VideoAudio";

	private final VideoPlayerCore core;
	private final MediaExtractor extractor;
	private final MediaFormat inputFormat;
	private final String mime;

	private MediaCodec codec;
	private Thread thread;
	private volatile boolean stop;

	private volatile AudioTrack track;
	private int sampleRate;
	private int channels;
	private volatile boolean playing;      // what the player wants, applied to the track
	private volatile long baseUs;          // the time the first sample after the last flush belongs to
	private volatile long framesWritten;   // since the last flush
	private volatile long lastFrames;      // keeps the clock from going backwards
	private float volume = 1.0f;
	private final AudioTimestamp timestamp = new AudioTimestamp();

	AudioRenderer(VideoPlayerCore core, MediaExtractor extractor, MediaFormat format)
	{
		this.core = core;
		this.extractor = extractor;
		this.inputFormat = format;
		this.mime = format.getString(MediaFormat.KEY_MIME);
	}

	boolean start()
	{
		try
		{
			codec = MediaCodec.createDecoderByType(mime);
			codec.configure(inputFormat, null, null, 0);
			codec.start();
		}
		catch (Exception e)
		{
			Log.w(TAG, "no audio decoder for " + mime + ": " + e);
			releaseCodec();
			return false;
		}

		thread = new Thread(this, "VideoAudio");
		thread.start();
		return true;
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

		AudioTrack t = track;
		track = null;
		if (t != null)
		{
			try
			{
				t.pause();
				t.flush();
			}
			catch (IllegalStateException ignored)
			{
			}
			t.release();
		}
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

	// ------------------------------------------------------------------
	// Control (any thread)
	// ------------------------------------------------------------------

	void play()
	{
		playing = true;
		AudioTrack t = track;
		if (t != null && t.getPlayState() != AudioTrack.PLAYSTATE_PLAYING)
			t.play();
	}

	boolean isPlaying()
	{
		return playing;
	}

	void pause()
	{
		playing = false;
		AudioTrack t = track;
		if (t != null && t.getPlayState() == AudioTrack.PLAYSTATE_PLAYING)
			t.pause();
	}

	/** Silences the sound at once and makes baseUs the time of the next sample written. */
	void pauseAndFlush(long newBaseUs)
	{
		pause();

		AudioTrack t = track;
		if (t != null)
		{
			t.flush(); // only works on a stopped or paused track, which it now is
		}

		framesWritten = 0;
		lastFrames = 0;
		baseUs = newBaseUs;
	}

	void setVolume(float newVolume)
	{
		volume = newVolume;
		AudioTrack t = track;
		if (t != null)
			t.setVolume(newVolume);
	}

	/** How far the audio has been played, in microseconds of the video's timeline. */
	long getPositionUs()
	{
		AudioTrack t = track;
		if (t == null || sampleRate == 0)
			return baseUs;

		long frames = -1;
		if (playing && t.getPlayState() == AudioTrack.PLAYSTATE_PLAYING && t.getTimestamp(timestamp))
		{
			// the hardware's own position, moved forward by the time since it was taken
			long sinceNs = System.nanoTime() - timestamp.nanoTime;
			frames = timestamp.framePosition + (sinceNs * sampleRate) / 1000000000L;
		}

		if (frames < 0)
			frames = t.getPlaybackHeadPosition() & 0xFFFFFFFFL;

		frames = Math.min(frames, framesWritten);
		if (frames < lastFrames)
			frames = lastFrames;
		lastFrames = frames;

		return baseUs + frames * 1000000L / sampleRate;
	}

	/** Everything that was written has been played. */
	boolean isDrained()
	{
		AudioTrack t = track;
		if (t == null)
			return true;

		return (t.getPlaybackHeadPosition() & 0xFFFFFFFFL) >= framesWritten;
	}

	// ------------------------------------------------------------------
	// The audio thread
	// ------------------------------------------------------------------

	@Override
	public void run()
	{
		final MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
		int epoch = -1;           // not in sync with the player yet: the first thing is a seek
		boolean inputEos = false;
		boolean outputEos = false;
		boolean seeking = false;  // dropping samples until the target
		long targetUs = 0;

		try
		{
			while (!stop)
			{
				int current = core.epoch();
				if (current != epoch)
				{
					epoch = current;
					targetUs = core.seekTargetUs();

					pauseAndFlush(targetUs);
					extractor.seekTo(targetUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC);
					codec.flush();
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
				if (out == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED)
				{
					if (!configureTrack(codec.getOutputFormat()))
					{
						core.audioFailed(epoch, "the audio format is not supported");
						return;
					}
					continue;
				}

				if (out < 0)
					continue;

				boolean eos = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
				if (info.size > 0 && track != null)
				{
					ByteBuffer pcm = codec.getOutputBuffer(out);
					pcm.position(info.offset);
					pcm.limit(info.offset + info.size);

					int frameBytes = 2 * channels;
					if (seeking)
					{
						long frames = info.size / frameBytes;
						long startUs = info.presentationTimeUs;
						long endUs = startUs + frames * 1000000L / sampleRate;
						if (endUs > targetUs)
						{
							long skip = startUs < targetUs ? Math.min(frames, (targetUs - startUs) * sampleRate / 1000000L) : 0;
							pcm.position(info.offset + (int) skip * frameBytes);
							baseUs = startUs + skip * 1000000L / sampleRate;
							seeking = false;
							write(pcm, epoch, frameBytes);
							core.audioSeekReady(epoch);
						}
					}
					else
					{
						write(pcm, epoch, frameBytes);
					}
				}

				codec.releaseOutputBuffer(out, false);

				if (eos)
				{
					outputEos = true;
					if (seeking)
					{
						// the audio ends before the target: nothing to play
						seeking = false;
						baseUs = targetUs;
						core.audioSeekReady(epoch);
					}
					core.audioEnded(epoch);
				}
			}
		}
		catch (Exception e)
		{
			if (!stop)
			{
				Log.w(TAG, "audio stopped: " + e);
				core.audioFailed(epoch, "audio decoding failed: " + e);
			}
		}
	}

	// without blocking: a full buffer must not keep a seek waiting
	private void write(ByteBuffer pcm, int epoch, int frameBytes)
	{
		AudioTrack t = track;
		while (pcm.hasRemaining() && !stop && core.epoch() == epoch)
		{
			int written = t.write(pcm, pcm.remaining(), AudioTrack.WRITE_NON_BLOCKING);
			if (written < 0)
				return;

			if (written == 0)
				core.sleepMs(5);
			else
				framesWritten += written / frameBytes;
		}
	}

	private boolean configureTrack(MediaFormat format)
	{
		int newChannels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
		int newRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE);

		int mask;
		switch (newChannels)
		{
		case 1:
			mask = AudioFormat.CHANNEL_OUT_MONO;
			break;
		case 2:
			mask = AudioFormat.CHANNEL_OUT_STEREO;
			break;
		case 6:
			mask = AudioFormat.CHANNEL_OUT_5POINT1;
			break;
		default:
			return false;
		}

		if (track != null && newChannels == channels && newRate == sampleRate)
			return true;

		AudioTrack old = track;
		track = null;
		if (old != null)
			old.release();

		int minimum = AudioTrack.getMinBufferSize(newRate, mask, AudioFormat.ENCODING_PCM_16BIT);
		int size = Math.max(minimum * 2, newRate * newChannels * 2 / 5); // about 200 ms
		AudioTrack created = new AudioTrack(
			new AudioAttributes.Builder()
				.setUsage(AudioAttributes.USAGE_MEDIA)
				.setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
				.build(),
			new AudioFormat.Builder()
				.setSampleRate(newRate)
				.setEncoding(AudioFormat.ENCODING_PCM_16BIT)
				.setChannelMask(mask)
				.build(),
			size,
			AudioTrack.MODE_STREAM,
			AudioManager.AUDIO_SESSION_ID_GENERATE);

		if (created.getState() != AudioTrack.STATE_INITIALIZED)
		{
			created.release();
			return false;
		}

		created.setVolume(volume);
		channels = newChannels;
		sampleRate = newRate;
		framesWritten = 0;
		lastFrames = 0;
		track = created;

		if (playing)
			created.play();
		return true;
	}
}
