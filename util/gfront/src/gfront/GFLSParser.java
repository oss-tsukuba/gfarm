package gfront;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.StringTokenizer;

/*
parse the following style output of gfls:
-rw-r--r-- tatebe   *             17885 Jan  1  1970 Event001.root
-rwxr-xr-x tatebe   *             11180 Nov  1 20:59 echo
-rw-rw-rw- tatebe   *                48 Jan  1  1970 host3
-rw-rw-r-- tatebe   *                92 Jan  1  1970 host4.indiana
-rwxrwxr-x tatebe   *            445420 Oct 31 20:53 test
-rw-r--r-- tatebe   *        1073741824 Jan  1  1970 test.file1
-rwxrwxr-x tatebe   *            365113 Jan  1  1970 thput-gfpio
*/

public class GFLSParser {
	public static final String DEFAULT_GFLS = "gfls";
	public static final String ATTRIB_ATTR = "ATTRIB_ATTR";
	public static final String ATTRIB_USER = "ATTRIB_USER";
	public static final String ATTRIB_GROUP = "ATTRIB_GROUP";
	public static final String ATTRIB_SIZE = "ATTRIB_SIZE";
	public static final String ATTRIB_MTIME_DATE = "ATTRIB_DATE";
	public static final String ATTRIB_MTIME_MONTH = "ATTRIB_MTIME_MONTH";
	public static final String ATTRIB_MTIME_DAY = "ATTRIB_MTIME_DAY";
	public static final String ATTRIB_MTIME_TIME = "ATTRIB_MTIME_TIME";
	public static final String ATTRIB_MTIME_YEAR = "ATTRIB_MTIME_YEAR";
	public static final String ATTRIB_NAME = "ATTRIB_NAME";

	String _gfls; // gfls command to use.
	String gfarm_path; // gfarm path to parse.
	HashMap[] attribs; // parsed data of gfarm_path;
	GFront gfront;

	public GFLSParser(GFront _gfront) {
		_gfls = DEFAULT_GFLS;
		gfarm_path = null;
		attribs = null;
		gfront = _gfront;
	}

	public String getPATH() {
		return new String(gfarm_path);
	}
	public void setPATH(String p) throws IllegalStateException {
		if (attribs != null) {
			// already parsed.
			throw new IllegalStateException("Already parsed parser.");
		}
		gfarm_path = new String(p);
	}
	public void setCommand(String cmd) throws IllegalStateException {
		if (attribs != null) {
			// already parsed.
			throw new IllegalStateException("Already parsed parser.");
		}
		_gfls = new String(cmd);
	}

	protected CommandOutput _spawn_gfls_and_capture_output() {
		Runtime r = Runtime.getRuntime();
		String[] cmds = { _gfls, "-lT", gfarm_path };
		Process p = null;
		try {
			p = r.exec(cmds);
		} catch (IOException ioe) {
			// cannot spawn gfls.
			return null;
		}
		int ret = -1;
		ArrayList l = new ArrayList();
		try {
			InputStream is = p.getInputStream();
			BufferedReader br = new BufferedReader(new InputStreamReader(is));
			/*
			ProgressMonitorInputStream pmis = new ProgressMonitorInputStream(gfront, "gfls...", is);

			//InputStream myis = new BufferedInputStream(pmis);
			StringWriter sw = new StringWriter();
			ProgressMonitor pm = pmis.getProgressMonitor();
			pm.setProgress(0);
			pm.setMillisToDecideToPopup(200);
			pm.setMaximum(10000);
			pm.setNote("**********************************************");
			
			System.out.println("available: " + is.available());

			int c;
			while((c = pmis.read()) != -1){
				sw.write(c);
				System.out.print(c);
			}

			BufferedReader br = new BufferedReader(new StringReader(sw.toString()));
			*/
			while (true) {
				String ln = br.readLine();
				if (ln == null) {
					// End of stream.
					break;
				}
				l.add(ln);
			}
			//pm.close();
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
		String attr = (String) (st.nextToken());
		nm.put(ATTRIB_ATTR, attr);

		// try extract file owner.
		if (st.hasMoreTokens() == false) {
			// Invalid output, no second token.
			return null;
		}
		String user = (String) (st.nextToken());
		nm.put(ATTRIB_USER, user);

		// try extract file group.
		if (st.hasMoreTokens() == false) {
			// Invalid output, no third token.
			return null;
		}
		String group = (String) (st.nextToken());
		nm.put(ATTRIB_GROUP, group);

		// try extract file size.
		if (st.hasMoreTokens() == false) {
			// Invalid output, no 4th token.
			return null;
		}
		String size = (String) (st.nextToken());
		nm.put(ATTRIB_SIZE, size);

		// try extract modified date.
		if (st.hasMoreTokens() == false) {
			// Invalid output, no 5th token.
			return null;
		}
		String mon = (String) (st.nextToken());
		nm.put(ATTRIB_MTIME_MONTH, mon);

		if (st.hasMoreTokens() == false) {
			// Invalid output, no 6th token.
			return null;
		}
		String day = (String) (st.nextToken());
		nm.put(ATTRIB_MTIME_DAY, day);

		if (st.hasMoreTokens() == false) {
			// Invalid output, no 7th token, time or year.
			return null;
		}
		String time = (String) (st.nextToken());
		nm.put(ATTRIB_MTIME_TIME, time);

		if (st.hasMoreTokens() == false) {
			// Invalid output, no 7th token, time or year.
			return null;
		}
		String year = (String) (st.nextToken());
		nm.put(ATTRIB_MTIME_YEAR, year);
		nm.put(ATTRIB_MTIME_DATE, mon + " " + day + " " + time + " " + year);

		if (st.hasMoreTokens() == false) {
			// Invalid output, no 8th token.
			return null;
		}
		String filename = (String) (st.nextToken());
		nm.put(ATTRIB_NAME, filename);

		return nm;
	}

	public boolean parse() throws IllegalStateException {
		if (attribs != null) {
			// already parsed.
			throw new IllegalStateException("Already parsed parser.");
		}
		if (gfarm_path == null) {
			throw new IllegalStateException("No path is set.");
		}
		CommandOutput o = _spawn_gfls_and_capture_output();
		if (o == null) {
			// spawning gfls is failed.
			return false;
		}
		if (o.retcode != 0) {
			// failure.
			return false;
		}
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
		return true;
	}

	public int getSize() {
		if (attribs == null) {
			// not parsed.
			return -1;
		}
		return attribs.length;
	}

	public String getAttr(int i, String key) {
		if (attribs == null) {
			// not parsed.
			return null;
		}
		return (String) (attribs[i].get(key));
	}

}
