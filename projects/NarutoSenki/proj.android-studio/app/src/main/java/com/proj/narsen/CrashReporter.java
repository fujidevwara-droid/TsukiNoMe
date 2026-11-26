package com.proj.narsen;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.InputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Writes crash reports to /storage/emulated/0/Android/data/<package>/ :
 *   crash_java_<time>.txt   - uncaught Java/Kotlin exceptions
 *   crash_native_<time>.txt - native signals (SIGSEGV, SIGABRT, ...) from crash_handler.cpp
 * No storage permission is needed: this is the app's own external directory.
 */
public final class CrashReporter {
	private static final String TAG = "CrashReporter";
	private static volatile File sDir;
	private static volatile String sHeader = "";
	private static boolean sInstalled = false;

	private CrashReporter() {}

	private static native void nativeInstall(String dir, String header);

	public static synchronized void install(Context context) {
		if (sInstalled) return;
		sInstalled = true;

		sDir = resolveDir(context);
		sHeader = buildHeader(context);

		final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
		Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
			@Override
			public void uncaughtException(Thread thread, Throwable ex) {
				try {
					writeJavaReport(thread, ex);
				} catch (Throwable ignored) {
				}
				if (previous != null) {
					previous.uncaughtException(thread, ex);
				}
			}
		});

		try {
			nativeInstall(sDir.getAbsolutePath(), sHeader);
		} catch (Throwable t) {
			Log.w(TAG, "native crash handler unavailable", t);
		}

		startLogcatDumper(sDir);
	}

	/**
	 * Without a PC there is no way to read logcat, so the app writes its own log (Log.*
	 * and the engine's CCLOG) to logcat_latest.txt in the same folder every few seconds.
	 * After a problem that does not crash (a black screen, say), wait ~10 seconds and read
	 * that file.
	 */
	private static void startLogcatDumper(final File dir) {
		final String pid = String.valueOf(android.os.Process.myPid());
		Thread thread = new Thread(new Runnable() {
			@Override
			public void run() {
				while (true) {
					try {
						Thread.sleep(8000);
					} catch (InterruptedException e) {
						return;
					}
					dumpLogcat(dir, pid);
				}
			}
		}, "LogcatDumper");
		thread.setDaemon(true);
		thread.start();
	}

	private static void dumpLogcat(File dir, String pid) {
		Process process = null;
		try {
			process = new ProcessBuilder("logcat", "-d", "-v", "threadtime", "--pid=" + pid, "-t", "2000")
					.redirectErrorStream(true).start();
			File tmp = new File(dir, "logcat_latest.tmp");
			InputStream in = process.getInputStream();
			FileOutputStream out = new FileOutputStream(tmp);
			try {
				byte[] buffer = new byte[8192];
				int n;
				while ((n = in.read(buffer)) > 0) {
					out.write(buffer, 0, n);
				}
			} finally {
				out.close();
				in.close();
			}
			process.waitFor();
			File target = new File(dir, "logcat_latest.txt");
			target.delete();
			tmp.renameTo(target);
		} catch (Throwable ignored) {
		} finally {
			if (process != null) {
				process.destroy();
			}
		}
	}

	private static File resolveDir(Context context) {
		// getExternalFilesDir() is .../Android/data/<pkg>/files; its parent is the folder we want.
		File files = context.getExternalFilesDir(null);
		File dir = files != null ? files.getParentFile() : null;
		if (dir != null && (dir.isDirectory() || dir.mkdirs()) && dir.canWrite()) {
			return dir;
		}
		if (files != null) {
			return files; // fallback: .../Android/data/<pkg>/files
		}
		return context.getFilesDir(); // last resort: internal storage
	}

	private static String buildHeader(Context context) {
		String version = "?";
		try {
			PackageInfo pi = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
			version = pi.versionName + " (" + pi.versionCode + ")";
		} catch (Throwable ignored) {
		}
		return "Package: " + context.getPackageName() + "\n"
				+ "Version: " + version + "\n"
				+ "Device: " + Build.MANUFACTURER + " " + Build.MODEL + "\n"
				+ "Android: " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")\n"
				+ "ABI: " + Build.SUPPORTED_ABIS[0] + "\n";
	}

	private static void writeJavaReport(Thread thread, Throwable ex) throws Exception {
		File dir = sDir;
		if (dir == null) return;
		String stamp = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
		File out = new File(dir, "crash_java_" + stamp + ".txt");

		StringWriter sw = new StringWriter();
		ex.printStackTrace(new PrintWriter(sw));

		FileWriter w = new FileWriter(out);
		try {
			w.write("=== Java crash ===\n");
			w.write("Time: " + new Date() + "\n");
			w.write(sHeader);
			w.write("Thread: " + thread.getName() + "\n\n");
			w.write(sw.toString());
		} finally {
			w.close();
		}
	}
}
