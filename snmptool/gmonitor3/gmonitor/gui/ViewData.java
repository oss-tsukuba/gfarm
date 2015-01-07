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
public class ViewData {
	
	/**
	 * この描画データを描画対象としてよいかどうか
	 */
	private boolean valid = false;
	
	/**
	 * この描画データを画面上で小さな点(プロット)として描画すべきかどうか
	 */
	private boolean plot = false;
	
	/**
	 * この描画データのプロットを描画すべき色
	 */
	private Color plotColor = Color.GREEN;
	
	/**
	 * この描画データから垂直下向きに塗りつぶしを行うべきかどうか(棒グラフのように見える)
	 */
	private boolean fill = false;
	
	/**
	 * この描画データを垂直下向きに塗りつぶすべき色 
	 */
	private Color fillColor = Color.RED;
	
	/**
	 * この描画データと隣接する有効な描画データとを線分で連結して描画すべきかどうか
	 */
	private boolean join = false;
	
	/**
	 * 描画データを連結する線分を描画すべき色
	 */
	private Color joinColor = Color.YELLOW;
	
	/**
	 * 最新データを示す水平線(レベルメータ)を描画すべきかどうか
	 */
	private boolean level = false;
	
	/**
	 * レベルメータを描画すべき色
	 */
	private Color levelColor = Color.BLUE;
	
	/**
	 * 描画データ要素の配列
	 */
	private ViewDataElement[] data;
	
	/**
	 * プロットの半径(ピクセル単位)
	 */
	private int plotRadius = 2;


	/**
	 * @return
	 */
	public ViewDataElement[] getData() {
		return data;
	}

	/**
	 * @return
	 */
	public boolean isFill() {
		return fill;
	}

	/**
	 * @return
	 */
	public Color getFillColor() {
		return fillColor;
	}

	/**
	 * @return
	 */
	public boolean isJoin() {
		return join;
	}

	/**
	 * @return
	 */
	public Color getJoinColor() {
		return joinColor;
	}

	/**
	 * @return
	 */
	public boolean isLevel() {
		return level;
	}

	/**
	 * @return
	 */
	public Color getLevelColor() {
		return levelColor;
	}

	/**
	 * @return
	 */
	public boolean isPlot() {
		return plot;
	}

	/**
	 * @return
	 */
	public Color getPlotColor() {
		return plotColor;
	}

	/**
	 * @return
	 */
	public boolean isValid() {
		return valid;
	}

	/**
	 * @param elements
	 */
	public void setData(ViewDataElement[] elements) {
		data = elements;
	}

	/**
	 * @param b
	 */
	public void setFill(boolean b) {
		fill = b;
	}

	/**
	 * @param color
	 */
	public void setFillColor(Color color) {
		fillColor = color;
	}

	/**
	 * @param b
	 */
	public void setJoin(boolean b) {
		join = b;
	}

	/**
	 * @param color
	 */
	public void setJoinColor(Color color) {
		joinColor = color;
	}

	/**
	 * @param b
	 */
	public void setLevel(boolean b) {
		level = b;
	}

	/**
	 * @param color
	 */
	public void setLevelColor(Color color) {
		levelColor = color;
	}

	/**
	 * @param b
	 */
	public void setPlot(boolean b) {
		plot = b;
	}

	/**
	 * @param color
	 */
	public void setPlotColor(Color color) {
		plotColor = color;
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
	public int getPlotRadius() {
		return plotRadius;
	}

	/**
	 * @param i
	 */
	public void setPlotRadius(int i) {
		plotRadius = i;
	}

}
