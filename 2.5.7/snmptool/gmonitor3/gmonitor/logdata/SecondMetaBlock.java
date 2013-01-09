package gmonitor.logdata;
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
public class SecondMetaBlock extends BinaryBlock{
	HostDefBlock     hdb;
	OIDDefBlock      odb;
	IntervalDefBlock idb;
	DataBlockGroupTable dbgt;
//	int szHostDef;         // (2bytes)
//	ArrayList lsHost;      // (varibale)
//	int szOIDDef;          // (2bytes)
//	ArrayList lsOID;       // (variable)
//	int szIntervalDef;     // (2bytes)
//	ArrayList lsInterval;  // (variable)
//	int szReserved;        // (2bytes)

	public HostDefBlock getHostDefBlock()
	{
		return hdb;
	}

	public OIDDefBlock getOIDDefBlock()
	{
		return odb;
	}

	public IntervalDefBlock getIntervalDefBlock()
	{
		return idb;
	}

	public DataBlockGroupTable getDataBlockGroupTable()
	{
		return dbgt;
	}
	
	public String asDump()
	{
		StringBuffer b = new StringBuffer();
		b.append("# Second Meta Block\n");

		b.append("SecondMetaBlockSize::");
		b.append(size);
		
		b.append('\n');
		b.append(hdb.toString()); b.append('\n');
		b.append(odb.toString()); b.append('\n');
		b.append(idb.toString()); b.append('\n');
		b.append(dbgt.toString());b.append('\n');

		return b.toString();
	}
//	private static final int findzero(byte[] b, int off)
//	{
//		int i = off;
//		while(b[i] != 0)i++;
//		return i;
//	}
	public static SecondMetaBlock newInstance(InputStream is, int sz)
	throws IOException
	{
		SecondMetaBlock bb = new SecondMetaBlock();
		bb.deserialize(is, sz);
		return bb;
	}
	/* (non-Javadoc)
	 * @see BinaryBlock#parse_binary_block(java.io.ByteArrayInputStream)
	 */
	protected void parse_binary_block(InputStream is) throws IOException
	{
		// parsing and de-serializing Host Definition part.
		int sz = this.read2bytesInt(is);
		hdb = HostDefBlock.newInstance(is, sz);

		// parsing and de-serializing OID Definition part.
		sz = this.read2bytesInt(is);
		odb = OIDDefBlock.newInstance(is, sz);

		// parsing and de-serializing Interval Definition part.
		sz = this.read2bytesInt(is);
		idb = IntervalDefBlock.newInstance(is, sz);
		
		// parsing and de-serializing Data Block Group Table part.
		sz = this.read2bytesInt(is);
		dbgt = DataBlockGroupTable.newInstance(is, sz);
	}

	/**
	 * @param host
	 * @return
	 */
	public boolean containsHost(String host) {
		return hdb.containsHost(host);
	}

	/**
	 * @param event
	 * @return
	 */
	public boolean containsEvent(String event) {
		return odb.containsEvent(event);
	}

}
