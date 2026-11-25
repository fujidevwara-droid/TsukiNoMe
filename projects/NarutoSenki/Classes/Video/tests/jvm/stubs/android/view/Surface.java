package android.view;
import android.graphics.SurfaceTexture;
public class Surface {
	public interface RenderListener { void rendered(long timestampNs); }
	public static volatile RenderListener renderListener;
	private final SurfaceTexture texture;
	public Surface(SurfaceTexture t) { texture = t; }
	public void render(long timestampNs) {
		RenderListener l = renderListener;
		if (l != null) l.rendered(timestampNs);
		texture.queueFrame(timestampNs);
	}
	public void release() {}
}
