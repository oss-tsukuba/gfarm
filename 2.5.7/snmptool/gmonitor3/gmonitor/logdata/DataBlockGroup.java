/*
 * Created on 2003/07/09
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;

import java.util.ArrayList;
import java.util.Date;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class DataBlockGroup extends ArrayList {

	// ここで戻される ArrayList は、必ず java.util.Date と DataElement が組になっている。
	public ArrayList pickData(DataBlockGroupTable tbl, int hidx, int oidx)
	{
		// hidx と oidx にマッチするデータを検索して取り出す。
		ArrayList ret = new ArrayList();
		ArrayList rows = tbl.getDataBlockGroupElements();
		synchronized(this){
			for(int i = 0; i < rows.size(); i++){
				DataBlockGroupElement[] row = (DataBlockGroupElement[]) rows.get(i);
				for(int j = 0; j < row.length; j++){
					if(row[j].isPairOfHIDandOID(hidx, oidx) == true){
						// この row にペアを発見した
						DataBlock db = (DataBlock) this.get(i);
						long t = db.getTime();
						Date dt = new Date(t);
						ArrayList delist = db.getData();
						DataElement de = (DataElement) delist.get(j);
						ret.add(dt);
						ret.add(de);
						// row　には必ずひとつのデータしかない。二つ以上存在しても、時刻が同一である
						// 以上、同じ計測値でなければおかしい。そもそも、同じ時刻に存在する異なる計
						// 測値を正しくグラフに描画することはできない。
						break;
					}
				}
			}
		}
		return ret;
	}
}
