package gfront;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;


public class GfhostList implements ActionListener {
	String[] list;
	GFront gfront;
	
	GfhostList(GFront _gfront){
		list = null;
		gfront = _gfront;
	}

	public void runGfhost(){
		Runtime r = Runtime.getRuntime();
		String[] cmds = { "gfhost", "-l"};
		Process p = null;
		try {
			p = r.exec(cmds);
		} catch (IOException ioe) {
			list = null;
			return;
		}
		int ret = -1;
		ArrayList l = new ArrayList();
		try {
			InputStream is = p.getInputStream();
			BufferedReader br = new BufferedReader(new InputStreamReader(is));
			while (true) {
				String ln = br.readLine();
				if (ln == null) {
					// End of stream.
					break;
				}
				l.add(ln);
			}
		} catch (IOException ioe) {
			// End of stream, pass through.
		}
		try {
			ret = p.waitFor();
		} catch (InterruptedException ie) {
			// Interrupted waiting, but nothing to do...
		}
		int sz = l.size();
		CommandOutput out = new CommandOutput(sz, ret);
		list = out.output = (String[]) (l.toArray(out.output));

		return;
	}
	
	public String toString(){
		StringBuffer str = new StringBuffer();
		for(int i=0; i < list.length; i++){
			str.append(list[i] + System.getProperty("line.separator"));
		}
		return str.toString();
	}

	public void actionPerformed(ActionEvent ae) {
		runGfhost();
		if(list != null){
			GFrontCommon gfc = new GFrontCommon();
			gfc.showTextArea(gfront,"gfhost", toString(), 30, 60);
		}
	}

}
