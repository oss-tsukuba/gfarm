package gmonitor.gui;
import java.awt.Color;
import java.awt.Font;
import java.awt.FontMetrics;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.Rectangle;
import java.awt.event.ComponentEvent;
import java.awt.event.ComponentListener;
import java.awt.geom.Rectangle2D;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;

import javax.swing.JPanel;

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
public class GraphMonitor extends JPanel implements ComponentListener {
	
	private boolean stylePlot = true;
	private boolean styleJoin = true;
	private boolean styleFill = true;

	/**
	 * グラフ格子の描画色
	 */
	private Color gridColor = Color.GRAY;

	/**
	 * グラフの背景色
	 */
	private Color backColor = Color.WHITE;

	/**
	 * グラフのラベルの色
	 */
	private Color labelColor = Color.BLACK;

	/**
	 * グラフのタイトル
	 */
	private String title = "";
		
	/**
	 * Y 軸の最大値(グラフ縦軸の値域の最大値)
	 */
	private long maxValue = Long.MAX_VALUE;

	/**
	 * X 軸方向の刻み幅(ピクセル単位)
	 */
	private int unitX = 2;

	/**
	 * Y 軸方向の刻み幅(ピクセル単位)
	 */
	private int unitY = 2;

	/**
	 * X 軸方向の単位ラベル
	 */
	private String[] unitLabelX;
	
	/**
	 * Y 軸方向の単位ラベル
	 */
	private String[] unitLabelY;

	/**
	 * グリッドの縦線の横方向のオフセット(スクロール用)
	 */
	private int offsetX;

	/**
	 * X 軸のラベル
	 */
	private String axisLabelX = "";

	/**
	 * Y 軸のラベル
	 */
	private String axisLabelY = "";

	/**
	 * Y 軸の値の単位(プレフィクスも含む)
	 */
	private String valueUnit = "";

	/**
	 * 描画データの系列(内部には ViewData 型のみを許容すること)
	 */
	private ArrayList viewdata = new ArrayList();

	/**
	 * このグラフ表示コンポーネントについて、計測データを保持している「データモデル」を参照する
	 */
	private GraphMonitorModel model;

	/**
	 * Y 軸ラベルの最大幅(ピクセル単位)
	 */
	private int y_label_width_max;
	private int x_label_width_max;
	
	/**
	 * グラフ表示部分の幅(ピクセル単位)
	 */
	private int grid_width;
	
	/**
	 * グラフ表示部分の高さ(ピクセル単位)
	 */
	private int grid_height;

	private DateFormat dateFormat = new SimpleDateFormat("yyyy/MM/dd HH:mm:ss");

	public GraphMonitor(){
		super();
		addComponentListener(this);
	}

	/**
	 * 再描画
	 * @see java.awt.Component#paint(java.awt.Graphics)
	 */
	public void paint(Graphics g) {
		Rectangle r = getBounds();
		g.clearRect(0, 0, r.width, r.height);
		if(model == null){
			// データモデルが設定されていないので、その旨を描画する
			draw_nodata(g);
			return;
		}
		synchronized(model){
			recalc();
			// デコレーションを生成更新する
			createDecoration();
			// デコレーションも含めて座標を計算しなおす。
			recalc();
			// グラフ本体の描画
			draw_graph(g);
			// デコレーション(グラフ格子, 各種ラベル, グラフのタイトル)の描画
			draw_decoration(g);
//System.out.println("Model.begin: " + dateFormat.format(new Date(model.getBegin())));
//System.out.println("Model.term : " + model.getTerm());
		}
	}

