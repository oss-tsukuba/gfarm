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
public class IntervalDefElement {
	private int hostIndex;
	private int oidIndex;
	private long time;
	private long time_unix_seconds;
	private long time_unix_useconds;

	public String toString(int idx)
	{
		StringBuffer b = new StringBuffer();
		b.append("IntervalDefElement:");
		b.append(idx);
		b.append(':');
		b.append(hostIndex);
		b.append('-');
		b.append(oidIndex);
		b.append(':');
		b.append(time_unix_seconds);
		b.append('.');
		b.append(format(time_unix_useconds));
		return b.toString();
	}
	private String format(long val){
		String ret = "000000" + String.valueOf(val);
		return ret.substring(ret.length() - 6);
	}
	/**
	 * @return
	 */
	public int getHostIndex() {
		return hostIndex;
	}

	/**
	 * @return
	 */
	public int getOidIndex() {
		return oidIndex;
	}

	/**
	 * @return
	 */
	public long getTime() {
		return time;
	}

	/**
	 * @param i
	 */
	public void setHostIndex(int i) {
		hostIndex = i;
	}

	/**
	 * @param i
	 */
	public void setOidIndex(int i) {
		oidIndex = i;
	}

	/**
	 * @param l
	 */
	public void setTime(long l) {
		time = l;
	}

	/**
	 * @return
	 */
	public long getTime_unix_seconds() {
		return time_unix_seconds;
	}

	/**
	 * @return
	 */
	public long getTime_unix_useconds() {
		return time_unix_useconds;
	}

	/**
	 * @param l
	 */
	public void setTime_unix_seconds(long l) {
		time_unix_seconds = l;
	}

	/**
	 * @param l
	 */
	public void setTime_unix_useconds(long l) {
		time_unix_useconds = l;
	}

}
