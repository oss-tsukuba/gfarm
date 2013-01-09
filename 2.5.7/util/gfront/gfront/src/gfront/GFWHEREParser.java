package gfront;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.StringTokenizer;

/*
[hkondo@gfm01 glogger]$ /usr/local/gfarm/bin/gfwhere gfarm:/tatebe/echo
i386-intel-linux-aist: gfm02.apgrid.org gfm01.apgrid.org gfm03.apgrid.org gfm04
.apgrid.org gfm17.apgrid.org gfm18.apgrid.org gfm19.apgrid.org

[hkondo@gfm01 glogger]$ /usr/local/gfarm/bin/gfwhere gfarm:/tatebe/thput-gfpio
i386-intel-linux-aist: gfm02.apgrid.org gfm03.apgrid.org gfm17.apgrid.org gfm18
.apgrid.org gfm01.apgrid.org gfm04.apgrid.org gfm19.apgrid.org

[hkondo@gfm01 glogger]$ /usr/local/gfarm/bin/gfwhere gfarm:/tatebe/test.file1
0: gfm01.apgrid.org gfm17.apgrid.org
*/

public class GFWHEREParser extends Thread {
	public static final String DEFAULT_GFWHERE = "gfwhere";
	public static final String ATTRIB_INDEX = "ATTRIB_INDEX";
	public static final String ATTRIB_NODES = "ATTRIB_NODES";

	String _gfwhere; // gfwhere command to use.
	String gfarm_file; // gfarm file to analyze.
	HashMap[] attribs; // parsed data of gfarm_path;
	int all_fragment_count = 0;
	int fragment_count = 0;

	public GFWHEREParser() {
		_gfwhere = DEFAULT_GFWHERE;
		gfarm_file = null;
		attribs = null;
	}


	public String getPATH() {
		return new String(gfarm_file);
	}

	public void setPATH(String p) throws IllegalStateException {
		if (attribs != null) {
			// already parsed.
			throw new IllegalStateException("Already parsed parser.");
		}
		gfarm_file = new String(p);
	}

	public void setCommand(String cmd) throws IllegalStateException {
		if (attribs != null) {
			// already parsed.
			throw new IllegalStateException("Already parsed parser.");
		}
		_gfwhere = new String(cmd);
	}
	
	protected CommandOutput _spawn_gfwhere_and_capture_output() {
		Runtime r = Runtime.getRuntime();
		String[] cmds = { _gfwhere, gfarm_file };
		Process p = null;
		try {
			p = r.exec(cmds);
		} catch (IOException ioe) {
			// cannot spawn gfwhere.
			return null;
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
		out.output = (String[]) (l.toArray(out.output));

		return out;
	}

	protected HashMap _chop_and_eat(String s) {
		HashMap nm = new HashMap();
		StringTokenizer st = new StringTokenizer(s, " ");

		// try extract file attributes.
		if (st.hasMoreTokens() == false) {
			// Invalid output, no tokens.
			return null;
		}
		String str_idx = (String) (st.nextToken());
		str_idx = str_idx.substring(0, str_idx.length() - 1);
		nm.put(ATTRIB_INDEX, str_idx);

		if (st.hasMoreTokens() == false) {
			// No filesystem node is.
			return null;
		}
		ArrayList node_list = new ArrayList();
		while (st.hasMoreTokens()) {
			String n = (String) (st.nextToken());
			node_list.add(n);
			all_fragment_count++;
		}
		nm.put(ATTRIB_NODES, node_list);
		return nm;
	}

	boolean retval;

//	public boolean parse() throws IllegalStateException {
	public void run() throws IllegalStateException {
		retval = false;
		
		if (attribs != null) {
			// already parsed.
			throw new IllegalStateException("Already parsed parser.");
		}
		if (gfarm_file == null) {
			throw new IllegalStateException("No path is set.");
		}
		CommandOutput o = _spawn_gfwhere_and_capture_output();
		if (o == null) {
			// spawning gfwhere is failed.
			//return false;
			retval = false;
			return;
		}
		if (o.retcode != 0) {
			// failure.
			//return false;
			retval = false;
			return;
		}
		// success.
		int sz = o.output.length;
		attribs = new HashMap[sz];
		for (int i = 0; i < sz; i++) {
			HashMap nm = _chop_and_eat(o.output[i]);
			if (nm == null) {
				// Invalid format in output.
				//return false;
				
			}
			attribs[i] = nm;
		}
		fragment_count = sz;
		//return true;
		retval = true;
		return;
	}

	public int getFragmentCount() {
		return fragment_count;
	}

	public int getAllFragmentCount() {
		return all_fragment_count;
	}

	public int getSize() {
		if (attribs == null) {
			// not parsed.
			return -1;
		}
		return attribs.length;
	}

	public ArrayList getNodeList(int i) {
		if (attribs == null) {
			// not parsed.
			return null;
		}
		return (ArrayList) (attribs[i].get(ATTRIB_NODES));
	}
	public String getARCH(int i) {
		if (attribs == null) {
			// not parsed.
			return null;
		}
		return (String) (attribs[i].get(ATTRIB_INDEX));
	}
	public int getFragmentIndex(int i) throws NumberFormatException {
		if (attribs == null) {
			// not parsed.
			throw new NumberFormatException("Not parsed.");
		}
		return Integer.parseInt((String) (attribs[i].get(ATTRIB_INDEX)));
	}
}