	/**
	 * グラフ本体を描画する
	 * @param g 再描画に用いる Graphics
	 */
	private void draw_graph(Graphics g) {
		ArrayList l = viewdata;
		if(l == null){
			return;
		}
		int sz = l.size();
		Graphics org_g = g;

		FontMetrics fm = getFontMetrics(getFont());
		ViewData vd;
		int fh = fm.getHeight(); 
		int grid_y_origin = 0 + fh + grid_height;
		int grid_x_origin = 0 + fh + y_label_width_max;
//		int grid_x_origin = 0 + fh + y_label_width_max + 1;
//      int grid_x_origin = 0 + fh + x_label_width_max / 2;

		for(int i = 0; i < sz; i++){
			g = org_g.create();
			vd = (ViewData)l.get(i);
			if(vd == null || vd.isValid() != true){
				// 系列iは無効な描画データなので、描画しないでスキップ
				//break;
				continue;
			}
			ViewDataElement[] vde = vd.getData();

			g.setClip(grid_x_origin, grid_y_origin - grid_height, grid_width, grid_height);
			int previous_point_x = grid_x_origin;
			int previous_point_y = 0;
			boolean previous_point_valid = false;
			int y = grid_y_origin;
			if(vde != null){
				Color fillColor = vd.getFillColor();
				Color joinColor = vd.getJoinColor();
//System.out.println("vde len:" + vde.length);
				for(int j = 0; j < vde.length; j++){
					if(vde[j].isValid() != true){
						// 無効なポイントなので描画しない
						// 直前ポイントも無効にしておく
						previous_point_valid = false;
						//break;
						continue;
					}
					int x = vde[j].getPos() + grid_x_origin;
					int h = vde[j].getHeight();
					if(vd.isFill() == true){
						// 棒の塗りつぶし
						int lefthalf = 0;
						if(previous_point_valid == true){
							lefthalf = (x - previous_point_x) / 2; // 中値
						}else{
							lefthalf = 0;
						}
						int nextIdx = j + 1;
						if(nextIdx >= vde.length){
							nextIdx = j;
						}
						int righthalf= (int) Math.ceil((vde[nextIdx].getPos() + grid_x_origin - x) / 2.0); // 中値
						int w = lefthalf + righthalf;
						g.setColor(fillColor);
						g.fillRect(x - lefthalf, y - h, w, h);
					}
					if(vd.isJoin() == true){
						// line
						if(previous_point_valid == true){
							// 連結する
							g.setColor(joinColor);
							g.drawLine(previous_point_x, previous_point_y, x, y - h);
							g.drawLine(previous_point_x, previous_point_y-1, x, y - h-1); // bold
						}else{
							// 連結しない
						}
					}
					previous_point_x = x;
					previous_point_y = y - h;
					previous_point_valid = true;
				}

				if(vd.isPlot() == true){
					// 頂点の描画を行う
					int r = vd.getPlotRadius();
					int r2 = r*2;
					Color vdColor = vd.getPlotColor();
					Color vdColorD = vdColor.darker();
					Color error = Color.ORANGE; // error color
					Color errorD = error.darker();
					for(int j = 0; j < vde.length; j++){
						int x = vde[j].getPos() + grid_x_origin;
						int h = vde[j].getHeight();
						if(vde[j].isValid() != true){
							g.setColor(error);  
							g.fillOval(x - r, grid_y_origin - r, r2, r2); 
							g.setColor(errorD);	
							g.drawOval(x - r, grid_y_origin - r, r2, r2); 
						} else {
							g.setColor(vdColor);
							g.fillOval(x - r, y - h - r, r2, r2); 
							g.setColor(vdColorD);	
							g.drawOval(x - r, y - h - r, r2, r2);
						}
					}
				}
				
				if(vd.isLevel() == true){
				//if(true){
					// 最終要素(最新要素)の値を明示するためのバーを描画する
					if(vde.length == 0){
						break;
					}
					int h = vde[vde.length - 1].getHeight();
					g.setColor(vd.getLevelColor());
					g.drawLine(grid_x_origin, grid_y_origin - h,
								grid_x_origin + grid_width - fh, grid_y_origin - h);
					g.drawLine(grid_x_origin, grid_y_origin - h - 1,
								grid_x_origin + grid_width - fh, grid_y_origin - h - 1);
				}
			}else{
//System.out.println("Null view data.");
			}
		}
	}

	/**
	 * データモデルが設定されていないことを描画する
	 * @param g 再描画に用いる Graphics
	 */
	private void draw_nodata(Graphics g) {
		FontMetrics fm = g.getFontMetrics();
		int fh = fm.getHeight();
		g.drawString("No data model.", 0, fh);
	}

