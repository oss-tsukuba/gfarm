package gfront;
import java.awt.event.ActionEvent;

import javax.swing.AbstractAction;

public class GFAction extends AbstractAction {
	GFront gf;
	public GFAction(String name, GFront gf) {
		super(name);
		this.gf = gf;
	}
	public void actionPerformed(ActionEvent a) {
		// Nothing to do.
	}
}
