/*
 * Created on 2003/05/14
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;
//TODO: このクラス、immutableにできるんじゃないか。DataBlockGroupTableとあわせて改造。
/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class DataBlockGroupElement {
	private int oidIndex;
	private int hostIndex;
	
	public String toString()
	{
		return String.valueOf(hostIndex) + '-' + String.valueOf(oidIndex);
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

	public boolean isPairOfHIDandOID(int hidx, int oidx)
	{
		if( (hidx == hostIndex) && (oidx == oidIndex) ){
			return true;
		}else{
			return false;
		}
	}
}
