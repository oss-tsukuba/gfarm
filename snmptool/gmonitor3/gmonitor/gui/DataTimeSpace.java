/*
 * Created on 2003/07/09
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.gui;
import gmonitor.logdata.DataBlock;
import gmonitor.logdata.DataBlockGroupElement;
import gmonitor.logdata.DataBlockGroupTable;
import gmonitor.logdata.DataElement;
import gmonitor.logdata.DataFile;
import gmonitor.logdata.FirstMetaBlock;
import gmonitor.logdata.HostDefBlock;
import gmonitor.logdata.HostDefElement;
import gmonitor.logdata.OIDDefBlock;
import gmonitor.logdata.OIDDefElement;
import gmonitor.logdata.SecondMetaBlock;

import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.TreeSet;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class DataTimeSpace {
	ArrayList files = new ArrayList(); // DataFile のリスト
	TreeSet hostnames = new TreeSet();
	TreeSet eventnames= new TreeSet();

	long largestGroupInterval = 1;
	long smallestGroupInterval = 1;
	
	private long COUNTMAX = 4294967296L; // 32bit
	private long MAXbps = 1500L*1000L*1000L;  // 1.5Gbps
	
	public long getBeginDateTime() throws IOException
	{
		if(files != null && files.size() > 0 ){
			// compared
			DataFile f = (DataFile) files.get(0);
			return f.getBeginDateTime();
		} else {
			throw new IOException();
		}
	}
	
	public long getLatestDateTime() throws IOException
	{
		if(files != null && files.size() > 0 ){
			// compared
			DataFile f = (DataFile) files.get(files.size() - 1);
			return f.getLatestDateTime();
		} else {
			throw new IOException();
		}
	}

	// ファイル名の配列を与え、対象とする時空間を設定する	
	public DataTimeSpace(String[] list) throws IOException
	{
		largestGroupInterval = 1;
		smallestGroupInterval = Long.MAX_VALUE;
		for(int i = 0; i < list.length; i++){
			DataFile f = DataFile.getInstance(list[i]);
//			f.reload();
			SecondMetaBlock smb = f.getSecondMetaBlock();
FirstMetaBlock fmb = f.getFirstMetaBlock();
long intv = fmb.getGroupInterval();
if(largestGroupInterval < intv) largestGroupInterval = intv;
if(smallestGroupInterval > intv) smallestGroupInterval = intv;
			// Extract all hostnames for Human readable interface.
			HostDefBlock hdb = smb.getHostDefBlock();
			int hdb_count = hdb.getCount();
			for(int j = 0; j < hdb_count; j++){
				HostDefElement e = hdb.getHostDefElement(j);
				String h = e.getNameAndNick();
				hostnames.add(h);
			}

			// Extract all eventnames for Human readable interface.
			OIDDefBlock odb = smb.getOIDDefBlock();
			int odb_count = odb.getCount();
			for(int j = 0; j < odb_count; j++){
				OIDDefElement e = odb.getOIDDefElement(j);
				String n = e.getNameAndNick();
				eventnames.add(n);
			}

			// Add file to file list.
			files.add(f);
		}
		Collections.sort(files);
	}

	public String[] getHostnames()
	{
		String[] hosts;
		synchronized(hostnames){
			hosts = new String[hostnames.size()];
			hosts = (String[]) hostnames.toArray(hosts);
		}
		return hosts;
	}
	
	public String[] getEvents()
	{
		String[] events;
		synchronized(eventnames){
			events = new String[eventnames.size()];
			events = (String[]) eventnames.toArray(events);
		}
		return events;
	}
	
	private ArrayList extract_aList(String host)
	{
		ArrayList aList = new ArrayList();
//System.out.println("ALIST:");
		for(int i = 0; i < files.size(); i++){
			DataFile f = (DataFile) files.get(i);
			if(f.containsHost(host) == true){
				aList.add(f);
//System.out.println(f.getUrl());
			}
		}
		return aList;
	}

	private ArrayList extract_bList(ArrayList aList, String event)
	{
		ArrayList bList = new ArrayList();
//System.out.println("BLIST:");
		for(int i = 0; i < aList.size(); i++){
			DataFile f = (DataFile) aList.get(i);
			if(f.containsEvent(event) == true){
				bList.add(f);
//System.out.println(f.getUrl());
			}
		}
		Collections.sort(bList);		
		return bList;
	}

	private DataFile extract_file(ArrayList bList, long begin) throws IOException
	{
		DataFile file = null;
		for(int i = 0; i < bList.size(); i++){
			DataFile f = (DataFile) bList.get(i);
			if(f.containsDateTime(begin) == true){
				file = f;
				break;
			}
		}
		return file;
	}
	private ArrayList extract_cList(ArrayList bList, long begin, long term) throws IOException
	{
SimpleDateFormat dtf = new SimpleDateFormat("yyyy/MM/dd HH:mm:ss");
		if(bList == null || bList.size() == 0){
			return null;
		}
		TreeSet set = new TreeSet();
//System.out.println("CLIST: begin: " + dtf.format(new Date(begin)) + " term: " + term);
		for(int i = 1; i < bList.size(); i++){
//System.out.println("***** index = " + i);
			DataFile f1 = (DataFile) bList.get(i - 1);
			DataFile f2 = (DataFile) bList.get(i);
			long b1 = f1.getBeginDateTime();
			long b2 = f2.getBeginDateTime();
//System.out.println("  f1: " + f1.getUrl() + " b1: " + dtf.format(new Date(b1)));
//System.out.println("  f2: " + f2.getUrl() + " b2: " + dtf.format(new Date(b2)));
			if(begin <= b1 && b1 <= (begin + term)){
//System.out.println("  Rule 1 matched, add " + f1.getUrl());
				set.add(f1);
			}else if(b1 <= begin && begin < b2){
//System.out.println("  Rule 2 matched, add " + f1.getUrl());
				set.add(f1);
			}else{
				// Nothing to do.
			}
		}
		// 最後のファイルだけ特別。
		DataFile f = (DataFile) bList.get(bList.size() - 1);
		long b = f.getBeginDateTime();
//System.out.println("  f: " + f.getUrl() + " b: " + b + " " + dtf.format(new Date(b)));
		if(b<= (begin + term)){
//System.out.println("  Special rule matched.");
			set.add(f);
		}
//System.out.println(set);
		ArrayList cList = new ArrayList();
		cList.addAll(set);
		return cList;
	}
	
//	private ArrayList extract_cList(ArrayList bList, long begin, long term) throws IOException
//	{
//		if(bList == null || bList.size() == 0){
//			return null;
//		}
//		TreeSet set = new TreeSet();
//System.out.println("CLIST:");
//		for(int i = 1; i < bList.size(); i++){
//			DataFile f1 = (DataFile) bList.get(i - 1);
//			DataFile f2 = (DataFile) bList.get(i);
//			long b1 = f1.getBeginDateTime();
//			long b2 = f2.getBeginDateTime();
//			if(begin <= b1 && b1 <= (begin + term)){
//				set.add(f1);
//System.out.println(f1.getUrl());
//			}
//			if(b1 <= begin && begin <= b2){
//				set.add(f1);
//System.out.println(f1.getUrl());
//			}
//			if(begin <= b2 && b2 <= (begin + term)){
//				set.add(f2);
//System.out.println(f2.getUrl());
//			}else{
//				// Nothing to do.
//			}
//		}
//		ArrayList cList = new ArrayList();
//		cList.addAll(set);
//		return cList;
//	}
	public ArrayList convertDateDataPairListToRawDataElements(ArrayList l)
	{
		ArrayList list = new ArrayList();
		for(int i = 0; i <= l.size()-2; i += 2){
			Date date = (Date) l.get(i);
			DataElement de = (DataElement) l.get(i + 1);
			RawDataElement re = new RawDataElement();
			re.setTime(date.getTime());
			re.setValue(de.getValue());
			if(de.getFlag(DataElement.VALID_FLAG) == true && de.getFlag(DataElement.SUCCESS_FLAG) == true){
				re.setValid(true);
			}else{
				// Nothing to do.
			}
			list.add(re);
		}
		return list;
	}
	

	public ArrayList convertDateDataPairListToRawDataElements(ArrayList l, boolean diffmode)
	{
		ArrayList list = new ArrayList();
		if(diffmode == false){
			return convertDateDataPairListToRawDataElements(l);
		}
		if(l.size() <= 2){
			RawDataElement re = new RawDataElement();
			re.setValid(false);	
			re.setValue(0);
			list.add(re);
			return list;
		}
		
		Date prevDate = null;
		DataElement prevDataElement = null;
		int offset;
		for(offset=0; offset <= l.size()-4; offset += 2){
			prevDataElement = (DataElement) l.get(offset+1);
			if(prevDataElement.getFlag(DataElement.VALID_FLAG) && prevDataElement.getFlag(DataElement.SUCCESS_FLAG)){
				prevDate = (Date) l.get(offset);
				break;
			}
		}
		if(offset > l.size()-4){
			return null;
		}
		
		for(int i = 2+offset; i <= l.size()-2; i += 2){
			Date date = (Date) l.get(i);
			DataElement de = (DataElement) l.get(i + 1);
			RawDataElement re = new RawDataElement();
			re.setTime(date.getTime());
			
			if(de.getFlag(DataElement.VALID_FLAG) == false || de.getFlag(DataElement.SUCCESS_FLAG) == false){
				re.setValue(0);
				re.setValid(false);
			} else {
				long during = date.getTime() - prevDate.getTime();
				long value = de.getValue() - prevDataElement.getValue();
				if(value < 0){
					value = COUNTMAX + value;  // MAX - prev + now
				}
				value = (long) Math.rint((double)(value * 1000) / (double)during); // per second (v / (d / 1000));
				if(value >= 0){	
					re.setValid(true);
					re.setValue(value);
				} else {
//System.out.println("invalid value: " + value + "(during="+during+")");
					re.setValue(0);
					re.setValid(false);
				}
				prevDate = date;
				prevDataElement = de;
			}
			list.add(re);
		}
		return list;
	}
	
	public ArrayList convertDateDataPairListToRawDataElementsByUptime(ArrayList l, ArrayList uptimeList)
	{
		if(l.size() != uptimeList.size()){
//System.out.println("l != uptime: " + l.size() + ", " + uptimeList.size());
			return null;
		}
		
		ArrayList list = new ArrayList();
		if(l.size() <= 2){
			RawDataElement re = new RawDataElement();
			re.setValid(false);	
			re.setValue(0);
			list.add(re);
			return list;
		}
		
		Date prevDate = null;
		DataElement prevDataElement = null;
		DataElement prevUptimeElement = null;
		int offset;
		for(offset=0; offset <= l.size()-4; offset += 2){
			prevDataElement = (DataElement) l.get(offset+1);
			prevUptimeElement = (DataElement) uptimeList.get(offset+1);
			
			if(prevDataElement.getFlag(DataElement.VALID_FLAG)
				&& prevDataElement.getFlag(DataElement.SUCCESS_FLAG)
				&& prevUptimeElement.getFlag(DataElement.VALID_FLAG)
				&& prevUptimeElement.getFlag(DataElement.SUCCESS_FLAG)){
				prevDate = (Date) l.get(offset);
				break;
			}
		}
		if(offset > l.size()-4){
			return null;
		}
		
		for(int i = 2+offset; i <= l.size()-2; i += 2){
			Date date = (Date) l.get(i);
			DataElement de = (DataElement) l.get(i + 1);
			DataElement uptime = (DataElement) uptimeList.get(i + 1);

			RawDataElement re = new RawDataElement();
			re.setTime(date.getTime());
			if(de.getFlag(DataElement.VALID_FLAG) == false || de.getFlag(DataElement.SUCCESS_FLAG) == false
				|| uptime.getFlag(DataElement.VALID_FLAG) == false || uptime.getFlag(DataElement.SUCCESS_FLAG) == false){
				re.setValue(0);
				re.setValid(false);
			} else {
				long during = uptime.getValue() - prevUptimeElement.getValue();
				long value = de.getValue() - prevDataElement.getValue();
				if(value < 0){
					value = COUNTMAX + value;  // MAX - prev + now
				}
				value = (long) Math.rint((double)(value * 100) / (double)during); // per second (v / (d / 1000));
				if(value >= 0){
					re.setValid(true);
					re.setValue(value);
					prevDate = date;
				} else {
//System.out.println("invalid value: " + value + " (during:"+during+"=" + uptime.getValue() + "-" + prevUptimeElement.getValue() +")");
					re.setValue(0);
					re.setValid(false);
				}
				prevDataElement = de;
				prevUptimeElement = uptime;
			}
			list.add(re);
		}
		return list;
	}
	
	private final int JUST_BEFORE = 0;
	private final int ESTIMATE = 1;
	
	private static long savedBeginTime = -1;
	public void initResampingMode(){
		savedBeginTime = -1;
	}
	
	private ArrayList resampleDateDataPair(ArrayList l, long resampleResolution, int mode){
		long resample = resampleResolution * 1000;
		ArrayList list = new ArrayList();
		Date prevDate = (Date) l.get(0);
		DataElement prevDataElement = (DataElement) l.get(1);
		long term = ((Date)l.get(l.size()-2)).getTime() - prevDate.getTime();
		long newPointNum = term/resample;
		long newBegin = prevDate.getTime() + term%resample;
		//long newBegin = prevDate.getTime();
		
		if(savedBeginTime - resample > newBegin){ // expand Time Range
			savedBeginTime = -1;
		}
		if(savedBeginTime > 0){
			long newBeginTmp = savedBeginTime;
			while(newBegin > newBeginTmp){    // shrink Time Range
				newBeginTmp = newBeginTmp + resample;
			}
			newBegin = newBeginTmp;
		} 
		
//System.out.println("resample: " + term +" "+ newPointNum +" "+ newBegin +" "+term%resample+" "+savedBeginTime);
		savedBeginTime = newBegin;

		int j = 0;
		
		if(mode == JUST_BEFORE){
			for(int i = 0; i <= newPointNum; i++){
				long currentTime = newBegin + i*resample;
				Date newTime = new Date(currentTime);
//System.out.println(currentTime);
				list.add(newTime);
				DataElement de = null;
				DataElement prev = null;
				while(j <= l.size()-4) {
					prev = (DataElement) l.get(j + 1);
					long nextTime = ((Date)l.get(j+2)).getTime();
					if(currentTime < nextTime){
						de = prev;
						break;
					} else if(currentTime == nextTime){
						de = (DataElement) l.get(j + 3);
						break;
					}
					j += 2;
				}
				if(j > l.size()-4){
					break;
				}
				if(de == null){
					de = new DataElement((byte)0x00, 0);
				}
				list.add(de);
//	  System.out.println("---");
			}
		} else { // Interpolate
//System.out.println("===== Interpolate =====");
			
			for(int i = 0; i <= newPointNum; i++){
//System.out.println("--- new point ---");
				long currentTime = newBegin + i*resample;
				Date newTime = new Date(currentTime);
				list.add(newTime);
				DataElement de = null;
				DataElement prev = null;
				
				while(j <= l.size()-4) {
					long prevTime = ((Date) l.get(j)).getTime();
					prev = (DataElement) l.get(j + 1);
					long nextTime = ((Date)l.get(j + 2)).getTime();
					long nextVal = ((DataElement) l.get(j + 3)).getValue();
//System.out.println("prevTime " + prevTime + ", prevVal " + prev.getValue() + ", nextTime " + nextTime + ", nextVal " + nextVal);
					if(currentTime < nextTime){
						long valDiff = nextVal - prev.getValue();
						long timeDiff = nextTime - prevTime;
						long increase = (long) Math.rint((double) valDiff
										* ((double) (currentTime - prevTime) / (double) timeDiff));
//System.out.println("valDiff " + valDiff + ", timeDiff " + timeDiff + ", zoukaritu " + increase);
						long value = prev.getValue() + increase;
						de = new DataElement(prev.getFlagByte(), value);
						break;
					} else if(currentTime == nextTime){
						de = (DataElement) l.get(j + 3);
						break;
					}
					j += 2;
				}
				if(j > l.size()-4){
					break;
				}
				if(de == null){
					de = new DataElement((byte)0x00, 0);
				}
//System.out.print("time " + currentTime + ", value " + de.getValue());				
				list.add(de);
//	  System.out.println("---");
			}
		}
		return list;
	}

	// Just Before
	public ArrayList resampleBeforeDateDataPairListToRawDataElements(ArrayList l, long resampleInterval)
	{
		return resampleDateDataPair(l, resampleInterval, JUST_BEFORE);
	}
	
	// Estimate
	public ArrayList resampleEstimateDateDataPairListToRawDataElements(ArrayList l, long resampleInterval)
	{
		return resampleDateDataPair(l, resampleInterval, ESTIMATE);
	}
	
	public ArrayList getMeasurementData(String host, String event, long begin, long term)
	throws IOException
	{
		ArrayList a_list = extract_aList(host);
		ArrayList b_list = extract_bList(a_list, event);
		ArrayList c_list = extract_cList(b_list, begin, term);   // target file list
		ArrayList data = new ArrayList();
		if(c_list == null || c_list.size() == 0){
			throw new IOException("File not matched, no measurement data.");
		}

		// 1. seek starting point
		int startingFileIndex = -1;
		long db_idx = 0;

		for(int i = 0; i < c_list.size(); i++){
			if(startingFileIndex >= 0){
				// found
				break;
			}
			DataFile file = (DataFile) c_list.get(i);
			db_idx = 0;
			SecondMetaBlock smb = file.getSecondMetaBlock();
			HostDefBlock hdb = smb.getHostDefBlock();
			int hidx = hdb.getHostIndex(host);
			OIDDefBlock odb = smb.getOIDDefBlock();
			int oidx = odb.getOIDIndex(event);
			DataBlockGroupTable tbl = smb.getDataBlockGroupTable();
			ArrayList dbgeList = (ArrayList) tbl.getDataBlockGroupElements();
			int dbgCount = dbgeList.size();
			while(true){
				if(file.readDataBlock(db_idx) == null){  // end
					break;
				}
				
				long dbg_idx = db_idx % dbgCount;
				DataBlockGroupElement row[] = (DataBlockGroupElement[]) dbgeList.get((int)dbg_idx);
				int columns = -1;
				for(int j = 0; j < row.length; j++){
					boolean matched = row[j].isPairOfHIDandOID(hidx, oidx);
					if(matched == true){
						columns = j;
						break;
					}
				}
				
				if(columns >= 0){
					DataBlock db = file.readDataBlock(db_idx);
					if(db == null){
						// nexe file
						break;
					}
					if(db.getTime() >= begin){
						// found
						startingFileIndex = i;
						break;
					}
				}
				db_idx++;
			}
		}
		if(startingFileIndex < 0){
			throw new IOException("File not mathed, no starting point.");
		}

		// 2. set a data from a data block
		long dbidx = db_idx;
		long endingTime = begin + term;
		for(int i = startingFileIndex; i < c_list.size(); i++){
			DataFile file = (DataFile) c_list.get(i);
			SecondMetaBlock smb = file.getSecondMetaBlock();
			HostDefBlock hdb = smb.getHostDefBlock();
			int hidx = hdb.getHostIndex(host);
			OIDDefBlock odb = smb.getOIDDefBlock();
			int oidx = odb.getOIDIndex(event);
			DataBlockGroupTable tbl = smb.getDataBlockGroupTable();
			ArrayList rows = (ArrayList) tbl.getDataBlockGroupElements();
			int dbgCount = rows.size();
			
			while(true){
				if(file.readDataBlock(dbidx) == null){  // end
					break;
				}
				int dbg_idx = (int) (dbidx % dbgCount);
				DataBlockGroupElement[] row = (DataBlockGroupElement[]) rows.get(dbg_idx);
				
				int columns = -1;
				for(int j = 0; j < row.length; j++){
					boolean matched = row[j].isPairOfHIDandOID(hidx, oidx);
					if(matched == true){
						columns = j;
						break;
					}
				}
//System.out.println("dbidx:" + dbidx + ", columns:" + columns + ", dbg_idx:" + dbg_idx + ", i:" + i);

				// found
				if(columns >= 0){  
					DataBlock db = file.readDataBlock(dbidx);
					if(db == null){
						// next file
//System.out.println("file termination.");
						break;
					}
					
					long dt = db.getTime();
					ArrayList dbdata = db.getData();
					DataElement de = (DataElement) dbdata.get(columns);
					data.add(new Date(dt));
//System.out.println(gmonitor.app.SimpleGrapherApp.dateString(dt));
					data.add(de);

					if(dt > endingTime){
//System.out.println("time exceeded, finish.");
						break;
					}
				}else{
//System.out.println("data not exists in this datablock.");
				}
				dbidx++;
			}

			// next file: reset
			dbidx = 0;
		}
		return data;
	}

//	public ArrayList getMeasurementDataXXX(String host, String event, long begin, long term)
//	throws IOException
//	{
//		// ファイルから計測データを読み込む
//		ArrayList a_list = extract_aList(host);
//		ArrayList b_list = extract_bList(a_list, event);
//		DataFile file = extract_file(b_list, begin);
//		int file_idx = b_list.indexOf(file); 
//		ArrayList result = new ArrayList();
//		if(file == null){
//			// 該当するファイルがなかったので、計測データがない
//System.out.println("No suitable file in DataTimeSpace, no measurement data.");
//			return result;
//		}
//
//		SecondMetaBlock smb = file.getSecondMetaBlock();
//		HostDefBlock hdb = smb.getHostDefBlock();
//		int hidx = hdb.getHostIndex(host);
//		OIDDefBlock odb = smb.getOIDDefBlock();
//		int oidx = odb.getOIDIndex(event);
//		DataBlockGroupTable tbl = smb.getDataBlockGroupTable();
//		long dbg_idx = file.getDataBlockGroupIndex(begin);
//		
//		while(file != null){
//			DataBlockGroup dbg = file.getDataBlockGroup(dbg_idx);
//			ArrayList date_data_list = dbg.pickData(tbl, hidx, oidx);
//			if(date_data_list.size() == 0){
//				// 一切データが取れなかったので、ここでギブアップする。
//				break;
//			}
//			
//			Date date = null;
//			DataElement elm = null;
//			for(int i = 0; i < date_data_list.size(); i+= 2){
//				date = (Date) date_data_list.get(i);
//				elm  = (DataElement) date_data_list.get(i + 1);
//				result.add(date);
//				result.add(elm);
//				if(date.getTime() >= (begin + term)){
//					// 要求された時間範囲をすべて取ったので、ここで終了する。
//					file = null;
//					break;
//				}
//			}
//			if(date.getTime() < (begin + term)){
//				// まだ要求された時間範囲をすべて取りきっていないので、続行を試みる。
//				// まず、「次の」データブロックグループを選択してみる。
//				dbg_idx++;
//				if(dbg_idx >= file.getLength()){
//					// 「次の」データブロックグループは、このファイルには存在しないので、「次の」ファイルを
//					// 試行する。
//					file_idx++;
//					if(file_idx >= b_list.size()){
//						// 「次の」ファイルは存在しないので、これ以上読めない
//						file = null;
//						break;
//						// ここで終了する。
//					}
//					file = (DataFile) b_list.get(file_idx);
//					// ファイルを切り替えたので、最初のデータブロックグループを読むべき
//					dbg_idx = 0;
//				}
//			}
//		} // while(file != null);
//
//		return result;
//	}

/**
 * @return
 */
public long getLargestGroupInterval() {
	return largestGroupInterval;
}

/**
 * @return
 */
public long getSmallestGroupInterval() {
	return smallestGroupInterval;
}

}
