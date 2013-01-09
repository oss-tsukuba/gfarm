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
public class OIDDefBlock extends BinaryBlock {
	protected ArrayList definition = new ArrayList();

	public static OIDDefBlock newInstance(InputStream is, int sz)
	throws IOException
	{
		OIDDefBlock bb = new OIDDefBlock();
		bb.deserialize(is, sz);
		return bb;
	}
	public int getCount(){
		return definition.size();
	}
	/* (non-Javadoc)
	 * @see BinaryBlock#parse_binary_block(java.io.InputStream)
	 * {OID(ASN.1Ç≈ÇÕÇ»Ç≠ï∂éöóÒ)}{NULL}{ó™èÃ}{NULL}
	 */
	protected void parse_binary_block(InputStream is) throws IOException {
		byte[] buf = new byte[STRLEN];

		int read_size = 0;
		while(read_size < size){
			OIDDefElement e = new OIDDefElement();

			int cnt = readBytesWithNull(is, buf);
			e.setOID(UTY.byte2String(buf, 0, cnt));
			read_size += (cnt + 1); // '+1' means NULL.
			
			cnt = readBytesWithNull(is, buf);
			e.setNickname(UTY.byte2String(buf, 0, cnt));
			read_size += (cnt + 1); // '+1' means NULL.
	
			definition.add(e);
		}
	}

	public String toString()
	{
		StringBuffer sb = new StringBuffer("# OIDDefBlock");
		synchronized(definition){
			int cnt = definition.size();
			for(int i = 0; i < cnt; i++){
				OIDDefElement e = (OIDDefElement) definition.get(i);
				sb.append('\n');
				sb.append(e.toString(i));
			}
		}
		return sb.toString();
	}
	
	public int getOIDIndex(String nick)
	{
		int idx = -1;
		synchronized(definition){
			for(int i = 0; i < definition.size(); i++){
				OIDDefElement e = (OIDDefElement) definition.get(i);
				String n = e.getNameAndNick();
				if(n.equals(nick) == true){
					idx = i;
					break;
				}
			}
		}
		return idx;
	}

	public int getOIDIndexNext(String nick, int next)
	{
		int idx = -1;
		if(nick == null){
			return idx;
		}
		synchronized(definition){
			for(int i = 0; i < definition.size(); i++){
				OIDDefElement e = (OIDDefElement) definition.get(i);
				String n = e.getNameAndNick();
				if(n.equals(nick) == true){
					if(next <= 1){
						idx = i;
						break;
					} else {
						next--;
					}
				}
			}
		}
		return idx;
	}

	public OIDDefElement getOIDDefElement(int idx)
	{
		OIDDefElement e = null;
		synchronized(definition){
			e = (OIDDefElement) definition.get(idx);
		}
		return e;
	}

	public boolean containsEvent(String event)
	{
		int idx = getOIDIndex(event);
		if(idx < 0){
			return false;
		}else{
			return true;
		}
	}
}
