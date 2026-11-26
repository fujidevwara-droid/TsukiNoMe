// Native crash reporter: writes a report file when the process receives a fatal signal.
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <unwind.h>

#define CR_TAG "CrashHandler"

namespace
{
	char g_dir[512] = {0};
	char g_header[1024] = {0};
	struct sigaction g_old[NSIG];
	const int kSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGTRAP};
	volatile sig_atomic_t g_inHandler = 0;

	struct BtState
	{
		int fd;
		int n;
	};

	void writeStr(int fd, const char *s)
	{
		if (fd >= 0)
			write(fd, s, strlen(s));
	}

	_Unwind_Reason_Code unwindCb(struct _Unwind_Context *ctx, void *arg)
	{
		BtState *st = static_cast<BtState *>(arg);
		uintptr_t pc = _Unwind_GetIP(ctx);
		if (pc == 0 || st->n >= 128)
			return _URC_END_OF_STACK;

		char line[512];
		Dl_info info;
		if (dladdr(reinterpret_cast<void *>(pc), &info) && info.dli_fname)
		{
			uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);
			snprintf(line, sizeof(line), "  #%02d pc %016lx  %s (%s+%lu)\n", st->n,
					 (unsigned long)(pc - base), info.dli_fname,
					 info.dli_sname ? info.dli_sname : "?",
					 info.dli_saddr ? (unsigned long)(pc - reinterpret_cast<uintptr_t>(info.dli_saddr)) : 0UL);
		}
		else
		{
			snprintf(line, sizeof(line), "  #%02d pc %016lx\n", st->n, (unsigned long)pc);
		}
		writeStr(st->fd, line);
		st->n++;
		return _URC_NO_REASON;
	}

	void handler(int sig, siginfo_t *info, void *ucontext)
	{
		if (!g_inHandler)
		{
			g_inHandler = 1;

			char path[640];
			snprintf(path, sizeof(path), "%s/crash_native_%ld.txt", g_dir, (long)time(NULL));
			int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

			char line[256];
			writeStr(fd, "=== Native crash ===\n");
			writeStr(fd, g_header);
			snprintf(line, sizeof(line), "Signal: %d (%s), code %d, fault addr %p\n", sig,
					 strsignal(sig), info ? info->si_code : 0, info ? info->si_addr : nullptr);
			writeStr(fd, line);
			snprintf(line, sizeof(line), "PID: %d  TID: %d\n\nBacktrace:\n", (int)getpid(), (int)gettid());
			writeStr(fd, line);

			BtState st = {fd, 0};
			_Unwind_Backtrace(unwindCb, &st);
			writeStr(fd, "\n(Symbolize with: ndk-stack / addr2line against libcocos2dcpp.so)\n");

			if (fd >= 0)
				close(fd);
			__android_log_print(ANDROID_LOG_FATAL, CR_TAG, "native crash report: %s", path);
		}

		// Restore the previous handler and re-raise so Android still produces its tombstone.
		sigaction(sig, &g_old[sig], NULL);
		raise(sig);
	}
}

extern "C" JNIEXPORT void JNICALL
Java_com_proj_narsen_CrashReporter_nativeInstall(JNIEnv *env, jclass, jstring dir, jstring header)
{
	const char *d = env->GetStringUTFChars(dir, NULL);
	const char *h = env->GetStringUTFChars(header, NULL);
	strncpy(g_dir, d, sizeof(g_dir) - 1);
	strncpy(g_header, h, sizeof(g_header) - 1);
	env->ReleaseStringUTFChars(dir, d);
	env->ReleaseStringUTFChars(header, h);

	// Alternate stack so stack-overflow crashes can still be reported.
	static char altStack[64 * 1024];
	stack_t ss;
	ss.ss_sp = altStack;
	ss.ss_size = sizeof(altStack);
	ss.ss_flags = 0;
	sigaltstack(&ss, NULL);

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	for (int s : kSignals)
		sigaction(s, &sa, &g_old[s]);
}
