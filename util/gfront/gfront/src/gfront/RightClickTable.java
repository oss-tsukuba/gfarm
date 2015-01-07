package gfront;

import java.awt.Toolkit;
import java.awt.event.*;
import java.awt.event.ActionListener;
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import javax.swing.JFileChooser;
import javax.swing.JMenuItem;
import javax.swing.JOptionPane;
import javax.swing.JPopupMenu;
import javax.swing.JTable;
import javax.swing.ProgressMonitor;
import javax.swing.Timer;

public class RightClickTable implements MouseListener, ActionListener {
	public JPopupMenu jpop;
	JMenuItem gfrun, gfrm, gfexport, gfexport_view, gfrep;
	JTable jt;
	GFront gfront;

	private ExportTask task;
	private ProgressMonitor progressMonitor;
	private Timer timer;

	RightClickTable(JTable _jt, GFront _gfront) {
		jpop = new JPopupMenu();

		gfrun = new JMenuItem("gfrun");
		gfrun.addActionListener(this);
		jpop.add(gfrun);

		gfrm = new JMenuItem("gfrm");
		gfrm.addActionListener(this);
		jpop.add(gfrm);

		gfexport = new JMenuItem("gfexport");
		gfexport.addActionListener(this);
		jpop.add(gfexport);

		gfexport_view = new JMenuItem("gfexport viewer");
		gfexport_view.addActionListener(this);
		jpop.add(gfexport_view);

		gfrep = new JMenuItem("* gfrep");
		gfrep.addActionListener(this);
		jpop.add(gfrep);

		jt = _jt;
		gfront = _gfront;
	}

	public void actionPerformed(ActionEvent ae) {
		GFrontCommon gfc = new GFrontCommon();
		
		int row = jt.getSelectedRow();
		if (row < 0) {
			// Not selected.
			return;
		}
		String name = (String) (jt.getValueAt(row, 0));
		String url = gfront.savedSelectedDirname + name;
		//System.out.println(url);

		// check file size
		long filesize;
		try {
			//filesize = new Long((String) jt.getValueAt(row, 1)).longValue();
			filesize = Long.parseLong((String) jt.getValueAt(row, 1));
		} catch (Exception e) {
			//e.printStackTrace();
			// gfls error
			JOptionPane.showMessageDialog(
				null,
				"cannot get file size",
				"error",
				JOptionPane.ERROR_MESSAGE);
			//filesize = 777777;
			return;
		}

		if (ae.getSource() == gfrun) {
			String commandLine =
				JOptionPane.showInputDialog(
					gfront,
					"gfrun (for the output of strings)",
					"gfarm:CMD " + url);
			if(commandLine != null){
				gfc.runNomalCommand(gfront, "gfrun " + commandLine, "gfrun");
			}
		}
		else if (ae.getSource() == gfrm) {
			Object[] options = { "OK", "CANCEL" };
			int ret =
				JOptionPane.showOptionDialog(
					null,
					"remove " + url + " ?",
					"gfrm",
					JOptionPane.YES_NO_OPTION,
					JOptionPane.WARNING_MESSAGE,
					null,
					options,
					options[0]);
			if (ret == 0) { // OK
				System.out.println("$ gfrm " + url);
				gfc.runNomalCommand(gfront, "gfrm " + url, "gfrm");
				// update
				gfront._update_table(gfront.savedSelectedDirname);
			} else { // cancel
				// do nothing
				System.out.println(ret);
			}
		} else if (ae.getSource() == gfexport) {
			JFileChooser chooser = new JFileChooser();
			int returnVal = chooser.showSaveDialog(gfront);
			if (returnVal == JFileChooser.APPROVE_OPTION) {
				File outf = chooser.getSelectedFile();
				String to = outf.getName();
				System.out.println("*** $ gfexport " + url + " > " + to);

				if (outf.exists()) {
					Object[] options = { "OK", "CANCEL" };
					int ret =
						JOptionPane.showOptionDialog(
							null,
							"\"" + to + "\" exists. overwrite?",
							"gfexport",
							JOptionPane.YES_NO_OPTION,
							JOptionPane.WARNING_MESSAGE,
							null,
							options,
							options[0]);
					if (ret != 0) { // ! cancel
						return;
					}
				}
				run_gfexport(filesize, url, outf);
			}
		} else if (ae.getSource() == gfexport_view) {
			run_gfexport(filesize, url, null);
		} else if (ae.getSource() == gfrep) {
			/*
			gfrep -H hostfile gfarm-URL  一時ファイル作らなければならない
			gfrep -D domainname gfarm-URL  できる
			右下  ノード名選択すると -s オプションも反映
			gfrep -I fragment-index [ -s src-node ] -d dest-node gfarm-URL
			*/

		}
	}

	private void run_gfexport(long filesize, String url, File outf) {
		//filesize = 1000;
		int setmax;
		boolean isLong;
		if (filesize > Integer.MAX_VALUE) {
			setmax = (int) ((filesize >> 32) & 0x00000000FFFFFFFF);
			isLong = true;
		} else {
			setmax = (int) filesize;
			isLong = false;
		}
		progressMonitor =
			new ProgressMonitor(gfront, "gfexport", "", 0, setmax);
		progressMonitor.setProgress(0);
		progressMonitor.setMillisToDecideToPopup(200);
		progressMonitor.setNote("0 / 0");
		task = new ExportTask(url, outf, filesize, isLong);
		task.start();
		timer = new Timer(1000, new ExportTimerListener());
		timer.start();

	}

