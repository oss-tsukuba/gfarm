/*
 * Created on 2003/07/07
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class DataElement {

	public static final int VALID_FLAG = 0;
	public static final int SUCCESS_FLAG = 1;
//	public static final int RESERVED2_FLAG = 2;
//	public static final int RESERVED3_FLAG = 3;
//	public static final int RESERVED4_FLAG = 4;
//	public static final int RESERVED5_FLAG = 5;
//	public static final int RESERVED6_FLAG = 6;
//	public static final int RESERVED7_FLAG = 7;
	
	/**
	 * @param b
	 * @param val
	 */
	public DataElement(byte b, long val) {
		value = val;
		flagByte = b;

//    	for(int i = 0; i < 8; i++){
//			flags[i] = (((int)b & (0x01 << i)) != 0) ? true : false;
//    	}

		flags[0] = (((int)b & 0x01) != 0) ? true : false;
		flags[1] = (((int)b & 0x02) != 0) ? true : false;
//		flags[2] = (((int)b & 0x04) != 0) ? true : false;
//		flags[3] = (((int)b & 0x08) != 0) ? true : false;
//		flags[4] = (((int)b & 0x10) != 0) ? true : false;
//		flags[5] = (((int)b & 0x20) != 0) ? true : false;
//		flags[6] = (((int)b & 0x40) != 0) ? true : false;
//		flags[7] = (((int)b & 0x80) != 0) ? true : false;		
	}

	byte flagByte;
	//boolean[] flags = new boolean[8];
	boolean[] flags = new boolean[2];
	long value = 0;
	
	public byte getFlagByte() {
		return flagByte;
	}

	/**
	 * @return
	 */
	public boolean[] getFlags() {
		return flags;
	}

	/**
	 * @param idx
	 * @return
	 */
	public boolean getFlag(int idx){
		return flags[idx];
	}

	/**
	 * @return
	 */
	public long getValue() {
		return value;
	}

	/**
	 * @param bs
	 */
	public void setFlags(boolean[] bs) {
		flags = bs;
	}

	/**
	 * @param i
	 */
	public void setValue(long i) {
		value = i;
	}

	public String asDump(int hid, int oid)
	{
		StringBuffer b = new StringBuffer();
		b.append("MeasurementData:" + hid + "-" + oid + ":");
		for(int i = 7; i >= 0; i--){
			if(flags[i] == true){
				b.append('1');
			}else{
				b.append('0');
			}
		}
		b.append(":" + value);
		return b.toString();
	}
}