	/**
	 * コンポーネントがresizeされたとき等に呼ばれ、各種座標を再計算する
	 */
	private void recalc(){
//		y_label_width_max = 0;
//		Rectangle rect = getBounds();
//		Font f = getFont();
//		Graphics g = getGraphics();
//		FontMetrics fm = getFontMetrics(f);
//		int fh = fm.getHeight();
//
//		// Y軸ラベル群の最大幅を求める
//		String[] l = getUnitLabelY();
//		if (l != null) {
//			for (int i = 0; i < l.length; i++) {
//				Rectangle2D fr2 = fm.getStringBounds(l[i], g);
//				Rectangle fr = fr2.getBounds();
//				if (fr.width > y_label_width_max) {
//					y_label_width_max = fr.width;
//				}
//			}
//		}
//
//		// ラベルの幅が決まったので、グラフ描画エリアのサイズを決定できる
//		grid_width = rect.width - y_label_width_max - fh - fh;
//		grid_height = rect.height - fh - fh - fh - fh;
	}

	/**
	 * デコレーションの描画
	 * @param g 描画に用いる Graphics
	 */
	private void draw_decoration(Graphics g) {
		FontMetrics fm = getFontMetrics(getFont());
		int fh = fm.getHeight();
		int ystep = getUnitY();
		int xstep = getUnitX();
		Rectangle rect = getBounds();
		int ascent = fm.getAscent();
//		int descent= fm.getDescent();
		int descent= 0;  // draw_axis_x_label() -> 

		int offset = getOffsetX();

		/* draw_title() */ {
			g.setColor(getLabelColor());
			String t = getTitle();
			Rectangle2D fr2 = fm.getStringBounds(t, g);
			Rectangle fr = fr2.getBounds();
			int x = rect.width / 2 - fr.width / 2;
			int y = ascent;
			g.drawString(t, x, y);
		}

		/* draw_axis_y_label() */{
			g.setColor(getLabelColor());
			Graphics2D gc = (Graphics2D) g.create();

			gc.setColor(getLabelColor());
			String t = getAxisLabelY();
			Rectangle2D fr2 = fm.getStringBounds(t, g);
			Rectangle fr = fr2.getBounds();
			int x = 0 + ascent;
//			int y = (rect.height - fh) / 2 + (fr.width / 2);
			int y = (rect.height - fh - fh - fh - fh) / 2 + (fr.width / 2);
			gc.translate(x, y);
			gc.rotate(-1.0 * (Math.PI / 2.0));
			gc.drawString(t, 0.0f, 0.0f);
		}

		/* draw_unit_y_label() */{
			g.setColor(getLabelColor());
			int x = 0 + fh;
			int i = 0;
			// drawing y value label.
			String[] ylabel = getUnitLabelY();
			if(ylabel == null){
				ylabel = new String[]{"0",
				};
			}
			for(int y = fh + grid_height; y >= (0 + fh); y -= ystep){
				String lbl = ylabel[i];
				if(lbl == null){
					lbl = "";
				}
				Rectangle2D fr2 = fm.getStringBounds(lbl, g);
				Rectangle fr = fr2.getBounds();
				g.clearRect(x, y - fh, fr.width, fh);
//				g.drawString(lbl, x, y - descent);
				g.drawString(lbl, x + (y_label_width_max - fr.width), y - descent);
				i++;
				if(i >= ylabel.length){
					break;					
				}
			}
		}

		/* draw_axis_x_label() */
		/*
		{
			g.setColor(getLabelColor());
			String t = getAxisLabelX();
			Rectangle2D fr2 = fm.getStringBounds(t, g);
			Rectangle fr = fr2.getBounds();
			int x = rect.width / 2 - fr.width / 2;
			int y = rect.height - descent;
			g.drawString(t, x, y);
		}
		*/

		/* draw_unit_x_label() */{
			g.setColor(getLabelColor());
			int y = 0 + fh + grid_height;
			int i = 0;
			String[] xlabel = getUnitLabelX();
			if(xlabel == null){
				xlabel = new String[]{"0",
				};
			}
			for(int x = 0 + y_label_width_max + fh + offset; x <= grid_width + y_label_width_max + fh; x += xstep){
				String lbl = xlabel[i];
				if(lbl == null){
					lbl = "";
				}
				String[] row = lbl.split("¥n", 2);
				for(int j = 0; j < row.length; j++){
					Rectangle2D fr2 = fm.getStringBounds(row[j], g);
					Rectangle fr = fr2.getBounds();
					int cx = x - (fr.width / 2);
//					g.clearRect(cx, y + 1, fr.width, fh);
					g.drawString(row[j], cx, y + ascent + fh * j);
				}
				i++;
				if(i >= xlabel.length){
					break;
				}
			}
		}

		/* draw_grid() */{
			// set a color.
			g.setColor(getGridColor());
			// draw vertical lines.
			for(int x = 0 + y_label_width_max + fh + offset; x <= grid_width + y_label_width_max + fh + offset; x += xstep){
				g.drawLine(x, (0 + fh), x, (fh + grid_height));
			}
			// draw horizontal lines.
			for(int y = fh + grid_height; y >= (0 + fh); y -= ystep){
				g.drawLine((0 + y_label_width_max + fh), y, (y_label_width_max + grid_width + fh), y);
			}
		}

	}
//	private void draw_decoration_XXX(Graphics g) {
//		FontMetrics fm = getFontMetrics(getFont());
//		int fh = fm.getHeight();
//		int ystep = getUnitY();
//		int xstep = getUnitX();
//		Rectangle rect = getBounds();
//		int ascent = fm.getAscent();
//		int descent= fm.getDescent();
//		int offset = getOffsetX();
//
//		/* draw_title() */ {
//			g.setColor(getLabelColor());
//			String t = getTitle();
//			Rectangle2D fr2 = fm.getStringBounds(t, g);
//			Rectangle fr = fr2.getBounds();
//			int x = rect.width / 2 - fr.width / 2;
//			int y = ascent;
//			g.drawString(t, x, y);
//		}
//
//		/* draw_axis_y_label() */{
//			g.setColor(getLabelColor());
//			Graphics2D gc = (Graphics2D) g.create();
//
//			gc.setColor(getLabelColor());
//			String t = getAxisLabelY();
//			Rectangle2D fr2 = fm.getStringBounds(t, g);
//			Rectangle fr = fr2.getBounds();
//			int x = 0 + ascent;
//			int y = (rect.height - fh) / 2 + (fr.width / 2);
//			gc.translate(x, y);
//			gc.rotate(-1.0 * (Math.PI / 2.0));
//			gc.drawString(t, 0.0f, 0.0f);
//		}
//
//		/* draw_unit_y_label() */{
//			g.setColor(getLabelColor());
//			int x = 0 + fh;
//			int i = 0;
//			// drawing y value label.
//			String[] ylabel = getUnitLabelY();
//			if(ylabel == null){
//				ylabel = new String[]{"0",
//				};
//			}
//			for(int y = fh + grid_height; y >= (0 + fh); y -= ystep){
//				String lbl = ylabel[i];
//				if(lbl == null){
//					lbl = "";
//				}
//				Rectangle2D fr2 = fm.getStringBounds(lbl, g);
//				Rectangle fr = fr2.getBounds();
//				g.clearRect(x, y - fh, fr.width, fh);
//				g.drawString(lbl, x, y - descent);
//				i++;
//				if(i >= ylabel.length){
//					break;					
//				}
//			}
//		}
//
//		/* draw_axis_x_label() */{
//			g.setColor(getLabelColor());
//			String t = getAxisLabelX();
//			Rectangle2D fr2 = fm.getStringBounds(t, g);
//			Rectangle fr = fr2.getBounds();
//			int x = rect.width / 2 - fr.width / 2;
//			int y = rect.height - descent;
//			g.drawString(t, x, y);
//		}
//
//		/* draw_unit_x_label() */{
//			g.setColor(getLabelColor());
//			int y = 0 + fh + grid_height;
//			int i = 0;
//			String[] xlabel = getUnitLabelX();
//			if(xlabel == null){
//				xlabel = new String[]{"0",
//				};
//			}
//			for(int x = 0 + y_label_width_max + fh + offset; x < grid_width + y_label_width_max; x += xstep){
//				String lbl = xlabel[i];
//				if(lbl == null){
//					lbl = "";
//				}
//				Rectangle2D fr2 = fm.getStringBounds(lbl, g);
//				Rectangle fr = fr2.getBounds();
//				int cx = x - (fr.width / 2);
//				g.clearRect(cx, y + 1, fr.width, fh);
//				g.drawString(lbl, cx, y + ascent);
//				i++;
//				if(i >= xlabel.length){
//					break;
//				}
//			}
//		}
//
//		/* draw_grid() */{
//			// set a color.
//			g.setColor(getGridColor());
//			// draw vertical lines.
//			for(int x = 0 + y_label_width_max + fh + offset; x < grid_width + y_label_width_max; x += xstep){
//				g.drawLine(x, (0 + fh), x, (fh + grid_height));
//			}
//			// draw horizontal lines.
//			for(int y = fh + grid_height; y >= (0 + fh); y -= ystep){
//				g.drawLine((0 + y_label_width_max + fh), y, (y_label_width_max + grid_width + fh), y);
//			}
//		}
//
//	}

