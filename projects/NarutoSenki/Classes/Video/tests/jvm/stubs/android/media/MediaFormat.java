package android.media;
import java.util.HashMap;
public final class MediaFormat {
	public static final String KEY_MIME = "mime", KEY_WIDTH = "width", KEY_HEIGHT = "height", KEY_DURATION = "durationUs",
		KEY_FRAME_RATE = "frame-rate", KEY_CHANNEL_COUNT = "channel-count", KEY_SAMPLE_RATE = "sample-rate";
	private final HashMap<String, Object> map = new HashMap<String, Object>();
	public static MediaFormat createVideoFormat(String mime, int w, int h) {
		MediaFormat f = new MediaFormat(); f.setString(KEY_MIME, mime); f.setInteger(KEY_WIDTH, w); f.setInteger(KEY_HEIGHT, h); return f;
	}
	public boolean containsKey(String k) { return map.containsKey(k); }
	public String getString(String k) { return (String) map.get(k); }
	public int getInteger(String k) { return (Integer) map.get(k); }
	public long getLong(String k) { return (Long) map.get(k); }
	public void setString(String k, String v) { map.put(k, v); }
	public void setInteger(String k, int v) { map.put(k, v); }
	public void setLong(String k, long v) { map.put(k, v); }
}
