package gmonitor.logdata;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;

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
public class DataFile implements Comparable {
	private String url;
	private SeekableFile file;
	private FirstMetaBlock  fmb;
	private SecondMetaBlock smb;
	private long latestDateTime = -1;
	private long size;
	private int dbgroup_length; // ファイル中に含まれるデータブロックグループの個数
	long datablock_pos;
	private static final int CAPACITY = 100;
	private static HashMap instanceMap = new HashMap();
	private static ArrayList instanceGeneration = new ArrayList();

	public static DataFile getInstance(String url) throws IOException {
		DataFile f = null;
		synchronized(instanceMap){
			Object o = instanceMap.get(url);
			if(o == null){
				// first accessing to url
				f = new DataFile(url);
				instanceMap.put(url, f);
				instanceGeneration.add(url);
				if(instanceGeneration.size() > CAPACITY){
					// 同時にオープンしておくファイル数を超過したのでクローズする
					String u = (String) instanceGeneration.remove(0);
					DataFile df = (DataFile) instanceMap.remove(u);
					df.close();
				}
			}else{
				f = (DataFile) o;
				int idx = instanceGeneration.indexOf(url);
				// アクセスされたので世代をリフレッシュ
				instanceGeneration.remove(idx);
				instanceGeneration.add(url);
				
				DataFile df = (DataFile) instanceMap.remove(url);
				df.close();
				f = new DataFile(url);
				instanceMap.put(url, f);
			}
		}
		return f;
	}
	/**
	 * 
	 */
	public void close() throws IOException {
		file.close();
	}
	public void reload() throws IOException
	{
//System.out.println("Reloaded file attr: " + getUrl());
		initialize();
	}
	protected void initialize() throws IOException
	{
		file.seek(0);

		byte[] sz = new byte[2];
		file.read(sz, 0, 2);
		int size = UTY.byte2int(sz);
		
		byte[] fmbBytes = new byte[size];
		file.read(fmbBytes, 0, size);
		InputStream is = new ByteArrayInputStream(fmbBytes, 0, size);
		fmb = (FirstMetaBlock)FirstMetaBlock.newInstance(is, size);
		is.close();
		
		byte[]dsz = new byte[4];
		file.read(dsz, 0, 4);
		size = UTY.byte2int(dsz);
		byte[] smbBytes = new byte[size];
		file.read(smbBytes, 0, size);
		is = new ByteArrayInputStream(smbBytes, 0, size);
		smb = (SecondMetaBlock)SecondMetaBlock.newInstance(is, size);
		is.close();
		this.size = file.size();

		// ファイルサイズから、含まれるであろうデータブロックグループの個数を算出し、dbgroup_lengthに覚える。
		DataBlockGroupTable t = smb.getDataBlockGroupTable();
		ArrayList l = t.getDataBlockGroupElements();
		datablock_pos = fmb.getSize() + smb.getSize() + 2 + 4; // 2 + 4 means size fields.
		dbgroup_length = (int) ((this.size - datablock_pos) / fmb.getDataBlockGroupSize());
		latestDateTime = -1L;
		
		file.seek(datablock_pos);
	}
	protected DataFile(String url) throws IOException{
		this.url = url;
		SeekableFile f = SeekableFileFactory.create(url);
//		file = new CachedSeekableFile(f); // TODO: CachedSeekableFile
file = f;
		initialize();
	}

	/**
	 * ファイルに含まれている完全なデータブロックグループの個数を返す
	 * @return
	 */
	public long getLength()
	{
		return dbgroup_length;
	}
	
	/**
	 * @return
	 */
	public FirstMetaBlock getFirstMetaBlock() {
		return fmb;
	}
	/**
	 * @return
	 */
	public SecondMetaBlock getSecondMetaBlock() {
		return smb;
	}

	public DataBlockGroup getDataBlockGroup(long idx) throws IOException
	{
		int i = 0;
		DataBlockGroup dbg = new DataBlockGroup();
		int szGroup = fmb.getDataBlockGroupSize();
		byte[] buf = new byte[szGroup];

		// 1.読み出すべき位置まで seek する
		long pos = datablock_pos + (idx * szGroup);
		file.seek(pos);
		
		// 2.read する
		int ret = file.read(buf, 0, szGroup);
		if(ret < 0){
			// ファイルの終端を越えた。
			throw new IOException("File pointer exceeded.");
		}
		
		// 3.parse する
		DataBlockGroupTable dbgt = smb.getDataBlockGroupTable();
		ArrayList rows = dbgt.getDataBlockGroupElements();
		for(int r = 0; r < rows.size(); r++){
			DataBlockGroupElement[] row = (DataBlockGroupElement[]) rows.get(r);
			if(row.length > 0){
				int dbsz = (4 + 4) + 5 * row.length;
				DataBlock db = new DataBlock(buf, i, row.length);
				dbg.add(db);
				i += dbsz;
			}else{
				DataBlock db = new DataBlock();
				dbg.add(db);
			}
		}
		
		return dbg;
	}

