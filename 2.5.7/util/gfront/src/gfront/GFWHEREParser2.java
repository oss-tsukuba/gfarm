package gfront;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.StringTokenizer;

public class GFWHEREParser2 {
	public static final String DEFAULT_GFWHERE = "gfwhere";
	public static final String ATTRIB_INDEX = "ATTRIB_INDEX";
	public static final String ATTRIB_NODES = "ATTRIB_NODES";

	String _gfwhere; // gfwhere command to use.
	String gfarm_file; // gfarm file to analyze.
	HashMap[] attribs; // parsed data of gfarm_path;
	ArrayList comargs;  // command arguments
	String[] outs;
	int all_fragment_count = 0;
	int fragment_count = 0;

	public GFWHEREParser2() {
		_gfwhere = DEFAULT_GFWHERE;
		gfarm_file = null;
		attribs = null;
		comargs = new ArrayList();
		comargs.add(_gfwhere);
		outs = null;
	}
	
	public String getPATH() {
		return new String(gfarm_file);
	}

	public void addPATH(String p) throws IllegalStateException {
		if (attribs != null) {
			// already parsed.
			throw new IllegalStateException("Already parsed parser.");
		}
		comargs.add(p);
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
		String[] cmds = new String[comargs.size()];
		cmds = (String[]) comargs.toArray(cmds);
		
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
				System.out.println("add: " + ln);
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

	public boolean parse() throws IllegalStateException {
		if (attribs != null) {
			// already parsed.
			throw new IllegalStateException("Already parsed parser.");
		}
		System.out.println("size:" + comargs.size());
		if (comargs.size() == 1) {
			throw new IllegalStateException("No path is set.");
		}
		CommandOutput o = _spawn_gfwhere_and_capture_output();
		if (o == null) {
			// spawning gfwhere is failed.
			return false;
		}
		if (o.retcode != 0) {
			// failure.
			return false;
		}
		outs = o.output;
		/*
		// success.
		int sz = o.output.length;
		attribs = new HashMap[sz];
		for (int i = 0; i < sz; i++) {
			HashMap nm = _chop_and_eat(o.output[i]);
			if (nm == null) {
				// Invalid format in output.
				return false;
			}
			attribs[i] = nm;
		}
		fragment_count = sz;
		*/
		return true;
	}

	public int getFragmentCount(int index) {
		if(outs == null){
			return -1;
		}
		int nodeindex = -1;
		int fragcount = 0;
		int found = 0;
		for(int i=0; i < outs.length; i++){
			if(outs[i].startsWith("gfarm:")){
				if(found == 1) {
					return fragcount;
				}
				nodeindex++;
				fragcount = 0;
			}
			else if(outs[i].equals("")){
				
			}
			else if(index == nodeindex || nodeindex == -1){
				fragcount++;
				System.out.println("seek: " + outs[i]);
				found = 1;
			}
		}
		return fragcount;
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
	
	public static void main(String args[]){
		GFWHEREParser2 p = new GFWHEREParser2();
		p.addPATH("gfarm:/3com-stat");
		p.addPATH("gfarm:/3com-stat.bin");
		p.addPATH("gfarm:/takuya/aaa");
		p.parse();
		System.out.println("fragment:" + p.getFragmentCount(2));
	}
}
