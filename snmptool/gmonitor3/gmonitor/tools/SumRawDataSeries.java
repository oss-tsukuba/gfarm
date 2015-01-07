/*
 * Created on 2003/07/29
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.tools;

import gmonitor.gui.RawData;
import gmonitor.gui.RawDataElement;

import java.util.ArrayList;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class SumRawDataSeries {

	public RawDataElement[] total(RawData[] rda)
	{
		RawData newData = new RawData();
		ArrayList newList = new ArrayList();
		long time = 0;
		int series = rda.length;

		RawDataElement[] totalBuffer = new RawDataElement[series];
		int[] scanIndex = new int[series];
		long endTime[] = new long[series];
		fillEndTime(endTime, rda);

		while(true){
			// totalBuffer を更新して戻る。
			time = scanNext(time, rda, scanIndex, totalBuffer);
	
			// 合算バッファを合計し、新たな要素とする
			long v = addup(totalBuffer);
			RawDataElement ne = new RawDataElement();
			ne.setTime(time);
			ne.setValue(v);
			newList.add(ne);
	
			// 終了条件？
			if(isFinishedTime(time, endTime) == true){
				break;
			}
		}
		
		RawDataElement[] rde = new RawDataElement[newList.size()];
		rde = (RawDataElement[]) newList.toArray(rde);
		return rde;
	}

	private long scanNext(long time, RawData[] rda, int[] si, RawDataElement[] tb)
	{
		int series = rda.length;

		// すべての系列について、
		long t = Long.MAX_VALUE;
		for(int s = 0; s < series; s++){
			RawDataElement[] rde = rda[s].getData();
			// time より大きい要素のインデクスを探す。
			int idx = findFirstExceededTime(time, rde);
			// その中で、最古のタイムスタンプの要素を「マッチした」とする
			if(idx < 0){
				// この系列では見つからなかった。
			}else{
				// とりあえず覚えておく。
				si[s] = idx;
				long tt = rde[idx].getTime();
				if(t < tt){
					t = tt;
				}
			}
		}

		// マッチした要素をtbに覚える。複数マッチした場合にはすべて覚える。
		for(int s = 0; s < series; s++){
			RawDataElement[] rde = rda[s].getData();
			if(rde[si[s]].getTime() == t){
				// 合算バッファに蓄えるべき。
				tb[s] = rde[si[s]];
			}
		}
		return t;
	}

	/**
	 * @param time
	 * @param rde
	 * @return
	 */
	private int findFirstExceededTime(long time, RawDataElement[] rde) {
		for(int i = 0; i < rde.length; i++){
			if(rde[i].isValid() == true){
				if(rde[i].getTime() > time){
					return i;
				}
			}
		}
		return -1;
	}

	private boolean isFinishedTime(long t, long[] endTime)
	{
		for(int i = 0; i < endTime.length; i++){
			if(t < endTime[i]){
				return false;
			}
		}
		// t >= endTime[all of];
		return true;
	}

	private void fillEndTime(long[] endTime, RawData[] rda)
	{
		for(int i = 0; i < rda.length; i++){
			RawDataElement[] rde = rda[i].getData();
			endTime[i] = rde[rde.length + 1].getTime();
		}
		return;
	}
	
	private long addup(RawDataElement[] rde)
	{
		long s = 0;
		for(int i = 0; i < rde.length; i++){
			s += rde[i].getValue();
		}
		return s;
	}


}
