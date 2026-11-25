package android.os;
public class ParcelFileDescriptor {
	public static ParcelFileDescriptor adoptFd(int fd) { return new ParcelFileDescriptor(); }
	public java.io.FileDescriptor getFileDescriptor() { return null; }
	public long getStatSize() { return -1; }
	public void close() throws java.io.IOException {}
}
