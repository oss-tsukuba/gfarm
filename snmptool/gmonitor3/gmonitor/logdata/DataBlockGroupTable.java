/*
 * Created on 2003/05/14
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class DataBlockGroupTable extends BinaryBlock {
	protected ArrayList table = new ArrayList(); // a list of row(it is a data block group).

	public static DataBlockGroupTable newInstance(InputStream is, int sz)
	throws IOException{
		DataBlockGroupTable dbgt = new DataBlockGroupTable();
		dbgt.deserialize(is, sz);
		return dbgt;
	}

	public ArrayList getDataBlockGroupElements()
	{
		return table;
	}

	/* (non-Javadoc)
	 * @see gmonitor.logdata.BinaryBlock#parse_binary_block(java.io.InputStream)
	 * [ {項目数n(2bytes)} {ホスト定義インデクス(2bytes)}{OID定義インデクス(2bytes)} * n ] 
	 * の繰り返し
	 */
	protected void parse_binary_block(InputStream is) throws IOException {
		int read_size = 0;

		while(read_size < size){
			int cnt = read2bytesInt(is);
			read_size += 2;
			
			DataBlockGroupElement[] row = new DataBlockGroupElement[cnt];
			table.add(row);
			for(int i = 0; i < cnt; i++){
				DataBlockGroupElement e = new DataBlockGroupElement();
				int hidx = read2bytesInt(is);
				e.setHostIndex(hidx);
				read_size += 2;
				
				int oidx = read2bytesInt(is);
				e.setOidIndex(oidx);
				read_size += 2;
				
				row[i] = e;
			}
		}
	}
	public String toString()
	{
		StringBuffer sb = new StringBuffer("# DataBlockGroupTable");
		synchronized(table){
			int cnt = table.size();
			for(int i = 0; i < cnt; i++){
				DataBlockGroupElement[] e = (DataBlockGroupElement[]) table.get(i);
				sb.append("\nDataBlockGroupElement:");
				sb.append(e.length);
				for(int j = 0; j < e.length; j++){
					sb.append(':');
					sb.append(e[j].toString());
				}
			}
		}
		return sb.toString();
	}
}
