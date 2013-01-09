package gfront;
import java.awt.BorderLayout;
import java.awt.Container;
import java.awt.Dimension;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.HashMap;
import java.util.Locale;

import javax.swing.JFrame;
import javax.swing.JMenu;
import javax.swing.JMenuBar;
import javax.swing.JMenuItem;
import javax.swing.JProgressBar;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JTable;
import javax.swing.JTree;
import javax.swing.ListSelectionModel;
import javax.swing.SwingUtilities;
import javax.swing.event.ListSelectionEvent;
import javax.swing.event.ListSelectionListener;
import javax.swing.event.TreeExpansionEvent;
import javax.swing.event.TreeSelectionEvent;
import javax.swing.event.TreeSelectionListener;
import javax.swing.event.TreeWillExpandListener;
import javax.swing.table.JTableHeader;
import javax.swing.tree.DefaultTreeModel;
import javax.swing.tree.DefaultTreeSelectionModel;
import javax.swing.tree.TreeNode;
import javax.swing.tree.TreePath;

public class GFront
	extends JFrame
	implements
		GFAppConst,
		GFrontConst,
		ListSelectionListener,
		TreeSelectionListener,
		TreeWillExpandListener {
	DirectoryTreeNode root_dir;
	DefaultTreeModel dirs;
	FileTableModel files;
	FragmentEditor property;
	JTree tre_dirs;
	JTable tbl_files;

	JMenuBar menubar;
	JMenu[] menus;
	JMenuItem[] menuItems;

	ChangeLookAndFeel clf;
	
	TreePath selected_path;
	DirectoryTreeNode selected_dir;
	String ldap_hostname;
	String ldap_base;
	int ldap_port;
	
	JScrollPane jsp_files;
	HashMap dir_node_map;

	private JProgressBar progressBar;

	public GFront() {
		super();
		setSize(600, 600);

		selected_path = null;
		selected_dir = null;
		_node_is_not_selected_now();
		//setDefaultCloseOperation(DISPOSE_ON_CLOSE);
		setDefaultCloseOperation(EXIT_ON_CLOSE);
		//ldap_hostname = "(not connected)";
		ldap_hostname = "GFront";
		ldap_base = "";
		ldap_port = DEFAULT_METASERVER_PORT;
		
		root_dir = new DirectoryTreeNode();
		root_dir.setUserObject("/");
		dirs = new DefaultTreeModel(root_dir);
		
		files = new FileTableModel();

		clf = new ChangeLookAndFeel(this);

		// Tree (gfls)
		tre_dirs = new JTree(dirs);
		tre_dirs.addTreeWillExpandListener(this);
		tre_dirs.addTreeSelectionListener(this);
		RightClickDirTree tree_right = new RightClickDirTree(tre_dirs, this);
		tre_dirs.addMouseListener(tree_right);
		clf.addComponent(tree_right.jpop);

		DefaultTreeSelectionModel tslm = new DefaultTreeSelectionModel();
		tslm.setSelectionMode(DefaultTreeSelectionModel.SINGLE_TREE_SELECTION);
		tre_dirs.setSelectionModel(tslm);

		// Table (gfls)
		tbl_files = new JTable(files);
		tbl_files.setShowGrid(false);
		tbl_files.setShowHorizontalLines(false);
		tbl_files.setShowVerticalLines(true);
		tbl_files.setSelectionMode(ListSelectionModel.SINGLE_SELECTION);
		tbl_files.getSelectionModel().addListSelectionListener(this);
		
		RightClickTable tbl_right = new RightClickTable(tbl_files, this);
		tbl_files.addMouseListener(tbl_right);
		clf.addComponent(tbl_right.jpop);

		// Property (gfwhere)
		property = new FragmentEditor();
		property.setEnabled(false);
		
		// ProgressBar
		progressBar = new JProgressBar(0, 1000);
		progressBar.setValue(0);
		progressBar.setStringPainted(true);
		progressBar.setString("");          //but don't paint it
		//progressBar.setIndeterminate(true);
		Dimension dm = new Dimension();
		dm.setSize(5, 30);
		progressBar.setMinimumSize(dm);
		progressBar.setMaximumSize(dm);
		//progressBar.setPreferredSize(dm);

		jsp_files = new JScrollPane(tbl_files);
		jsp_files.getViewport().setBackground(tbl_files.getBackground());

		JTableHeader jth = tbl_files.getTableHeader();
		jsp_files.setColumnHeaderView(jth);

		// right
		JSplitPane tb_split =
			new JSplitPane(
				JSplitPane.VERTICAL_SPLIT,
				jsp_files,
				property);
		tb_split.setDividerLocation(getHeight()/2);
		tb_split.setDividerSize(5);
		
		// left
		JSplitPane left_split =
			new JSplitPane(
				JSplitPane.VERTICAL_SPLIT,
				new JScrollPane(tre_dirs),
				progressBar);				
		//left_split.setDividerLocation(1000);

		// all
		JSplitPane lr_split =
			new JSplitPane(
				JSplitPane.HORIZONTAL_SPLIT,
				true,
				new JScrollPane(tre_dirs),
//				left_split,
				tb_split);
		lr_split.setDividerLocation(getWidth()/4);
		lr_split.setDividerSize(5);
		
		Container c = getContentPane();
		c.setLayout(new BorderLayout());
		c.add(lr_split, BorderLayout.CENTER);

		init_all_menus();
		setJMenuBar(menubar);
		setTitle(ldap_hostname);

	}

	public boolean initial_setup() {
		root_dir.removeAllChildren();
		boolean retv = _update_node(root_dir, (String) (root_dir.getUserObject()));
		if(retv == false){
			return false;
		}
			
		if(selected_path == null){
			int sz = root_dir.getChildCount();
			GFrontCommon gfc = new GFrontCommon();
			String workDir = gfc.runNomalCommandToString(this, "gfsetdir", "gfsetdir");
			String userName;
			try {
				userName = workDir.substring(workDir.indexOf("gfarm:/")+7, workDir.indexOf(";"));
			} catch(Exception e){
				userName = System.getProperty("user.name");
			}
System.out.println("gfpwd: gfarm:/" + userName);
			for(int i=0; i<sz; i++){
				String name = (String) ((DirectoryTreeNode) root_dir.getChildAt(i)).getUserObject();
				if(userName.equals(name)){
					tre_dirs.setSelectionRow(i+1);
					break;
				}
			}
		}
//		else {
//			tre_dirs.setExpandsSelectedPaths(true);
//			tre_dirs.setSelectionPath(selected_path);
//		}
		return true;
	}
	
	protected boolean _update_node(DirectoryTreeNode n, String fullpath) {
		GFLSParser p = new GFLSParser(this);
		p.setPATH(fullpath);
		boolean r = p.parse();
		if (r == false) {
			// Prasing failed.
			return false;
		}
		int sz = p.getSize();
		for (int i = 0; i < sz; i++) {
			String attr = p.getAttr(i, GFLSParser.ATTRIB_ATTR);
			String name = p.getAttr(i, GFLSParser.ATTRIB_NAME);
			if (attr.charAt(0) == 'd') {
				// is dir.
				DirectoryTreeNode nn = new DirectoryTreeNode();
				nn.setUserObject(name);
				n.add(nn);
			}
		}
		n.setExplored(true);
		dirs.nodeStructureChanged(n);
		return true;
	}

	// Closing tree node.
	public void treeWillCollapse(TreeExpansionEvent e) {
		// Nothing to do.	
	}
	// Opening tree node.
	public void treeWillExpand(TreeExpansionEvent e) {
		TreePath tp = e.getPath();
		DirectoryTreeNode n = (DirectoryTreeNode) (tp.getLastPathComponent());
		if (n.isExplored()) {
			// Already explored, nothing to do.
			return;
		}
		_update_node(n, _get_fullpath_dirname(tp.getPath()));
	}
	// Selecting tree node.
	public synchronized void valueChanged(TreeSelectionEvent e) {
		progressBar.setIndeterminate(true);

		TreePath p = e.getPath();
		selected_path = p;
		Object[] o = p.getPath();
		DirectoryTreeNode n = (DirectoryTreeNode) (o[o.length - 1]);
		TreeNode[] path = n.getPath();
		String pathname = _get_fullpath_dirname(path);
		if(pathname == null){
			return;
		}
		selected_dir = n;
		if(!pathname.equals(savedSelectedDirname)){
			savedSelectedFilename = null;
			savedSelectedDirname = pathname;
			property.setNullFragmentTable();
		}
		_update_table(pathname);

//System.out.println("savedSelectedDirname: " + savedSelectedDirname);
		
		System.out.println("Tree Selected");
		//progressBar.setIndeterminate(false);
	}

	protected String _get_fullpath_dirname(Object[] path) {
		StringBuffer sb = new StringBuffer();
		sb.append("gfarm:/");
		DirectoryTreeNode n = (DirectoryTreeNode) (path[0]);
		String p = (String) (n.getUserObject());
		if(p.equals("/") == false){
			sb.append(p);
			sb.append("/");	
		}

		for (int i = 1; i < path.length; i++) {
			n = (DirectoryTreeNode) (path[i]);
			p = (String) (n.getUserObject());
			sb.append(p);
			if (p.equals("/") == false) {
				sb.append("/");
			}
		}
		return sb.toString();
	}

	protected boolean _update_table(String pathname) {
		GFLSParser p = new GFLSParser(this);
		p.setPATH(pathname);
		setTitle("GFront - " + pathname);
		FileTableModel dtm = _create_new_table_model();
		if (p.parse() == false) {
			// parsing failed, then clear table and show 'parse failed'.
			Object[] rowdata = new Object[3];
			rowdata[0] = "*** gfls parsing error ***";
			rowdata[1] = "";
			rowdata[2] = "";
			//rowdata[3] = "";
			dtm.addRow(rowdata);
		} else {
			int sz = p.getSize();
			SimpleDateFormat dtpr =
				new SimpleDateFormat("MMM dd HH:mm:ss yyyy", Locale.US);
			DateFormat dtft = DateFormat.getDateTimeInstance();
			// add gfls output to gfwhere
			//GFWHEREParser2 wp = new GFWHEREParser2();
			for (int i = 0; i < sz; i++) {
				String attr = p.getAttr(i, GFLSParser.ATTRIB_ATTR);
				String name = p.getAttr(i, GFLSParser.ATTRIB_NAME);
				//wp.addPATH(pathname + name);
			}
			//wp.parse();
			for (int i = 0; i < sz; i++) {
				String attr = p.getAttr(i, GFLSParser.ATTRIB_ATTR);
				String name = p.getAttr(i, GFLSParser.ATTRIB_NAME);
				//int frag = wp.getFragmentCount(i);
				int frag = -1;
				if (attr.charAt(0) != 'd') {
					// is file.
					String date = p.getAttr(i, GFLSParser.ATTRIB_MTIME_DATE);
					try {
						Date dt = dtpr.parse(date);
						date = dtft.format(dt);
					} catch (ParseException pe) {
						pe.printStackTrace();
					}
					Object[] rowdata = new Object[4];
					rowdata[0] = name;
					rowdata[1] = p.getAttr(i, GFLSParser.ATTRIB_SIZE);
					rowdata[2] = date;
//					if (frag == -1) {
//						rowdata[3] = "*";
//					} else {
//						rowdata[3] = new Integer(frag);
//					}
					dtm.addRow(rowdata);
				}
			}
			/*
			for (int i = 0; i < sz; i++) {
				String attr = p.getAttr(i, GFLSParser.ATTRIB_ATTR);
				String name = p.getAttr(i, GFLSParser.ATTRIB_NAME);
				GFWHEREParser wp = new GFWHEREParser();
				wp.setPATH(pathname + name);
				wp.parse();
				int frag = wp.getFragmentCount();
				if (attr.charAt(0) != 'd') {
					// is file.
					String date = p.getAttr(i, GFLSParser.ATTRIB_MTIME_DATE);
					try {
						Date dt = dtpr.parse(date);
						date = dtft.format(dt);
					} catch (ParseException pe) {
						pe.printStackTrace();
					}
					Object[] rowdata = new Object[4];
					rowdata[0] = name;
					rowdata[1] = p.getAttr(i, GFLSParser.ATTRIB_SIZE);
					rowdata[2] = date;
					rowdata[3] = new Integer(frag);
					dtm.addRow(rowdata);
				}
			}
			*/
		}

		files = dtm;
		SwingUtilities.invokeLater(new Runnable() {
			public void run() {
				tbl_files.setModel(files);
			}
		});
		jsp_files.getViewport().setBackground(tbl_files.getBackground());
		return true;
	}
	
	private int save_row = -1;
	
	public synchronized void valueChanged(ListSelectionEvent e) {
		int row = tbl_files.getSelectedRow();
		if(save_row != row){
			save_row =row;
		}
		else {
			return;
		}

		if (row < 0) {
			// Not selected.
			return;
		}
		DirectoryTreeNode n = selected_dir;
		String dir = _get_fullpath_dirname(n.getPath());
		String name = (String) (tbl_files.getValueAt(row, 0));
		savedSelectedFilename = dir + name;
		property.update(savedSelectedFilename);
		setTitle("GFront - " + savedSelectedFilename);
		
		System.out.println("Table Selected " + e.getFirstIndex() + " " + e.getLastIndex() + ": " + savedSelectedFilename);
	}

	protected FileTableModel _create_new_table_model() {
		FileTableModel dtm = new FileTableModel();
		dtm.setColumnIdentifiers(
		new String[] { "Filename", "Size", "Date"});
//			new String[] { "Filename", "Size", "Date", "Fragments" });
		return dtm;
	}

	private synchronized void _node_is_not_selected_now() {
		//
	}
	private synchronized void _node_is_selected_now() {
		//
	}

	protected void init_all_menus() {
		_init_menubar_menus();
		_init_popup_menus();
	}
	protected void _init_popup_menus() {
	}
	protected void _init_menubar_menus() {
		menubar = new JMenuBar();
		menus = new JMenu[IDX_MENU_COUNT];
		menus[IDX_MENU_FILE] = new JMenu(_LABEL_MENU_FILE);
		menus[IDX_MENU_EDIT] = new JMenu(_LABEL_MENU_EDIT);
		menus[IDX_MENU_SPECIAL] = new JMenu(_LABEL_MENU_SPECIAL);

		/*
		menuItems = new JMenuItem[IDX_ITEM_COUNT];
		menuItems[IDX_ITEM_IMPORT] =
			new JMenuItem(new GFAction(_LABEL_ITEM_IMPORT, this));
		menuItems[IDX_ITEM_EXPORT] =
			new JMenuItem(new GFAction(_LABEL_ITEM_EXPORT, this));
		menuItems[IDX_ITEM_REPL] =
			new JMenuItem(new GFAction(_LABEL_ITEM_REPL, this));
		menuItems[IDX_ITEM_REMOVE] =
			new JMenuItem(new GFAction(_LABEL_ITEM_REMOVE, this));
		menuItems[IDX_ITEM_REGISTER] =
			new JMenuItem(new GFAction(_LABEL_ITEM_REGISTER, this));
		menuItems[IDX_ITEM_MKDIR] =
			new JMenuItem(new GFAction(_LABEL_ITEM_MKDIR, this));
		menuItems[IDX_ITEM_RUN] =
			new JMenuItem(new GFAction(_LABEL_ITEM_RUN, this));
		menuItems[IDX_ITEM_MPIRUN] =
			new JMenuItem(new GFAction(_LABEL_ITEM_MPIRUN, this));
		menuItems[IDX_ITEM_METASERVER] =
			new JMenuItem(new GFAction(_LABEL_ITEM_METASERVER, this));
		menuItems[IDX_ITEM_INTERNAL_DEMO] =
			new JMenuItem(new GFAction(_LABEL_ITEM_INTERNAL_DEMO, this));
		*/


		int i = IDX_MENU_FILE;
		JMenuItem gfhost = new JMenuItem("gfhost");
		GfhostList ghl = new GfhostList(this);
		gfhost.addActionListener(ghl);
		menus[i].add(gfhost);

		menus[i].addSeparator();

		JMenuItem ex = new JMenuItem("Exit");
		ex.addActionListener(new ActionListener() {
			public void actionPerformed(ActionEvent e) {
				System.exit(0);
			}
		});
		menus[i].add(ex);
		/*
		menus[i].add(menuItems[IDX_ITEM_IMPORT]);
		menus[i].add(menuItems[IDX_ITEM_EXPORT]);
		menus[i].add(menuItems[IDX_ITEM_REPL]);
		menus[i].add(menuItems[IDX_ITEM_REMOVE]);
		menus[i].add(menuItems[IDX_ITEM_REGISTER]);
		menus[i].add(menuItems[IDX_ITEM_MKDIR]);
		menus[i].add(menuItems[IDX_ITEM_RUN]);
		menus[i].add(menuItems[IDX_ITEM_MPIRUN]);
		menus[i].add(menuItems[IDX_ITEM_METASERVER]);
		*/

		i = IDX_MENU_EDIT;
		JMenuItem menuRefresh = new JMenuItem("Refresh");
		menuRefresh.addActionListener(new AllRefresh());
		menus[i].add(menuRefresh);

		// Look and Feel
		i = IDX_MENU_SPECIAL;
		//		menus[i].add(menuItems[IDX_ITEM_INTERNAL_DEMO]);
		//		menus[i].addSeparator();
		clf.addMenuItem(menus[i]);

		// in the initial state, any node is not selected.
		_node_is_not_selected_now();

		for (int j = 0; j < menus.length; j++) {
			menubar.add(menus[j]);
		}
	}
	
	
	public String savedSelectedDirname;
	public String savedSelectedFilename;
	public class AllRefresh implements ActionListener {
		public void actionPerformed(ActionEvent e) {
			initial_setup();
			_update_table(savedSelectedDirname);
			property.update(savedSelectedFilename);
			if(savedSelectedFilename != null){
				setTitle("GFront - " + savedSelectedFilename);
			}
		}
	}
}
