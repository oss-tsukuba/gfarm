/*
 * Created on 2003/05/14
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;
//TODO: Is this changed into immutable? (and DataBlockGroupTable)
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

	public int seekPairOfHIDandOID(int hidx, String event, OIDDefBlock odb)
	{
		int next = 1;
		int oidx;
		while(true){
			oidx = odb.getOIDIndexNext(event, next);
			if(oidx == -1){
				return -1;
			} else if( (hidx == hostIndex) && (oidx == oidIndex) ){
				return oidx;
			} else{
				next++;
			}
		}
	}

	public boolean isPairOfHIDandOID(int hidx, int oidx)
	{
		if( (hidx == hostIndex) && (oidx == oidIndex) ){
			return true;
		} else{
			return false;
		}
	}

}