	// exec gfexport
	class ExportTask extends Thread {
		String from;
		File to;
		long now;
		long maxsize;
		boolean isLong;
		public boolean isError;
		public String errorString;
		private boolean stopFlag;
		private StringBuffer str;

		private int maxview = 2048;

		ExportTask(String _from, File _to, long max, boolean _isLong) {
			from = _from;
			to = _to;
			maxsize = max;
			isLong = _isLong;
			if (to == null) {
				str = new StringBuffer();
			} else {
				str = null;
			}
		}

		String getMessage() {
			String msg = "copy: " + now + " / " + maxsize;
			System.out.println(msg);
			return msg;
		}

		/*
				public void run() {
					now = 0;
					while (now < maxsize) {
						// test code
						try {
							Thread.sleep(1000);
						} catch (InterruptedException e) {
							e.printStackTrace();
						}
						now += Math.random() * maxsize / 10;
					}
				}
		*/

		public void run() {
			now = 0;
			stopFlag = false;
			isError = false;

			Runtime r = Runtime.getRuntime();
			String[] cmds = { "gfexport", from };
			Process p = null;
			try {
				p = r.exec(cmds);
			} catch (IOException ioe) {
				isError = true;
				errorString = "gfexport is not found";
				return;
			}

			FileOutputStream fo = null;
			if (to != null) { // file output
				try {
					fo = new FileOutputStream(to);
				} catch (FileNotFoundException e) {
					isError = true;
					errorString = "file open error: " + to;
					return;
				}
			}

			BufferedInputStream is =
				new BufferedInputStream(p.getInputStream());
			int len;
			byte[] b = new byte[1024];
			try {
				while ((len = is.read(b)) != -1 && stopFlag != true) {
					if (to != null) {
						fo.write(b, 0, len);
					} else {
						str.append(new String(b, 0, len));
						if (str.length() > maxview) {
							str.append(
								System.getProperty("line.separator")
									+ System.getProperty("line.separator")
									+ "[print first "
									+ maxview
									+ " bytes]");
							break;
						}
					}
					now += len;
				}
			} catch (IOException ioe) {
				// do nothing
			}
			if (now > maxsize) {
				isError = true;
				errorString = "output file is bigger than input gfarm file";
			}
			if (now < maxsize) {
				isError = true;
				errorString = "output file is not enough";
				BufferedInputStream es =
					new BufferedInputStream(p.getErrorStream());
				try {
					len = es.read(b);
				} catch (IOException e1) {
					len = -1;
				}
				if (len > 0) {
					errorString = new String(b, 0, len);
				}
			}
			if (stopFlag == true) {
				System.out.println("Interrupt!");
			}

			try {
				p.waitFor();
			} catch (InterruptedException ie) {
				// Interrupted waiting, but nothing to do...
			}
			stopFlag = true;
		}

		int getCurrent() {
			if (isLong) {
				return (int) ((now >> 32) & 0x00000000FFFFFFFF);
			} else {
				return (int) now;
			}
		}

		// stop
		void end() {
			if (progressMonitor.isCanceled()) {
				stopFlag = true;
				to.delete();
			} else if (isError) {
				stopFlag = true;
			}
		}

		String viewString() {
			if (str != null) {
				return str.toString();
			} else {
				return null;
			}
		}

		boolean isDone() {
			if (stopFlag != true && now < maxsize) {
				return false;
			} else {
				return true;
			}
		}
	}

	class ExportTimerListener implements ActionListener {
		public void actionPerformed(ActionEvent evt) {
			if (progressMonitor.isCanceled()
				|| task.isDone()
				|| task.isError) {
				task.end();
				progressMonitor.close();
				Toolkit.getDefaultToolkit().beep();
				timer.stop();
				String st = task.viewString();
				if (st != null && st.length() > 0) {
					GFrontCommon gfc = new GFrontCommon();
					gfc.showTextArea(gfront, "gfexport viewer", st, 30, 60);
				} else if (task.isError) {
					JOptionPane.showMessageDialog(
						null,
						task.errorString,
						"error",
						JOptionPane.WARNING_MESSAGE);
				} else if (task.isDone()) {
					JOptionPane.showMessageDialog(
						null,
						"done",
						"gfexport",
						JOptionPane.INFORMATION_MESSAGE);

				} else {
					JOptionPane.showMessageDialog(
						null,
						"canceled",
						"gfexport",
						JOptionPane.WARNING_MESSAGE);
				}
			} else {
				progressMonitor.setNote(task.getMessage());
				progressMonitor.setProgress(task.getCurrent());
			}
		}
	}

	private void actionPoint(MouseEvent e) {
		System.out.println(e.getX() + ", " + e.getY());
		jpop.show(jt, e.getX(), e.getY());
	}

	public void mouseClicked(MouseEvent e) {
		if (e.isPopupTrigger()) {
			System.out.println("Popup");
		}
		//System.out.println("mouse click");
	}
	public void mousePressed(MouseEvent e) {
		if (e.isPopupTrigger()) { // Linux
			System.out.println("Popup2");
			actionPoint(e);
		}
		//System.out.println("mouse press");
	}
	public void mouseReleased(MouseEvent e) {
		if (e.isPopupTrigger()) { // Windows
			System.out.println("Popup3");
			actionPoint(e);
		}
		//System.out.println("mouse release");
	}
	public void mouseEntered(MouseEvent e) {
		//System.out.println("mouse enter");
	}
	public void mouseExited(MouseEvent e) {
		//System.out.println("mouse exit");
	}
}
