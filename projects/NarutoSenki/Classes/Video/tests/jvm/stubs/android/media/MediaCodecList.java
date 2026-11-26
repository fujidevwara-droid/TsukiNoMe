package android.media;
public class MediaCodecList {
	public static final int REGULAR_CODECS = 0;
	public static volatile boolean supported = true;
	public MediaCodecList(int kind) {}
	public MediaCodecInfo[] getCodecInfos() { return new MediaCodecInfo[0]; }
	public String findDecoderForFormat(MediaFormat f) { return supported ? "fake.decoder" : null; }
}