	/* (non-Javadoc)
	 * @see java.awt.event.ComponentListener#componentHidden(java.awt.event.ComponentEvent)
	 */
	public void componentHidden(ComponentEvent arg0) {
		// Nothing to do.
	}

	/* (non-Javadoc)
	 * @see java.awt.event.ComponentListener#componentMoved(java.awt.event.ComponentEvent)
	 */
	public void componentMoved(ComponentEvent arg0) {
		// Nothing to do.
	}

	/* (non-Javadoc)
	 * @see java.awt.event.ComponentListener#componentShown(java.awt.event.ComponentEvent)
	 */
	public void componentShown(ComponentEvent arg0) {
		// Nothing to do.
	}

	/* (non-Javadoc)
	 * @see java.awt.event.ComponentListener#componentResized(java.awt.event.ComponentEvent)
	 */
	public void componentResized(ComponentEvent evt) {
		if(model == null){
			return;
		}
		synchronized(model){
			recalc();				// リサイズされたのでまず座標を更新する。
			createDecoration();     // デコレーションを更新する。
			recalc();               // デコレーションも含めて座標を計算しなおす。
			createViewData(); 		// 描画データを更新する。
		}
	}

protected String[] createYLabels(int div, long min, long max, long prefixFactor)
{
	String[] ret = new String[div + 1];
	long tick = (max - min) / prefixFactor / div;
//System.out.println("tick " + tick);	
	if(tick == 0){
		tick = 1;
	}
	for(int i = 0; i < div + 1; i++){
		ret[i] = Long.toString(min + tick * i);
	}
	return ret;
}
protected String[] createXLabels(int div, long begin, long term)
{
	String[] ret = new String[div + 1];
	long tick = term / div;
	Date d = new Date(begin);
	for(int i = 0; i < div + 1; i++){
		long t = begin + tick * i;
		d.setTime(t);
		ret[i] = dateFormat.format(d);
		ret[i] = ret[i].replace(' ', '\n');
	}
	return ret;
}
private int calc_division(int[] divs, int pix, int thr)
{
	for(int i = 0; i < divs.length; i++){
		int allover = thr * divs[i];
		if(allover < pix){
			return divs[i];
		}
	}
	return divs[divs.length - 1];
}
private static final int[] X_DIVISION = new int[]{100, 50, 25, 10, 5, 2, 1, };
private static final int[] Y_DIVISION = new int[]{100, 50, 25, 10, 5, 2, 1, };
  
private void createDecoration()
{
	y_label_width_max = 0;
	Rectangle rect = getBounds();
	Font f = getFont();
	Graphics g = getGraphics();
	FontMetrics fm = getFontMetrics(f);
	int fh = fm.getHeight();

	int tmp_height = rect.height - fh - fh - fh - fh; 
	int y_div = calc_division(Y_DIVISION, tmp_height, 16);// 16 is adhoc
	//if((model.getTopValue() / model.getPrefixFactor()) <= 5){
//System.out.println("y_div,getTopValue,getPrefixFactor " + y_div +" "+ model.getTopValue() +" "+ model.getPrefixFactor());
	long ylabeltop = model.getTopValue() / model.getPrefixFactor();
//System.out.println("ylabeltop " + ylabeltop); 
	if(ylabeltop < y_div){
		y_div = (int)ylabeltop;
	}
	if(y_div <= 0){
		y_div = 1;
	}
	grid_height = (tmp_height / y_div) * y_div; // y_div の倍数に丸める 
	//String[] ylabels = createYLabels(y_div, 0, model.getTopValue() / model.getPrefixFactor());
	String[] ylabels = createYLabels(y_div, 0, model.getTopValue(), model.getPrefixFactor());
	setMaxValue(Long.parseLong(ylabels[ylabels.length-1]) * model.getPrefixFactor());
	
	setUnitLabelY(ylabels);  // Y 軸方向のラベル配列を設定する
	String[] l = getUnitLabelY();
	// Y軸ラベル群の最大幅を求める
/*
	if (l != null) {
		for (int i = 0; i < l.length; i++) {
			Rectangle2D fr2 = fm.getStringBounds(l[i], g);
			Rectangle fr = fr2.getBounds();
			if (fr.width > y_label_width_max) {
				y_label_width_max = fr.width;
			}
		}
	}
*/
	Rectangle2D fr2 = fm.getStringBounds("0000", g);
	Rectangle fr = fr2.getBounds();
	y_label_width_max = fr.width;
	
	
	//int[] fw = fm.getWidths();
	//x_label_width_max = 0;
	//l = getUnitLabelX();
	// X軸ラベル群の最大幅を求める
/*
	if (l != null) {
		for (int i = 0; i < l.length; i++) {
			Rectangle2D fr2 = fm.getStringBounds(l[i], 0, 10, g); // yyyy/mm/dd
			Rectangle fr = fr2.getBounds();
			if (fr.width > x_label_width_max) {
				x_label_width_max = fr.width;
			}
		}
	}
*/
	fr2 = fm.getStringBounds("0000/00/00", g);
	fr = fr2.getBounds();
	x_label_width_max = fr.width;
	
//System.out.println("x_label_width_max " + x_label_width_max);
	
	// ラベルの幅が決まったので、グラフ描画エリアのサイズを決定できる
	int tmp_width = rect.width - y_label_width_max - fh - x_label_width_max / 2;
	// 各軸の分割数を計算
	int x_div = calc_division(X_DIVISION, tmp_width, x_label_width_max + 10); // 10 is adhoc.
	grid_width = (tmp_width / x_div) * x_div; // x_div の倍数に丸める
	
//System.out.println("grid_width " + grid_width + ", x_label_width_max " + x_label_width_max + ", x_div " + x_div);
	
//	grid_width = 0; // x_div の倍数に決定;

	// 各軸分割時のピクセル幅を計算
	int x_unit = getGrid_width() / x_div;
	int y_unit = getGrid_height()/ y_div;
	setUnitX(x_unit);        // X 軸方向刻み幅を設定する
	setUnitY(y_unit);        // Y 軸方向刻み幅を設定する

	// 各軸の単位ラベルを生成
	String[] xlabels = createXLabels(x_div, model.getBegin(), model.getTerm());
	setUnitLabelX(xlabels);  // X 軸方向のラベル配列を設定する

}
//private void createDecoration()
//{
//	// 各軸の分割数を計算
//	int x_div = calc_division(X_DIVISION, getGrid_width(), 64); // 64 is adhoc.
//	int y_div = calc_division(Y_DIVISION, getGrid_height(), 16);// 16 is adhoc.
//
//	// 各軸分割時のピクセル幅を計算
//	int x_unit = getGrid_width() / x_div;
//	int y_unit = getGrid_height()/ y_div;
//	setUnitX(x_unit);        // X 軸方向刻み幅を設定する
//	setUnitY(y_unit);        // Y 軸方向刻み幅を設定する
//
//	// 各軸の単位ラベルを生成
//	String[] xlabels = createXLabels(x_div, model.getBegin(), model.getTerm());
//	String[] ylabels = createYLabels(y_div, 0, model.getTopValue() / model.getPrefixFactor());
//	setUnitLabelX(xlabels);  // X 軸方向のラベル配列を設定する
//	setUnitLabelY(ylabels);  // Y 軸方向のラベル配列を設定する
//}

