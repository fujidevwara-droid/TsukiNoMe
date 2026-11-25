package com.proj.narsen.video;

import android.annotation.TargetApi;
import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.MediaDataSource;
import android.media.MediaExtractor;
import android.os.Build;
import android.os.ParcelFileDescriptor;

import org.cocos2dx.lib.Cocos2dxActivity;
import org.cocos2dx.lib.Cocos2dxHelper;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Where a video comes from, and a MediaExtractor factory for it: the video and the audio
 * each get an extractor of their own over the same source.
 *
 * Supported: a file path, an APK asset ("assets/..." as the engine's file utils spell it),
 * a file descriptor with offset and length, and a byte array. Everything accepts a byte
 * range (offset, length; length <= 0 means "to the end").
 */
final class VideoSourceSpec
{
	private final long offset;
	private final long length;

	private AssetFileDescriptor asset;
	private ParcelFileDescriptor descriptor;
	private String filePath;
	private Object memorySource; // a MediaDataSource (API 23+)
	private File tempFile;       // memory on API 21-22

	private VideoSourceSpec(long offset, long length)
	{
		this.offset = offset;
		this.length = length;
	}

	/**
	 * @param fd a descriptor this object takes over (and closes), or -1
	 * @param memory the bytes to play (already cut to the range, so offset/length are not
	 *               applied again), or null
	 */
	static VideoSourceSpec create(String path, int fd, long offset, long length, byte[] memory) throws IOException
	{
		if (memory != null)
		{
			VideoSourceSpec spec = new VideoSourceSpec(0, 0);
			if (Build.VERSION.SDK_INT >= 23)
			{
				spec.memorySource = new ByteArraySource(memory);
			}
			else
			{
				// MediaDataSource needs API 23: older devices get a temporary file.
				Context context = Cocos2dxActivity.getContext();
				spec.tempFile = File.createTempFile("video", ".mp4", context.getCacheDir());
				FileOutputStream out = new FileOutputStream(spec.tempFile);
				try
				{
					out.write(memory);
				}
				finally
				{
					out.close();
				}
				spec.filePath = spec.tempFile.getAbsolutePath();
			}
			return spec;
		}

		VideoSourceSpec spec = new VideoSourceSpec(offset, length);
		if (fd >= 0)
		{
			spec.descriptor = ParcelFileDescriptor.adoptFd(fd);
		}
		else if (path != null && path.startsWith("assets/"))
		{
			try
			{
				spec.asset = Cocos2dxHelper.getAssetManager().openFd(path.substring("assets/".length()));
			}
			catch (FileNotFoundException e)
			{
				throw new IOException("the video is in the APK but compressed; it has to be stored uncompressed "
					+ "(androidResources { noCompress 'mp4' } in app/build.gradle): " + path, e);
			}
		}
		else if (path != null && !path.isEmpty())
		{
			spec.filePath = path;
		}
		else
		{
			throw new IOException("no video source");
		}
		return spec;
	}

	MediaExtractor newExtractor() throws IOException
	{
		MediaExtractor extractor = new MediaExtractor();
		try
		{
			if (memorySource != null)
			{
				setMemorySource(extractor);
			}
			else if (asset != null)
			{
				long start = asset.getStartOffset() + offset;
				long size = length > 0 ? length : asset.getLength() - offset;
				extractor.setDataSource(asset.getFileDescriptor(), start, size);
			}
			else if (descriptor != null)
			{
				long size = length > 0 ? length : descriptor.getStatSize() - offset;
				extractor.setDataSource(descriptor.getFileDescriptor(), offset, size);
			}
			else if (offset == 0 && length <= 0)
			{
				extractor.setDataSource(filePath);
			}
			else
			{
				FileInputStream in = new FileInputStream(filePath);
				try
				{
					long size = length > 0 ? length : in.getChannel().size() - offset;
					extractor.setDataSource(in.getFD(), offset, size);
				}
				finally
				{
					in.close(); // MediaExtractor keeps its own copy of the descriptor
				}
			}
			return extractor;
		}
		catch (IOException e)
		{
			extractor.release();
			throw e;
		}
		catch (RuntimeException e)
		{
			extractor.release();
			throw new IOException("cannot read the video: " + e.getMessage(), e);
		}
	}

	@TargetApi(23)
	private void setMemorySource(MediaExtractor extractor) throws IOException
	{
		extractor.setDataSource((MediaDataSource) memorySource);
	}

	void close()
	{
		try
		{
			if (asset != null)
				asset.close();
			if (descriptor != null)
				descriptor.close();
			if (memorySource != null && Build.VERSION.SDK_INT >= 23)
				closeMemorySource();
		}
		catch (IOException ignored)
		{
		}

		if (tempFile != null)
			tempFile.delete();

		asset = null;
		descriptor = null;
		memorySource = null;
		tempFile = null;
	}

	@TargetApi(23)
	private void closeMemorySource() throws IOException
	{
		((MediaDataSource) memorySource).close();
	}

	@TargetApi(23)
	private static final class ByteArraySource extends MediaDataSource
	{
		private final byte[] data;

		ByteArraySource(byte[] data)
		{
			this.data = data;
		}

		@Override
		public int readAt(long position, byte[] buffer, int bufferOffset, int size)
		{
			if (position >= data.length)
				return -1;

			int count = (int) Math.min((long) size, data.length - position);
			System.arraycopy(data, (int) position, buffer, bufferOffset, count);
			return count;
		}

		@Override
		public long getSize()
		{
			return data.length;
		}

		@Override
		public void close()
		{
		}
	}
}
