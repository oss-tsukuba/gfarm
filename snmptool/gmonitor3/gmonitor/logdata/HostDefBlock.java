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
public class HostDefBlock extends BinaryBlock {
	protected ArrayList definition = new ArrayList();

	public static HostDefBlock newInstance(InputStream is, int sz)
	throws IOException
	{
		HostDefBlock bb = new HostDefBlock();
		bb.deserialize(is, sz);
		return bb;
	}
	public int getCount(){
		return definition.size();
	}
	/* (non-Javadoc)
	 * @see BinaryBlock#parse_binary_block(java.io.InputStream)
	 * {IPアドレス(文字列)}{NULL}{FQDN}{NULL}{略称}{NULL}{コミュニティストリング}{NULL}
	 */
	protected void parse_binary_block(InputStream is) throws IOException {
		byte[] buf = new byte[STRLEN];

		int read_size = 0;
		while(read_size < size){
			HostDefElement e = new HostDefElement();

			// Read string represented IP Address.
			String ipaddr;
			int cnt = readBytesWithNull(is, buf);
			e.setIpAddr(UTY.byte2String(buf, 0, cnt));
			read_size += (cnt + 1); // '+1' means NULL.
	
			// Read Hostname.
			cnt = readBytesWithNull(is, buf);
			e.setHostname(UTY.byte2String(buf, 0, cnt));
			read_size += (cnt + 1); // '+1' means NULL.

			// Read Nickname of host.	
			cnt = readBytesWithNull(is, buf);
			e.setNickname(UTY.byte2String(buf, 0, cnt));
			read_size += (cnt + 1); // '+1' means NULL.
			
			// Read community string of host.
			cnt = readBytesWithNull(is, buf);
			e.setCommunityString(UTY.byte2String(buf, 0, cnt));
			read_size += (cnt + 1); // '+1' means NULL.
	
			// add definition element.
			definition.add(e);
		}
	}

	public String toString()
	{
		StringBuffer sb = new StringBuffer("# HostDefBlock");
		synchronized(definition){
			int cnt = definition.size();
			for(int i = 0; i < cnt; i++){
				HostDefElement e = (HostDefElement) definition.get(i);
				sb.append('\n');
				sb.append(e.toString(i));
			}
		}
		return sb.toString();
	}
	
	public int getHostIndex(String host)
	{
		int idx = -1;
		synchronized(definition){
			for(int i = 0; i < definition.size(); i++){
				HostDefElement e = (HostDefElement) definition.get(i);
				String h = e.getNameAndNick();
				if(h.equals(host) == true){
					idx = i;
					break;
				}
			}
		}
		return idx;
	}

	public HostDefElement getHostDefElement(int idx)
	{
		HostDefElement e = null;
		synchronized(definition){
			e = (HostDefElement) definition.get(idx);
		}
		return e;
	}

	public boolean containsHost(String host)
	{
		int idx = getHostIndex(host);
		if(idx < 0){
			return false;
		}else{
			return true;
		}
	}
}
