package gmonitor.logdata;
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
public class OIDDefElement {
	private String OID;
	private String nickname;
	
	public String toString(int idx)
	{
		StringBuffer b = new StringBuffer();
		b.append("OIDDefElement:");
		b.append(idx);
		b.append(':');
		b.append(OID);
		b.append(':');
		b.append(nickname);
		return b.toString();
	}
	/**
	 * @return
	 */
	public String getNameAndNick() {
		//return nickname;
		return nickname + "#" + OID;
	}

	public String getNickname() {
		return nickname;
	}
	
	/**
	 * @return
	 */
	public String getOID() {
		return OID;
	}

	/**
	 * @param string
	 */
	public void setNickname(String string) {
		nickname = string;
	}

	/**
	 * @param string
	 */
	public void setOID(String string) {
		OID = string;
	}

}
