package gmonitor.gui;

import java.awt.Color;

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
public class RawData {
	
	private Color plotColor = Color.GREEN;
	private Color lineColor = Color.BLUE;
	private Color barColor = Color.RED;
	private Color levelColor = Color.YELLOW;

	/**
	 * この計測データ系列のもっとも古いデータの日時(Java日時)
	 */
//	private long epoch; // XXXtodoXXX 要再考: ここにあるべきではないような気がする

	/**
	 * このオブジェクトが保持している計測データの最初の日時(Java日時)
	 */
//	private long begin;	// XXXtodoXXX 要再考: ここにあるべきではないような気がする
	
	/**
	 * このオブジェクトが保持している計測データの型(差分物として扱うかどうか)
	 */
	private boolean diffmode = false;

	/**
	 * この観測対象において、SNMP カウンター値の折り返しが発生する数値
	 */
	private long max = 4294967295L; // SNMP int32 max.

	/**
	 * この計測データ系列の対象ホスト名
	 */
//	private String host;

	/**
	 * この計測データ系列の対象事象名
	 */
//	private String event;
	
	/**
	 * この計測データ系列の計測間隔(in milli-seconds)
	 */
//	private long interval;
	
	/**
	 * この計測データのヒント名
	 */
//	private String hint;
	
	/**
	 * この計測データが処理対象として正しい状態にあるかどうか
	 */
	private boolean valid;
	
	/**
	 * 計測データそのもの
	 */
	private RawDataElement[] data;

	/**
	 * @return
	 */
//	public long getBegin() {
//		return begin;
//	}

	/**
	 * @return
	 */
	public RawDataElement[] getData() {
		return data;
	}

	/**
	 * @return
	 */
	public boolean isDiffmode() {
		return diffmode;
	}

	/**
	 * @return
	 */
//	public long getEpoch() {
//		return epoch;
//	}

	/**
	 * @return
	 */
//	public String getEvent() {
//		return event;
//	}

	/**
	 * @return
	 */
//	public String getHint() {
//		return hint;
//	}

	/**
	 * @return
	 */
//	public String getHost() {
//		return host;
//	}

	/**
	 * @return
	 */
//	public long getInterval() {
//		return interval;
//	}

	/**
	 * @return
	 */
	public long getMax() {
		return max;
	}

	/**
	 * @return
	 */
	public boolean isValid() {
		return valid;
	}

	/**
	 * @param l
	 */
//	public void setBegin(long l) {
//		begin = l;
//	}

	/**
	 * @param elements
	 */
	public void setData(RawDataElement[] elements) {
		data = elements;
	}

	/**
	 * @param b
	 */
	public void setDiffMode(boolean b) {
		diffmode = b;
	}

	/**
	 * @param l
	 */
//	public void setEpoch(long l) {
//		epoch = l;
//	}

	/**
	 * @param string
	 */
//	public void setEvent(String string) {
//		event = string;
//	}

	/**
	 * @param string
	 */
//	public void setHint(String string) {
//		hint = string;
//	}

	/**
	 * @param string
	 */
//	public void setHost(String string) {
//		host = string;
//	}

	/**
	 * @param l
	 */
//	public void setInterval(long l) {
//		interval = l;
//	}

	/**
	 * @param l
	 */
	public void setMax(long l) {
		max = l;
	}

	/**
	 * @param b
	 */
	public void setValid(boolean b) {
		valid = b;
	}

	/**
	 * @return
	 */
	public Color getBarColor() {
		return barColor;
	}

	/**
	 * @return
	 */
	public Color getLineColor() {
		return lineColor;
	}

	/**
	 * @return
	 */
	public Color getPlotColor() {
		return plotColor;
	}

	/**
	 * @param color
	 */
	public void setBarColor(Color color) {
		barColor = color;
	}

	/**
	 * @param color
	 */
	public void setLineColor(Color color) {
		lineColor = color;
	}

	/**
	 * @param color
	 */
	public void setPlotColor(Color color) {
		plotColor = color;
	}

	/**
	 * @return
	 */
	public Color getLevelColor() {
		return levelColor;
	}

	/**
	 * @param color
	 */
	public void setLevelColor(Color color) {
		levelColor = color;
	}

}
