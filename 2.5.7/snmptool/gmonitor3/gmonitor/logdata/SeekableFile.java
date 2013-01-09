/*
 * Created on 2003/06/20
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;

import java.io.IOException;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public interface SeekableFile {

	public void close() throws IOException;

	/**
	 * @return
	 * @throws java.io.IOException
	 */
	public long size() throws IOException;

	/**
	 * @param buf 読み出しバッファ
	 * @param idx バッファ中の開始位置
	 * @param amount 読み出し量
	 * @return
	 * @throws java.io.IOException
	 */
	public int read(byte[] buf, int idx, int amount) throws IOException;
	
	public void seek(long pos) throws IOException;

}