	public void setStylePlot(boolean f)
	{
		stylePlot = f;
	}
	public void setStyleJoin(boolean f)
	{
		styleJoin = f;
	}
	public void setStyleFill(boolean f)
	{
		styleFill = f;
	}

	/**
	 * 現在の諸パラメータに従って描画データを新たに作り直す
	 */
	private void createViewData() {
		if(model == null){
			// 適切なモデルが設定されていないので描画データを更新できない
			return;
		}
		synchronized(model){
			ArrayList newView = new ArrayList();
			ArrayList rawDataList = model.getRawDataSeries();
			long begin_time_to_paint = model.getBegin();
			long term_to_paint = model.getTerm();

			for(int i = 0; i < rawDataList.size(); i++){
				RawData rd = (RawData) rawDataList.get(i);
				if(rd.isValid() == true){
					ViewDataElement[] vds = renderRawData(rd, begin_time_to_paint, term_to_paint);
					ViewData vd = new ViewData();
					vd.setData(vds);
					vd.setValid(true);
					vd.setPlot(stylePlot);
					vd.setJoin(styleJoin);
					vd.setFill(styleFill);
					vd.setPlotColor(rd.getPlotColor());
					vd.setFillColor(rd.getBarColor());
					vd.setJoinColor(rd.getLineColor());
					vd.setLevelColor(rd.getLevelColor());
					newView.add(vd);
				}
			}
			// 再構築後の描画データを設定する
			setViewdata(newView);
		}
	}

