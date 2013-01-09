package gfront;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.MouseEvent;
import java.awt.event.MouseListener;
import java.io.File;

import javax.swing.JFileChooser;
import javax.swing.JMenuItem;
import javax.swing.JOptionPane;
import javax.swing.JPopupMenu;
import javax.swing.JTree;
import javax.swing.tree.TreePath;

public class RightClickDirTree implements MouseListener, ActionListener {
	public JPopupMenu jpop;
	JMenuItem gfrmdir, gfmkdir, gfreg, gfimport_text, gfimport_fixed;
	JTree jt;
	GFront gfront;

	RightClickDirTree(JTree _jt, GFront _gfront) {
		jpop = new JPopupMenu();

		//gfreg = new JMenuItem("gfreg -N 1 -I 0");
		gfreg = new JMenuItem("import");
		gfreg.addActionListener(this);
		jpop.add(gfreg);

		gfimport_text = new JMenuItem("* gfimport_text");
		gfimport_text.addActionListener(this);
		//jpop.add(gfimport_text);

		gfimport_fixed = new JMenuItem("* gfimport_fixed");
		gfimport_fixed.addActionListener(this);
		//jpop.add(gfimport_fixed);

		gfmkdir = new JMenuItem("gfmkdir");
		gfmkdir.addActionListener(this);
		jpop.add(gfmkdir);

		gfrmdir = new JMenuItem("gfrmdir");
		gfrmdir.addActionListener(this);
		jpop.add(gfrmdir);

		jt = _jt;
		gfront = _gfront;
	}

	public void actionPerformed(ActionEvent ae) {
		GFrontCommon gfc = new GFrontCommon();

		TreePath tp = jt.getSelectionPath();
		if (tp == null) {
			return;
		}
		String dirurl = gfront.savedSelectedDirname;
		
		if (ae.getSource() == gfreg) {
			JFileChooser chooser = new JFileChooser();
			int returnVal = chooser.showOpenDialog(gfront);
			if (returnVal != JFileChooser.APPROVE_OPTION) {
				return;
			}
			File inFile = chooser.getSelectedFile();
			if (inFile.exists() == false) {
				JOptionPane.showMessageDialog(
					null,
					inFile.getName() + ": No such file or directory",
					"gfreg",
					JOptionPane.INFORMATION_MESSAGE);
				return;
			}
			String inPath = inFile.getPath();

			String regName =
				JOptionPane.showInputDialog(
					gfront,
					"a name for registering",
					inFile.getName());
			if(regName == null) {
				return;
			}

			//String cmd = "gfreg -N 1 -I 0 " + inFile + " " + dirurl + regName;
			String cmd = "gfreg " + inFile + " " + dirurl + regName;
			gfc.runNomalCommand(gfront, cmd, "gfreg");
			// update
			gfront._update_table(gfront.savedSelectedDirname);

		} else if (ae.getSource() == gfimport_text) {
			gfc.showTextArea(gfront, "gfimport_text", "test\ntest\n");
		} else if (ae.getSource() == gfimport_fixed) {

		} else if (ae.getSource() == gfrmdir) {
			Object[] options = { "OK", "CANCEL" };
			int ret =
				JOptionPane.showOptionDialog(
					null,
					"remove " + dirurl + " ?",
					"gfrmdir",
					JOptionPane.DEFAULT_OPTION,
					JOptionPane.WARNING_MESSAGE,
					null,
					options,
					options[0]);
			if (ret == 0) { // OK
				//System.out.println("$ gfrmdir " + dirurl);
				gfc.runNomalCommand(gfront, "gfrmdir " + dirurl, "gfrmdir");
				gfront.initial_setup();
			} else {
				System.out.println(ret);
			}

		} else if (ae.getSource() == gfmkdir) {
			String mkdirname =
				JOptionPane.showInputDialog("gfmkdir: input directry name");
			//System.out.println("$ gfmkdir " + dirurl + mkdirname);
			gfc.runNomalCommand(gfront, "gfmkdir " + dirurl + mkdirname, "gfmkdir");
			gfront.initial_setup();
		}
	}


	private void actionPoint(MouseEvent e) {
		if (e.getSource() == jt) {
			int selRow = jt.getRowForLocation(e.getX(), e.getY());
			if (selRow == jt.getMinSelectionRow()) {
				System.out.println("tree sel OK");
				if (jt.getSelectionPath() == null) {
					return;
				}
				System.out.println(e.getX() + ", " + e.getY());
				jpop.show(jt, e.getX(), e.getY());
			}

		}
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
