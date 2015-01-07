package gmonitor.logdata;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;

/*
 * Created on 2003/05/14
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public abstract class BinaryBlock {
	protected static int STRLEN = 1024 * 64;
	boolean valid = false;
	int size = 0;
	byte[] rawbytes;

	protected byte[] readBytesWithNull(InputStream is) throws IOException
	{
		byte[] buf = new byte[STRLEN];
		int cnt = readBytesWithNull(is, buf);
		byte[] ret = new byte[cnt];
//		System.arraycopy(ret, 0, buf, 0, cnt);
		System.arraycopy(buf, 0, ret, 0, cnt);
		return ret;
	}
	protected int readBytesWithNull(InputStream is, byte[] buf) throws IOException
	{
		int pos = 0;
		if(buf == null || buf.length == 0){
			throw new NullPointerException("No space is allocated in buffer.");
		}
		int max = buf.length;
		int b = 0;
		do{
			b = is.read();
			if(b < 0){
				// End of Stream.
				pos++;
				break;
			}
			buf[pos] = (byte)b;
			pos++;
		}while(pos < max && b != 0);
		return pos - 1; // pos->count conversion and NULL termination.
	}
	private int readInt(InputStream is, int sz) throws IOException
	{
		byte[] b = new byte[sz];
		int ct = is.read(b);
		if(ct != sz){
			throw new IOException("Invalid binary format, readInt.");
		}
		return UTY.byte2int(b);
	}
	protected int read2bytesInt(InputStream is) throws IOException
	{
		return readInt(is, 2);
	}
	protected int read4bytesInt(InputStream is) throws IOException
	{
		return readInt(is, 4);
	}
	protected long read8bytesLong(InputStream is) throws IOException
	{
		int sz = 8;
		byte[] b = new byte[sz];
		int ct = is.read(b);
		if(ct != sz){
			throw new IOException("Invalid binary format, read8bytes as long.");
		}
		return UTY.byte2long(b);
	}

	protected abstract void parse_binary_block(InputStream is)
	throws IOException;

	protected void deserialize(InputStream is, int sz, boolean remainRawBytes) throws IOException
	{
		size = sz;
		rawbytes = new byte[sz];
		byte[] r = rawbytes;		
		if(remainRawBytes == false){
			rawbytes = null;
			r = new byte[sz];
		}
		is.read(r);
		ByteArrayInputStream bis = new ByteArrayInputStream(r);
		parse_binary_block(bis);
		valid = true;
	}
	protected void deserialize(InputStream is, int sz) throws IOException
	{
		deserialize(is, sz, /* TO DEBUG */ true); // メモリを節約するためには false にすること
	}
//	protected void deserialize(InputStream is, int sz) throws IOException
//	{
//		size = sz;
//		rawbytes = new byte[sz];
//		is.read(rawbytes);
//		ByteArrayInputStream bis = new ByteArrayInputStream(rawbytes);
//		parse_binary_block(bis);
//		valid = true;
//	}

	public int getSize()
	{
		return size;
	}
	public boolean isValid()
	{
		return valid;
	}
	public byte[] getRawBytes()
	{
		if(rawbytes == null){
			return null;
		}
		byte[] buf = new byte[size];
//		System.arraycopy(buf, 0, rawbytes, 0, size);
		System.arraycopy(rawbytes, 0, buf, 0, size);
		return buf;
	}

}