	/**
	 * 計測データをレンダリングして描画データを生成する
	 * @param rd レンダリングすべき計測データ
	 * @return
	 */
//	private ViewDataElement[] renderRawData(RawData rd, long begin, long term) {
//		ArrayList elmList = new ArrayList();
//		RawDataElement[] rds = rd.getData();
//		int first_idx = search_first_valid_element(rds);
//		int last_idx = search_last_valid_element(rds);
////		long begin = rd.getBegin();
//if(first_idx < 0 || last_idx < 0){
//	// No valid element.
//	return null;
//}
//System.out.println("Rendering RawData:");
//		long first = rds[first_idx].getTime();
//		long last = rds[last_idx].getTime();
////		long term = last - first;
//		for(int i = 0; i < rds.length; i++){
//			RawDataElement e = rds[i];
//			long t = e.getTime() - first;
//			long v = e.getValue();
//System.out.println(v);
//			int x = (int) ((t * grid_width) / term);      // e の対応するX座標	
//			int y = (int) ((v * grid_height) / maxValue); // e の対応するY座標
//			ViewDataElement ve = new ViewDataElement();
//			ve.setPos(x);
//			ve.setHeight(y);
//			if(e.isValid() == true){
//				ve.setValid(true);
//			}else{
//				ve.setValid(false);
//			}
//			elmList.add(ve);
//		}
//		ViewDataElement[] vda = new ViewDataElement[elmList.size()];
//		vda = (ViewDataElement[]) elmList.toArray(vda); 
//
//		return vda;
//	}
	private ViewDataElement[] renderRawData(RawData rd, long begin, long term) {
		ArrayList elmList = new ArrayList();
		RawDataElement[] rds = rd.getData();
		int first_idx = search_first_valid_element(rds);
		int last_idx = search_last_valid_element(rds);
		if(first_idx < 0 || last_idx < 0 || term <= 0 || maxValue <= 0){
			// No valid element.
			return null;
		}
//System.out.println("Rendering RawData:");
		long first = rds[first_idx].getTime();
		long last = rds[last_idx].getTime();
//SimpleDateFormat dtFormat = new SimpleDateFormat("yyyy/MM/dd HH:mm:ss");
//		int mute_offpix = (int) (((first - begin) * grid_width) / term);
		//int mute_offpix = (int) Math.rint(((double)(first - begin) * (double)(grid_width)) / (double)term);
//System.out.println("Mute_OffPix: " + mute_offpix);
		for(int i = 0; i < rds.length; i++){
			RawDataElement e = rds[i];
			long t = e.getTime() - first;
			long v = e.getValue();
//System.out.println("Time: " + dtFormat.format(new Date(e.getTime())) + " Value: " + v);
			int x = (int) ((t * grid_width) / term);      // e の対応するX座標	
			int y = (int) ((v * grid_height) / maxValue); // e の対応するY座標
			//x += mute_offpix;
			ViewDataElement ve = new ViewDataElement();
			ve.setPos(x);
			ve.setHeight(y);
			if(e.isValid() == true){
				ve.setValid(true);
			}else{
				ve.setValid(false);
			}
			elmList.add(ve);
		}
		ViewDataElement[] vda = new ViewDataElement[elmList.size()];
		vda = (ViewDataElement[]) elmList.toArray(vda); 

		return vda;
	}

