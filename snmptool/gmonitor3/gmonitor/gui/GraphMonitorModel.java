package gmonitor.gui;
import java.util.ArrayList;

/*
 * Created on 2003/06/04
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
public class GraphMonitorModel {

	/**
	 * 計測データの系列(内部には RawData 型のみを許容すること。RawData[] として扱う)
	 * 複数系列のサポートのため。
	 */
	private ArrayList rawdata;
	
	/**
	 * 合算モードで動作すべきかどうかのフラグ
	 */
	private boolean sumMode = false;

	/**
	 * 表示されるべき時間範囲の開始日時(Java時間ミリ秒単位)
	 */
	private long begin;

	/**
	 * 表示されるべき時間幅(Java時間ミリ秒単位)
	 */
	private long term;

	/**
	 * 接頭辞の数大きさ(K, M, G, T, ... の倍率。たとえばKの場合1000とする)
	 * @return
	 */
	private long prefixFactor = 1;

	/**
	 * 計測値系列群中の最大値(系列群の中での最大値)
	 */
	private long maxValue;
	private long maxValueTime;
	
	private long minValue;
	private long minValueTime;
	
	private long latestValue;
	private long latestValueTime;

	private double avgValue;

	/**
	 * 計測値系列群中のグラフ縦軸の最大値(10, 100, 1000, 10000, ...)
	 * @return
	 */
	private long topValue;

	/**
	 * @return
	 */
	public long getBegin() {
		return begin;
	}

	/**
	 * @return
	 */
	public ArrayList getRawDataSeries() {
		return rawdata;
	}

	/**
	 * @return
	 */
	public boolean isSumMode() {
		return sumMode;
	}

	/**
	 * @return
	 */
	public long getTerm() {
		return term;
	}

	/**
	 * @param l
	 */
	public void setBegin(long l) {
		begin = l;
	}
	
	private long findMaxValue(RawDataElement[] rde)
	{
		long max = 0;
		for(int i = 0; i < rde.length; i++){
			if((rde[i].getValue() > max) && rde[i].isValid()) {
				max = rde[i].getValue();
				maxValueTime = rde[i].getTime();
			}
		}
		return max;
	}
	private long findMinValue(RawDataElement[] rde) {
		long min = -1;
		for(int i = 0; i < rde.length; i++){
			if((min == -1 || rde[i].getValue() < min) && rde[i].isValid()){
				min = rde[i].getValue();
				minValueTime = rde[i].getTime();
			}
		}
		return min;
	}
	private double calcAverage(RawDataElement[] rde) {
		double avg = 0;
		int invalidCount = 0;
		for(double i = 0; i < rde.length; i++){
			//avg = avg * (i/(i+1)) + (double)rde[(int)i].getValue() / (i+1) ;
			if(rde[(int)i].isValid() == false){
				invalidCount++;
			} else {
				avg = (avg * (i-invalidCount) + (double)rde[(int)i].getValue()) / (i+1-invalidCount) ;
			}
//System.out.print(avg + " ");
		}
		return avg;
	}

	/* 5, 10, 50, 100, 500, 1000 */
	private long findTopValue(long v)
	{
		long top = 1L;
		while(top < v){
			top *= 5L;
			if(top >= v){
				break;
			}
			top *= 2L;
		}
		return top;
	}
	/**
	 * @param list
	 */
	public void setRawDataSeries(ArrayList list, long magnification) {
		// セット
		rawdata = list;

		// TopValue, MaxValue を計算して覚えておく。
		long tTmp = 0;
		long maxTmp = 0;
		long minTmp = -1;
		double avgTmp = 0;
//System.out.println("list.size(): " + list.size());
		for(int i = 0; i < list.size(); i++){
			RawData rd = (RawData) list.get(i);
			RawDataElement[] rde = rd.getData();
			
			if(rde.length <= 0){
				continue;
			}
			
			long maxv = findMaxValue(rde);
			if(maxTmp < maxv) maxTmp = maxv;	

			long tv = findTopValue(maxTmp);
			if(tTmp < tv) tTmp = tv;

			long minv = findMinValue(rde);
			if(minTmp == -1 || minTmp > minv) minTmp = minv;
			
			avgTmp = calcAverage(rde);
			latestValue = rde[rde.length-1].getValue();
			latestValueTime = rde[rde.length-1].getTime();
			begin = rde[0].getTime();  // for fit graph and diff mode
		}
//System.out.println("rde[0].getTime() " + begin);
		term = latestValueTime - begin;
		
		topValue = tTmp / magnification;
		maxValue = maxTmp;
		minValue = minTmp;
		avgValue = avgTmp;
	}

	/**
	 * @param b
	 */
	public void setSumMode(boolean b) {
		sumMode = b;
	}

	/**
	 * @param l
	 */
	public void setTerm(long l) {
		term = l;
	}

	/**
	 * @return
	 */
	public long getPrefixFactor() {
		return prefixFactor;
	}

	/**
	 * @param l
	 */
	public void setPrefixFactor(long l) {
		prefixFactor = l;
	}

	/**
	 * @return
	 */
	public long getTopValue() {
		return topValue;
	}

	/**
	 * @return
	 */
	public long getMaxValue() {
		return maxValue;
	}
	public long getMaxValueTime() {
		return maxValueTime;
	}

	public long getMinValue() {
		return minValue;
	}
	public long getMinValueTime() {
		return minValueTime;
	}

	public long getLatestValue() {
		return latestValue;
	}
	public long getLatestValueTime() {
		return latestValueTime;
	}

	public double getAvgValue() {
		return avgValue;
	}
}
