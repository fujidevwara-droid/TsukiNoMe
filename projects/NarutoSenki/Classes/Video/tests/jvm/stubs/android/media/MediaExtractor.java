package android.media;

import java.io.FileDescriptor;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * A MediaExtractor over a made-up file. The "path" holds the parameters:
 *   fake:duration=3000000;vfps=30;key=30;audio=1;adur=3000000;astart=0;vstart=0;novideo=0
 * Video: 320x180, a sample every 1/vfps s, a sync sample every `key` samples.
 * Audio: 48 kHz stereo, a sample (access unit) of 1024 frames.
 * A sample's payload is its index (a long), which is all the fake codec needs.
 */
public class MediaExtractor {
	public static final int SEEK_TO_PREVIOUS_SYNC = 0;
	static final int AUDIO_RATE = 48000, AUDIO_UNIT = 1024;

	long durationUs = 3000000, audioDurationUs = 3000000, audioStartUs = 0, videoStartUs = 0, audioSeekDelayMs = 0, videoSeekDelayMs = 0;
	int vfps = 30, key = 30;
	boolean hasAudio = true, hasVideo = true;
	private int selected = -1;
	private int cursor;

	public void setDataSource(String path) throws IOException {
		if (!path.startsWith("fake:")) throw new IOException("not a fake file: " + path);
		audioDurationUs = -1;
		for (String part : path.substring(5).split(";")) {
			if (part.isEmpty()) continue;
			String[] kv = part.split("=");
			long v = Long.parseLong(kv[1]);
			switch (kv[0]) {
			case "duration": durationUs = v; break;
			case "adur": audioDurationUs = v; break;
			case "vfps": vfps = (int) v; break;
			case "key": key = (int) v; break;
			case "audio": hasAudio = v != 0; break;
			case "novideo": hasVideo = v == 0; break;
			case "astart": audioStartUs = v; break;
			case "vstart": videoStartUs = v; break;
			case "aseekdelay": audioSeekDelayMs = v; break;
			case "vseekdelay": videoSeekDelayMs = v; break;
			default: throw new IOException("bad parameter " + part);
			}
		}
		if (audioDurationUs < 0) audioDurationUs = durationUs;
	}
	public void setDataSource(FileDescriptor fd, long offset, long length) throws IOException { throw new IOException("unsupported"); }
	public void setDataSource(MediaDataSource source) throws IOException { throw new IOException("unsupported"); }

	// ---- the fake stream
	int videoCount() { return (int) ((durationUs * vfps + 999999) / 1000000); }
	long videoPts(int i) { return videoStartUs + (long) i * 1000000L / vfps; }
	int audioCount() { return (int) (((audioDurationUs * AUDIO_RATE / 1000000L) + AUDIO_UNIT - 1) / AUDIO_UNIT); }
	long audioPts(int j) { return audioStartUs + (long) j * AUDIO_UNIT * 1000000L / AUDIO_RATE; }

	public int getTrackCount() { return (hasVideo ? 1 : 0) + (hasAudio ? 1 : 0); }
	private boolean isVideoTrack(int i) { return hasVideo && i == 0; }
	public MediaFormat getTrackFormat(int i) {
		MediaFormat f = new MediaFormat();
		if (isVideoTrack(i)) {
			f.setString(MediaFormat.KEY_MIME, "video/avc"); f.setInteger(MediaFormat.KEY_WIDTH, 320); f.setInteger(MediaFormat.KEY_HEIGHT, 180);
			f.setLong(MediaFormat.KEY_DURATION, durationUs); f.setInteger(MediaFormat.KEY_FRAME_RATE, vfps);
		} else {
			f.setString(MediaFormat.KEY_MIME, "audio/mp4a-latm"); f.setInteger(MediaFormat.KEY_CHANNEL_COUNT, 2);
			f.setInteger(MediaFormat.KEY_SAMPLE_RATE, AUDIO_RATE); f.setLong(MediaFormat.KEY_DURATION, audioDurationUs);
		}
		return f;
	}
	public void selectTrack(int i) { selected = i; cursor = 0; }
	private boolean video() { return isVideoTrack(selected); }
	private int count() { return video() ? videoCount() : audioCount(); }

	public void seekTo(long timeUs, int mode) {
		long delay = video() ? videoSeekDelayMs : audioSeekDelayMs;
		if (delay > 0) try { Thread.sleep(delay); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
		if (video()) {
			int idx = 0;
			for (int i = 0; i < videoCount(); ++i) if (videoPts(i) <= timeUs) idx = i;
			cursor = idx - idx % key;                      // back to the sync sample
		} else {
			int idx = 0;
			for (int j = 0; j < audioCount(); ++j) if (audioPts(j) <= timeUs) idx = j;
			cursor = idx;
		}
	}
	public int readSampleData(ByteBuffer buffer, int offset) {
		if (cursor >= count()) return -1;
		buffer.putLong(offset, cursor);
		return 8;
	}
	public long getSampleTime() { return cursor >= count() ? -1 : (video() ? videoPts(cursor) : audioPts(cursor)); }
	public boolean advance() { ++cursor; return cursor < count(); }
	public void release() {}
}
