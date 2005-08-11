package gfront;

import java.awt.Dimension;
import java.awt.DisplayMode;
import java.awt.GraphicsEnvironment;

public class GFrontApp {
	public static void main(String args[]) {
		GFront f = new GFront();
		f.initial_setup();
		//f.pack();

		try {
			Dimension d = f.getSize();
			DisplayMode dm = GraphicsEnvironment.getLocalGraphicsEnvironment().getDefaultScreenDevice().getDisplayMode();
			f.setLocation(dm.getWidth()/2 - d.width/2, dm.getHeight()/2 - d.height/2);
		} catch(Exception e){
		}

		f.show();
	}
}
