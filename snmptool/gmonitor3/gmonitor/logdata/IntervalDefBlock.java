package gmonitor.logdata;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;

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
public class IntervalDefBlock extends BinaryBlock {
	protected ArrayList definition = new ArrayList();
	
	public static IntervalDefBlock newInstance(InputStream is, int sz)
	throws IOException
	{
		IntervalDefBlock bb = new IntervalDefBlock();
		bb.deserialize(is, sz);
		return bb;
	}
	public int getCount(){
		return definition.size();
	}
	public IntervalDefElement getElement(int idx){
		return (IntervalDefElement)definition.get(idx);
	}
	
	/* (non-Javadoc)
	 * @see BinaryBlock#parse_binary_block(java.io.InputStream)
	 * {ホストインデックス番号}{OID インデックス番号}{時間(4+4bytes)}
	 */
	protected void parse_binary_block(InputStream is) throws IOException {
		int read_size = 0;
		while(read_size < size){
			IntervalDefElement e = new IntervalDefElement();

			e.setHostIndex(read2bytesInt(is));
			read_size += 2;
			
			e.setOidIndex(read2bytesInt(is));
			read_size += 2;
			
			int unix_seconds = read4bytesInt(is);
			e.setTime_unix_seconds((long)unix_seconds);
			int unix_useconds= read4bytesInt(is);
			e.setTime_unix_useconds((long)unix_useconds);
			e.setTime(unix_seconds * 1000 + unix_useconds / 1000);
			read_size += 8;

			definition.add(e);
		}
	}
	
	public String toString()
	{
		StringBuffer sb = new StringBuffer("# IntervalDefBlock");
		synchronized(definition){
			int cnt = definition.size();
			for(int i = 0; i < cnt; i++){
				IntervalDefElement e = (IntervalDefElement) definition.get(i);
				sb.append('\n');
				sb.append(e.toString(i));
			}
		}
		return sb.toString();
	}
}
