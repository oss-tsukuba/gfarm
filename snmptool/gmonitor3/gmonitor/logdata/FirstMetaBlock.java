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
public class FirstMetaBlock extends BinaryBlock{
	int version;
	int szGroup;
	long beginDate;
	long beginDate_unix_seconds;
	long beginDate_unix_useconds;
	long groupInterval;
	long groupInterval_unix_seconds;
	long groupInterval_unix_useconds;

	public int getDataBlockGroupSize()
	{
		return szGroup;
	}
	public String asDump()
	{
		StringBuffer b = new StringBuffer();
		b.append("# First Meta Block\n");
		
		b.append("VersionOfFormat::");
		b.append(version);

		b.append('\n');
		b.append("SizeOfDataBlockGroup::");
		b.append(szGroup);

		b.append('\n');
		b.append("StartedDateTimeInUNIXSeconds::");
		b.append(beginDate_unix_seconds);

		b.append('\n');
		b.append("StartedDateTimeInUNIXuSeconds::");
		b.append(beginDate_unix_useconds);

		b.append('\n');
		b.append("DataBlockGroupIntervalInUNIXSeconds::");
		b.append(groupInterval_unix_seconds);

		b.append('\n');
		b.append("DataBlockGroupIntervalInUNIXuSeconds::");
		b.append(groupInterval_unix_useconds);

		return b.toString();
	}
		
	protected void parse_binary_block(InputStream is) throws IOException
	{
		version = read2bytesInt(is);
		szGroup = read4bytesInt(is);
		beginDate_unix_seconds = read4bytesInt(is);
		beginDate_unix_useconds= read4bytesInt(is);
		beginDate = beginDate_unix_seconds * 1000;
		beginDate+= beginDate_unix_useconds / 1000;
		groupInterval_unix_seconds = read4bytesInt(is);
		groupInterval_unix_useconds= read4bytesInt(is);
		groupInterval = groupInterval_unix_seconds * 1000;
		groupInterval+= groupInterval_unix_useconds / 1000;
		valid = true;
	}

	public static FirstMetaBlock newInstance(InputStream is, int sz)
	throws IOException
	{
		FirstMetaBlock bb = new FirstMetaBlock();
		bb.deserialize(is, sz);
		return bb;
	}
	public int getVersion()
	{
		return version;
	}
	public int getSizeOfGroup()
	{
		return szGroup;
	}
	public long getBeginDate()
	{
		return beginDate;
	}
	public long getGroupInterval()
	{
		return groupInterval;
	}

}
