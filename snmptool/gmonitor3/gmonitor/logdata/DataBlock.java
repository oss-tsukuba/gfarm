/*
 * Created on 2003/07/07
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class DataBlock {

	/**
	 * 空のデータブロックを生成する 
	 */
	public DataBlock() {
		valid = false;
	}

	boolean valid = true;
	int timeInUNIXSeconds;
	int timeInUNIXuSeconds;
	long time;
	ArrayList data = new ArrayList();

	/**
	 * バイナリ配列からデータブロックを生成する
	 */
	public DataBlock(byte[] bytes, int i, int num)
	{
		timeInUNIXSeconds = 
			( ((bytes[i++]&0xFF)<<24) | ((bytes[i++]&0xFF)<<16)
			| ((bytes[i++]&0xFF)<<8)  | (bytes[i++]&0xFF)
			);
		timeInUNIXuSeconds = 
			( ((bytes[i++]&0xFF)<<24) | ((bytes[i++]&0xFF)<<16)
			| ((bytes[i++]&0xFF)<<8)  | (bytes[i++]&0xFF)
			);
			
//		byte[] b4 = new byte[4];
//		int i = idx;
//		b4[0] = bytes[i + 0];
//		b4[1] = bytes[i + 1];
//		b4[2] = bytes[i + 2];
//		b4[3] = bytes[i + 3];
//		timeInUNIXSeconds = UTY.byte2int4(b4); i += 4;
//
//		b4[0] = bytes[i + 0];
//		b4[1] = bytes[i + 1];
//		b4[2] = bytes[i + 2];
//		b4[3] = bytes[i + 3];
//		timeInUNIXuSeconds = UTY.byte2int4(b4);i += 4;

		time = (long) (timeInUNIXSeconds * 1000L + timeInUNIXuSeconds / 1000L);
		long val;
		byte b;
		for(int j = 0; j < num; j++){
			b = bytes[i++];
			val =
				( ((bytes[i++]&0xFFL)<<24) | ((bytes[i++]&0xFFL)<<16)
				| ((bytes[i++]&0xFFL)<<8)  | (bytes[i++]&0xFFL)
				);
			
//			byte b = bytes[i]; i++;
//			byte[] b8 = new byte[8];
//			b8[0] = b8[1] = b8[2] = b8[3] = 0; // padding MSB.
//			b8[4] = bytes[i + 0];
//			b8[5] = bytes[i + 1];
//			b8[6] = bytes[i + 2];
//			b8[7] = bytes[i + 3];
//			long val= UTY.byte2long(b8); i += 4;
			DataElement e = new DataElement(b, val);
			data.add(e);
		}
		//valid = true;
	}
	/**
	 * @return
	 */
	public ArrayList getData() {
		return data;
	}
	
	/**
	 * @return
	 */
	public int getTimeInUNIXSeconds() {
		return timeInUNIXSeconds;
	}

	/**
	 * @return
	 */
	public int getTimeInUNIXuSeconds() {
		return timeInUNIXuSeconds;
	}

	/**
	 * @return
	 */
	public boolean isValid() {
		return valid;
	}
	
	/**
	 * 
	 */
	public String asDump(int i, IntervalDefBlock idb) {
		StringBuffer b = new StringBuffer();
		SimpleDateFormat df = new SimpleDateFormat("yyyy/MM/dd HH:mm:ss");
		long t = (long) (timeInUNIXSeconds * 1000L);
		String dt = df.format(new Date(t));
		b.append("# DataBlock " + i + "\n");
		if(valid == true){
			b.append("TimeStampInSeconds::" + timeInUNIXSeconds + ":\"");
			b.append(dt + "\"\n");
			b.append("TimeStampInuSeconds::" + timeInUNIXuSeconds + "\n");
		
			for(int j = 0; j < data.size(); j++){
				IntervalDefElement ie = idb.getElement(j);
				int hid = ie.getHostIndex();
				int oid = ie.getOidIndex();
				DataElement e = (DataElement) data.get(j);
				b.append(e.asDump(hid, oid));
				b.append("\n");
			}
		}
		return b.toString();		
	}

	/**
	 * @return
	 */
	public long getTime() {
		return time;
	}

}
