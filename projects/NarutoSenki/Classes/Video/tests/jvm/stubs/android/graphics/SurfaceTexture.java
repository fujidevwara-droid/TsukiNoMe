package android.graphics;
import android.os.Handler;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
public class SurfaceTexture {
	public interface OnFrameAvailableListener { void onFrameAvailable(SurfaceTexture t); }
	public static final List<Long> latched = Collections.synchronizedList(new ArrayList<Long>());
	private final ArrayDeque<Long> queue = new ArrayDeque<Long>();
	private volatile OnFrameAvailableListener listener;
	private long timestamp;
	private boolean released;
	public SurfaceTexture(int texName) {}
	public void setOnFrameAvailableListener(OnFrameAvailableListener l, Handler h) { listener = l; }
	public void setOnFrameAvailableListener(OnFrameAvailableListener l) { listener = l; }
	public void queueFrame(long ts) {
		synchronized (this) { if (released) return; queue.add(ts); }
		OnFrameAvailableListener l = listener;
		if (l != null) l.onFrameAvailable(this);
	}
	public synchronized void updateTexImage() {
		if (released) throw new IllegalStateException("released");
		Long ts = queue.poll();
		if (ts != null) { timestamp = ts; latched.add(ts); }
	}
	public long getTimestamp() { return timestamp; }
	public void getTransformMatrix(float[] m) { for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1f : 0f; }
	public synchronized void release() { released = true; queue.clear(); }
}
