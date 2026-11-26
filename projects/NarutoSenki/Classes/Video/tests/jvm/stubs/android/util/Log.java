package android.util;
public class Log {
	public static boolean verbose = false;
	public static int w(String tag, String msg) { if (verbose) System.out.println("W/" + tag + ": " + msg); return 0; }
	public static int e(String tag, String msg) { if (verbose) System.out.println("E/" + tag + ": " + msg); return 0; }
	public static int d(String tag, String msg) { return 0; }
}
