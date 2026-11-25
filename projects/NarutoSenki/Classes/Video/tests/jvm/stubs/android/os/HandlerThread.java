package android.os;
public class HandlerThread extends Thread {
	private final Looper looper = new Looper();
	public HandlerThread(String name) { super(name); setDaemon(true); }
	@Override public void run() {}
	public Looper getLooper() { return looper; }
	public boolean quitSafely() { return true; }
}
