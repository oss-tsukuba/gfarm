package gmonitor.gui;
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
public class RawDataElement {
	
	/**
	 * この要素が示す計測ポイントの計測値が有効かどうか
	 */
	private boolean valid = false;
	
	/**
	 * この要素が示す計測ポイントの計測値
	 * valid が false であった場合は、意味のある値として参照してはならない
	 */
	private long value = 0L;
	
	/**
	 * この要素が示す計測ポイントの時刻
	 * valid がいかなる値であっても、時刻は正しいものとして参照してよい
	 */
	private long time = 0L;

	/**
	 * @return
	 */
	public boolean isValid() {
		return valid;
	}

	/**
	 * @return
	 */
	public long getValue() {
		return value;
	}

	/**
	 * @param b
	 */
	public void setValid(boolean b) {
		valid = b;
	}

	/**
	 * @param l
	 */
	public void setValue(long l) {
		value = l;
	}

	/**
	 * @return
	 */
	public long getTime() {
		return time;
	}

	/**
	 * @param l
	 */
	public void setTime(long l) {
		time = l;
	}

}
