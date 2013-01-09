package gfront;
import java.awt.BorderLayout;
import java.util.ArrayList;
import java.util.Vector;

import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.ListSelectionModel;
import javax.swing.SwingUtilities;
import javax.swing.event.ListSelectionEvent;
import javax.swing.event.ListSelectionListener;
import javax.swing.table.JTableHeader;

public class FragmentEditor
	extends JPanel
	implements GFAppConst, ListSelectionListener {
	JTable tbl_frag;
	FragmentTableModel frags;
	JScrollPane jsp;

	protected static FragmentTableModel _create_new_table_model() {
		FragmentTableModel dtm = new FragmentTableModel();
		return dtm;
	}
	public FragmentEditor() {
		frags = _create_new_table_model();
		tbl_frag = new JTable(frags);
		tbl_frag.setShowGrid(false);
		tbl_frag.setShowHorizontalLines(false);
		tbl_frag.setShowVerticalLines(true);
		tbl_frag.setSelectionMode(ListSelectionModel.SINGLE_SELECTION);
		tbl_frag.getSelectionModel().addListSelectionListener(this);
		jsp = new JScrollPane(tbl_frag);
		jsp.getViewport().setBackground(tbl_frag.getBackground());
		JTableHeader jth = tbl_frag.getTableHeader();
		jsp.setColumnHeaderView(jth);
		setLayout(new BorderLayout());
		add(jsp, BorderLayout.CENTER);
	}

	public synchronized void valueChanged(ListSelectionEvent e) {
		//
	}

	/*
		public void update(String fullpath) {
			GFWHEREThread gwt = new GFWHEREThread();
			gwt.fullpath = fullpath;
			gwt.start();
			while(gwt.fullpath != null){
				System.out.println("sleep");
				try {
					Thread.sleep(1000);
				} catch (InterruptedException e) {
					e.printStackTrace();
				}
			}
		}
	
		class GFWHEREThread extends Thread {
			public String fullpath = null;
	
			public void run() {
				if(fullpath == null){
					return;
				}
				System.out.println("thread: gfwhere");
				GFWHEREParser p = new GFWHEREParser();
				p.setPATH(fullpath);
				if (p.parse() == false) {
					// update failed.
					return;
				}
				FragmentTableModel dtm = _create_new_table_model();
				int sz = p.getSize();
				Vector v = null;
				int col_sz = 2;
				dtm.setColumnCount(col_sz);
				try {
					// OK, file is normal file, index is index.
					int idx = p.getFragmentIndex(0);
					dtm.setExecutable(false);
					for (int i = 0; i < sz; i++) {
						int id = p.getFragmentIndex(i);
						ArrayList nl = p.getNodeList(i);
						if (col_sz - 1 < nl.size()) {
							int old_col_sz = col_sz;
							col_sz = nl.size() + 1;
							for (int j = 0; j < col_sz - old_col_sz; j++) {
								dtm.addColumn("");
							}
							//		    dtm.setColumnCount(col_sz);
						}
						v = new Vector(nl);
						v.add(0, new Integer(id));
						dtm.addRow(v);
					}
				} catch (NumberFormatException nfe) {
					// OK, file is executable, index is architecture.
					dtm.setExecutable(true);
					for (int i = 0; i < sz; i++) {
						String arch = p.getARCH(i);
						ArrayList nl = p.getNodeList(i);
						v = new Vector(nl);
						v.add(0, arch);
						dtm.addRow(v);
					}
				}
				tbl_frag.setModel(dtm);
				fullpath = null;
			}
		}
		*/
		
	public void setNullFragmentTable(){
		FragmentTableModel dtm = _create_new_table_model();
		frags = dtm;
		SwingUtilities.invokeLater(new Runnable() {
			public void run() {
				tbl_frag.setModel(frags);
			}
		});
	}
	
	public void update(String fullpath) {
		if(fullpath == null){
			setNullFragmentTable();
			return;
		}
		GFWHEREParser p = new GFWHEREParser();
		p.setPATH(fullpath);
		p.start();

		try {
			while (p.isAlive()) {
				//System.out.println("sleep: gfwhere");
				p.join(100);
			}
		} catch (InterruptedException e) {
			//e.printStackTrace();
		}

		if (p.retval == false) {
			// update failed.
			setNullFragmentTable();
			return;
		}
		FragmentTableModel dtm = _create_new_table_model();
		int sz = p.getSize();
		Vector v = null;
		int col_sz = 2;
		int colMax = 11;  // Limit: Filesystem Node = 10
		dtm.setColumnCount(col_sz);
		try {
			// OK, file is normal file, index is index.
//			int idx = p.getFragmentIndex(0);
			dtm.setExecutable(false);
			for (int i = 0; i < sz; i++) {
				int id = p.getFragmentIndex(i);
				ArrayList nl = p.getNodeList(i);
				if (col_sz - 1 < nl.size()) {
					int old_col_sz = col_sz;
					col_sz = nl.size() + 1;
					if(col_sz > colMax){
						col_sz = colMax;
					}
					for (int j = 0; j < col_sz - old_col_sz; j++) {
						dtm.addColumn("");
					}
					//		    dtm.setColumnCount(col_sz);
				}
				v = new Vector(nl);
				v.add(0, new Integer(id));
				dtm.addRow(v);
			}
		} catch (NumberFormatException nfe) {
			// OK, file is executable, index is architecture.
//			dtm.setExecutable(true);
//			for (int i = 0; i < sz; i++) {
//				String arch = p.getARCH(i);
//				ArrayList nl = p.getNodeList(i);
//				v = new Vector(nl);
//				v.add(0, arch);
//				dtm.addRow(v);
//			}
			dtm.setExecutable(true);
			for (int i = 0; i < sz; i++) {
				String arch = p.getARCH(i);
				ArrayList nl = p.getNodeList(i);
				if (col_sz - 1 < nl.size()) {
					int old_col_sz = col_sz;
					col_sz = nl.size() + 1;
					if(col_sz > colMax){
						col_sz = colMax;
					}
					for (int j = 0; j < col_sz - old_col_sz; j++) {
						dtm.addColumn("");
					}
				}
				v = new Vector(nl);
				v.add(0, arch);
				dtm.addRow(v);
			}
		}

		//tbl_frag.setModel(dtm);
		frags = dtm;
		SwingUtilities.invokeLater(new Runnable() {
			public void run() {
				tbl_frag.setModel(frags);
			}
		});
		jsp.getViewport().setBackground(tbl_frag.getBackground());
	}
}

class FragmentTableModel extends ReadOnlyTableModel {
	protected boolean isExecutable = false;
	public FragmentTableModel() {
		setColumnIdentifiers(new String[] { "Index", "Filesystem Node" });
	}
	public Class getColumnClass(int idx) {
		if (idx == 0 && isExecutable) {
			return String.class;
		}
		if (idx == 0 && !(isExecutable)) {
			return Number.class;
		}
		return Object.class;
	}
	public void setExecutable(boolean t) {
		isExecutable = t;
		if (t == true) {
			setColumnIdentifiers(
				new String[] { "Architecture", "Filesystem Node" });
		}
	}
}