	/**
	 * @param rds
	 * @return
	 */
	private int search_last_valid_element(RawDataElement[] rds) {
		int idx = -1;
		for(int i = rds.length - 1; i >= 0; i--){
			if(rds[i].isValid() == true){
				idx = i;
				break;
			}
		}
		return idx;
	}

	/**
	 * @param rds
	 * @return
	 */
	private int search_first_valid_element(RawDataElement[] rds) {
		int idx = -1;
		for(int i = 0; i < rds.length; i++){
			if(rds[i].isValid() == true){
				idx = i;
				break;
			}
		}
		return idx;
	}

	/**
	 * @return
	 */
	public String getAxisLabelX() {
		return axisLabelX;
	}

	/**
	 * @return
	 */
	public String getAxisLabelY() {
		return axisLabelY;
	}

	/**
	 * @return
	 */
	public Color getBackColor() {
		return backColor;
	}

	/**
	 * @return
	 */
	public int getGrid_height() {
		return grid_height;
	}

	/**
	 * @return
	 */
	public int getGrid_width() {
		return grid_width;
	}

	/**
	 * @return
	 */
	public Color getGridColor() {
		return gridColor;
	}

	/**
	 * @return
	 */
	public long getMaxValue() {
		return maxValue;
	}

	/**
	 * @return
	 */
	public int getOffsetX() {
		return offsetX;
	}

