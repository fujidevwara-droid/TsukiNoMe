package android.media;
public class AudioFormat {
	public static final int CHANNEL_OUT_MONO = 4, CHANNEL_OUT_STEREO = 12, CHANNEL_OUT_5POINT1 = 252, ENCODING_PCM_16BIT = 2;
	public int rate, mask, enc;
	public static class Builder {
		private final AudioFormat f = new AudioFormat();
		public Builder setSampleRate(int r) { f.rate = r; return this; }
		public Builder setEncoding(int e) { f.enc = e; return this; }
		public Builder setChannelMask(int m) { f.mask = m; return this; }
		public AudioFormat build() { return f; }
	}
}
