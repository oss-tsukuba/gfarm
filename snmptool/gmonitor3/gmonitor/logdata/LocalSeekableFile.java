/*
 * Created on 2003/06/20
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class LocalSeekableFile implements SeekableFile {
	private RandomAccessFile file;
	private File originalFile;

	public LocalSeekableFile(File f) throws IOException{
		originalFile = f;
		file = new RandomAccessFile(f, "r");
	}

	/**
	 * @throws java.io.IOException
	 */
	public void close() throws IOException {
		file.close();
	}

	/* (non-Javadoc)
	 * @see java.lang.Object#equals(java.lang.Object)
	 */
	public boolean equals(Object arg0) {
		return file.equals(arg0);
	}

	/**
	 * @return
	 * @throws java.io.IOException
	 */
	public long size() throws IOException {
//System.out.println("DataFile.size(): " + originalFile.length());
		return originalFile.length();
	}

	/**
	 * @param buf
	 * @param idx
	 * @param amount
	 * @return
	 * @throws java.io.IOException
	 */
	public int read(byte[] buf, int idx, int amount) throws IOException {
		return file.read(buf, idx, amount);
	}

	/**
	 * @param pos êVÇΩÇ»ì¸óÕà íu
	 * @throws java.io.IOException
	 */
	public void seek(long pos) throws IOException {
		file.seek(pos);
	}

	/* (non-Javadoc)
	 * @see java.lang.Object#toString()
	 */
	public String toString() {
		return file.toString();
	}

}