	/**
	 * @return
	 */
	public String getTitle() {
		return title;
	}

	/**
	 * @return
	 */
	public String[] getUnitLabelX() {
		return unitLabelX;
	}

	/**
	 * @return
	 */
	public String[] getUnitLabelY() {
		return unitLabelY;
	}

	/**
	 * @return
	 */
	public int getUnitX() {
		return unitX;
	}

	/**
	 * @return
	 */
	public int getUnitY() {
		return unitY;
	}

	/**
	 * @return
	 */
	public ArrayList getViewdata() {
		return viewdata;
	}

	/**
	 * @return
	 */
	public int getY_label_width_max() {
		return y_label_width_max;
	}

	/**
	 * @param string
	 */
	public void setAxisLabelX(String string) {
		axisLabelX = string;
	}

	/**
	 * @param string
	 */
	public void setAxisLabelY(String string) {
		axisLabelY = string;
	}

	/**
	 * @param color
	 */
	public void setBackColor(Color color) {
		backColor = color;
	}

	/**
	 * @param i
	 */
	public void setGrid_height(int i) {
		grid_height = i;
	}

	/**
	 * @param i
	 */
	public void setGrid_width(int i) {
		grid_width = i;
	}

	/**
	 * @param color
	 */
	public void setGridColor(Color color) {
		gridColor = color;
	}

	/**
	 * @param l
	 */
	public void setMaxValue(long l) {
		maxValue = l;
	}

	/**
	 * @param i
	 */
	public void setOffsetX(int i) {
		offsetX = i;
	}

	/**
	 * @param string
	 */
	public void setTitle(String string) {
		title = string;
	}

	/**
	 * @param strings
	 */
	public void setUnitLabelX(String[] strings) {
		unitLabelX = strings;
	}

	/**
	 * @param strings
	 */
	public void setUnitLabelY(String[] strings) {
		unitLabelY = strings;
	}

	/**
	 * @param i
	 */
	public void setUnitX(int i) {
		unitX = i;
	}

	/**
	 * @param i
	 */
	public void setUnitY(int i) {
		unitY = i;
	}

	/**
	 * @param list
	 */
	public void setViewdata(ArrayList list) {
		viewdata = list;
	}

	/**
	 * @param i
	 */
	public void setY_label_width_max(int i) {
		y_label_width_max = i;
	}

	/**
	 * @return MVCモデルにおけるGraphMonitorのモデル
	 */
	public GraphMonitorModel getModel() {
		return model;
	}

	/**
	 * @param model このGraphMonitorに描画させたいデータモデル
	 */
	public void setModel(GraphMonitorModel model) {
		this.model = model;
		if(model != null){
			setMaxValue(model.getTopValue());
			//recalc();
			createDecoration();
			//recalc();
			createViewData();
		}
	}

	/**
	 * @return
	 */
	public boolean isStyleFill() {
		return styleFill;
	}

	/**
	 * @return
	 */
	public boolean isStyleJoin() {
		return styleJoin;
	}

	/**
	 * @return
	 */
	public boolean isStylePlot() {
		return stylePlot;
	}

	/**
	 * @return
	 */
	public Color getLabelColor() {
		return labelColor;
	}

	/**
	 * @param color
	 */
	public void setLabelColor(Color color) {
		labelColor = color;
	}

	/**
	 * @return
	 */
	public String getValueUnit() {
		return valueUnit;
	}

	/**
	 * @param string
	 */
	public void setValueUnit(String string) {
		valueUnit = string;
	}

}
