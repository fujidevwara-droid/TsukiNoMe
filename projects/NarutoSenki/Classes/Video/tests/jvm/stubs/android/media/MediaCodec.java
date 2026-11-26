package android.media;

import android.view.Surface;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;

/**
 * A MediaCodec that "decodes" the fake streams: a video sample becomes a frame that is
 * "rendered" (sent to the Surface) when released with render=true, an audio sample becomes
 * 1024 frames of PCM whose left channel counts the sample index (so a test can see exactly
 * which samples reached the AudioTrack). Behaves like a synchronous-mode codec: limited
 * buffers, flush() throws everything away, output buffers wait to be released.
 */
public final class MediaCodec {
	public static final int BUFFER_FLAG_END_OF_STREAM = 4, INFO_TRY_AGAIN_LATER = -1, INFO_OUTPUT_FORMAT_CHANGED = -2;

	public static final class BufferInfo { public int offset, size, flags; public long presentationTimeUs; }

	private static final int INPUTS = 4, OUTPUTS = 4;
	private final String mime;
	private final boolean video;
	private MediaFormat format;
	private Surface surface;
	private final boolean[] inputBusy = new boolean[INPUTS];
	private final ByteBuffer[] inputs = new ByteBuffer[INPUTS];
	private final ByteBuffer[] outputs = new ByteBuffer[OUTPUTS];
	private final boolean[] outputUsed = new boolean[OUTPUTS];
	private final long[] outPts = new long[OUTPUTS];
	private final int[] outFlags = new int[OUTPUTS], outSize = new int[OUTPUTS];
	private final ArrayDeque<Integer> ready = new ArrayDeque<Integer>();
	private final ArrayDeque<long[]> pending = new ArrayDeque<long[]>();   // {index, pts, flags}
	private boolean formatChange;
	private boolean released;

	private MediaCodec(String mime) {
		this.mime = mime; video = mime.startsWith("video/");
		for (int i = 0; i < INPUTS; ++i) inputs[i] = ByteBuffer.allocate(64).order(ByteOrder.nativeOrder());
		for (int i = 0; i < OUTPUTS; ++i) outputs[i] = ByteBuffer.allocate(video ? 16 : 4096).order(ByteOrder.LITTLE_ENDIAN);
	}
	public static MediaCodec createDecoderByType(String mime) { return new MediaCodec(mime); }
	public static MediaCodec createByCodecName(String name) { return new MediaCodec("video/avc"); }
	public String getName() { return "fake.decoder"; }

	public void configure(MediaFormat f, Surface s, Object crypto, int flags) { format = f; surface = s; }
	public synchronized void start() { formatChange = !video; }
	public synchronized void stop() {}
	public synchronized void release() { released = true; }

	public synchronized void flush() {
		ready.clear(); pending.clear();
		for (int i = 0; i < OUTPUTS; ++i) outputUsed[i] = false;
		for (int i = 0; i < INPUTS; ++i) inputBusy[i] = false;
	}

	public synchronized int dequeueInputBuffer(long timeoutUs) {
		for (int i = 0; i < INPUTS; ++i) if (!inputBusy[i]) { inputBusy[i] = true; return i; }
		return -1;
	}
	public ByteBuffer getInputBuffer(int i) { inputs[i].clear(); return inputs[i]; }

	public synchronized void queueInputBuffer(int i, int offset, int size, long pts, int flags) {
		long index = size >= 8 ? inputs[i].getLong(offset) : -1;
		inputBusy[i] = false;
		pending.add(new long[] { index, pts, flags });
		decode();
	}

	private void decode() {
		while (!pending.isEmpty()) {
			int slot = -1;
			for (int i = 0; i < OUTPUTS; ++i) if (!outputUsed[i]) { slot = i; break; }
			if (slot < 0) return;
			long[] in = pending.poll();
			outputUsed[slot] = true;
			outPts[slot] = in[1]; outFlags[slot] = (int) in[2] & BUFFER_FLAG_END_OF_STREAM;
			if ((in[2] & BUFFER_FLAG_END_OF_STREAM) != 0) outSize[slot] = 0;
			else if (video) outSize[slot] = 1;
			else {
				ByteBuffer pcm = outputs[slot]; pcm.clear();
				for (int k = 0; k < MediaExtractor.AUDIO_UNIT; ++k) {
					short v = (short) ((in[0] * MediaExtractor.AUDIO_UNIT + k) & 0x7fff);
					pcm.putShort(v); pcm.putShort((short) (v ^ 0x1234));
				}
				outSize[slot] = pcm.position();
			}
			ready.add(slot);
		}
	}

	public int dequeueOutputBuffer(BufferInfo info, long timeoutUs) {
		synchronized (this) {
			if (formatChange) { formatChange = false; return INFO_OUTPUT_FORMAT_CHANGED; }
			Integer slot = ready.poll();
			if (slot != null) {
				info.offset = 0; info.size = outSize[slot]; info.presentationTimeUs = outPts[slot]; info.flags = outFlags[slot];
				return slot;
			}
		}
		try { Thread.sleep(1); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
		return INFO_TRY_AGAIN_LATER;
	}
	public ByteBuffer getOutputBuffer(int i) { return outputs[i]; }
	public MediaFormat getOutputFormat() {
		MediaFormat f = new MediaFormat();
		f.setInteger(MediaFormat.KEY_CHANNEL_COUNT, 2); f.setInteger(MediaFormat.KEY_SAMPLE_RATE, MediaExtractor.AUDIO_RATE);
		return f;
	}

	public void releaseOutputBuffer(int i, boolean render) {
		long pts;
		synchronized (this) { pts = outPts[i]; outputUsed[i] = false; decode(); }
		if (render && video && surface != null) surface.render(pts * 1000L);
	}
}
