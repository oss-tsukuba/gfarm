/*
 * Created on 2003/06/20
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;

import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.URL;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class HttpSeekableFile implements SeekableFile {

	URL url;
	long pos;

	/**
	 * @param u
	 */
	public HttpSeekableFile(URL u) {
		try {
			url = new URL(u.toString());
			pos = 0;
		} catch (MalformedURLException e) {
			// Cannot be reached.
			e.printStackTrace();
		}
	}

	/* (non-Javadoc)
	 * @see scratch.SeekableFile#close()
	 */
	public void close() throws IOException {
		// Nothing to do.
	}

	/* (non-Javadoc)
	 * @see scratch.SeekableFile#length()
	 */
	public long size() throws IOException {
		HttpURLConnection c = (HttpURLConnection) url.openConnection();
		c.setRequestMethod("HEAD");
		c.connect();
		long len = c.getContentLength();
		c.disconnect();
		return len;
	}

	/* (non-Javadoc)
	 * @see scratch.SeekableFile#read(byte[], int, int)
	 */
	public int read(byte[] buf, int idx, int amount) throws IOException {
		HttpURLConnection c = (HttpURLConnection) url.openConnection();
		String begin = Long.toString(pos);
		String end = Long.toString(pos + (long)(amount - idx) + 1);
		c.setRequestProperty("Range", "bytes=" + begin + "-" + end);
		c.connect();
		InputStream is = c.getInputStream();
		int len = is.read(buf, idx, amount);
		return len;
	}

	/* (non-Javadoc)
	 * @see scratch.SeekableFile#seek(long)
	 */
	public void seek(long pos) throws IOException {
		this.pos = pos;
	}

}