	/**
	 * このデータファイルにおけるもっとも古い計測値の時刻を取得する
	 * @return 時刻
	 */
	public long getBeginDateTime()
	{
		return fmb.getBeginDate();
	}

	/**
	 * このデータファイル中のもっとも新しい計測値の時刻を取得する
	 * @return 時刻
	 */
	public long getLatestDateTime() throws IOException
	{
		if(latestDateTime > 0){
			// Already computed.
			return latestDateTime;
		}
		long latest = fmb.getBeginDate();
		// 1.get the latest datablock group.
		ArrayList dbg = getDataBlockGroup(dbgroup_length - 1);

		// 2.scan datablock group to determine the latest time point.
		for(int i = 0; i < dbg.size(); i++){
			DataBlock db = (DataBlock) dbg.get(i);
			if(db.isValid() == true){
				long t = db.getTime();
				if(t > latest){
					latest = t;
				}
			}else{
				// db is invalid, skip it.
			}
		}
		
		// 3.cache latest time and return.
		latestDateTime = latest;
		return latest;
	}
	
	/**
	 * このデータファイル中に、指定されたホストが含まれているかどうかをテストする
	 * @param host 検査するホスト名
	 * @return 含まれていれば true.
	 */
	public boolean containsHost(String host)
	{
		return smb.containsHost(host);
	}
	
	/**
	 * このデータファイル中に、指定されたイベントが含まれているかどうかをテストする
	 * @param event 検査するイベント名
	 * @return 含まれていれば true.
	 */
	public boolean containsEvent(String event) {
		return smb.containsEvent(event);
	}

	/**
	 * このデータファイル中に、指定された時刻が含まれているかどうかをテストする
	 * ただし、「まさにちょうどその時刻」が含まれるかどうかは問題にしない。
	 * @param t 検査する時刻
	 * @return 含まれていれば true.
	 */
	public boolean containsDateTime(long t) throws IOException {
		boolean ret = false;
		long b = getBeginDateTime();
		long e = getLatestDateTime();
		if(b <= t && t <=e){
			ret = true;
		}
		return ret;
	}
	
	/**
	 * とある時刻がどのデータブロックグループに含まれているかを計算する
	 * @param t テストしたい時刻
	 * @return データブロックグループのインデクス
	 */
	public long getDataBlockGroupIndex(long t) throws IOException
	{
		long idx = -1;
		if(containsDateTime(t) == false){
			// このファイルの範囲外の時刻が指定されているのでエラー
			throw new IOException("TimePointOutOfRangeInFile " + getUrl() + " " + t);
		}
		long intv = fmb.getGroupInterval();
		long begin = getBeginDateTime();
		idx = (long) (t - begin) / (long) intv;
		return idx;		
	}

	/* (non-Javadoc)
	 * @see java.lang.Comparable#compareTo(java.lang.Object)
	 */
	public int compareTo(Object o) {
		if((o instanceof DataFile) == false){
			// クラスが違うので比較できない。
			throw new ClassCastException("Object is not compatible.");
		}
		DataFile f = (DataFile) o;
		long t1 = getBeginDateTime();
		long t2 = f.getBeginDateTime();
		return (int) (t1 - t2);
	}
	/**
	 * @return
	 */
	public String getUrl() {
		return url;
	}

	public DataBlock readDataBlock(long idx) throws IOException
	{
		// 通し番号で idx のデータブロックを取得する。idx は 0　から始まる。
		DataBlockGroupTable tbl = smb.getDataBlockGroupTable();
		long blockCount = tbl.getDataBlockGroupElements().size();
		long dbg_idx = idx / blockCount;
		DataBlockGroup dbg = null;
		try {
			dbg = getDataBlockGroup(dbg_idx);
		} catch (IOException e) {
			// 該当するデータブロックグループは存在しない。
			return null;
		}
		int db_idx = (int) (idx % blockCount);
		if(db_idx >= dbg.size()){
			// 該当するデータブロックは存在しない。
			return null;
		}
		DataBlock db = (DataBlock) dbg.get(db_idx);
		return db;
	}
}
