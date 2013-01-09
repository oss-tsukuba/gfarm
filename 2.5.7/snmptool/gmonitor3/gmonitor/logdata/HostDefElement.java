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
public class HostDefElement {
	private String ipAddr;
	private String hostname;
	private String nickname;
	private String communityString;

	public String toString(int idx)
	{
		StringBuffer b = new StringBuffer();
		b.append("HostDefElement:");
		b.append(idx);
		b.append(':');
		b.append(ipAddr);
		b.append(':');
		b.append(hostname);
		b.append(':');
		b.append(nickname);
		b.append(':');
		b.append(communityString);
		return b.toString();
	}
	/**
	 * @return
	 */
	public String getCommunityString() {
		return communityString;
	}

	/**
	 * @return
	 */
	public String getNameAndNick() {
		//return hostname;
		return nickname + "#" + hostname;
	}

	public String getHostname() {
		return hostname;
	}
	
	public String getIpAddr() {
		return ipAddr;
	}

	/**
	 * @return
	 */
	public String getNickname() {
		return nickname;
	}

	/**
	 * @param string
	 */
	public void setCommunityString(String string) {
		communityString = string;
	}

	/**
	 * @param string
	 */
	public void setHostname(String string) {
		hostname = string;
	}

	public void setIpAddr(String addr) {
		ipAddr = addr;
	}

	/**
	 * @param string
	 */
	public void setNickname(String string) {
		nickname = string;
	}

}
