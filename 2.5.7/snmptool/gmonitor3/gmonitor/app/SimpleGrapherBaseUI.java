/*
 * Created on 2003/07/10
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.app;

import gmonitor.gui.GraphMonitor;

import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Container;
import java.awt.Dimension;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Insets;
import java.io.File;
import java.util.ArrayList;
import java.util.Vector;

import javax.swing.BoxLayout;
import javax.swing.ButtonGroup;
import javax.swing.DefaultComboBoxModel;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JCheckBoxMenuItem;
import javax.swing.JComboBox;
import javax.swing.JFileChooser;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JList;
import javax.swing.JMenu;
import javax.swing.JMenuBar;
import javax.swing.JMenuItem;
import javax.swing.JPanel;
import javax.swing.JRadioButtonMenuItem;
import javax.swing.JScrollPane;
import javax.swing.JTextField;
import javax.swing.JTextPane;
import javax.swing.ListCellRenderer;
import javax.swing.border.TitledBorder;
import javax.swing.filechooser.FileFilter;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class SimpleGrapherBaseUI {
	
	JFileChooser dialogFileOpen = new JFileChooser();
	JFileChooser dialogDirOpen = new JFileChooser();

	JMenu menuFile = new JMenu("File");
	JMenu menuGraphStyle = new JMenu("Style");
	JMenu menuUnit = new JMenu("Unit");
	JMenu menuMode = new JMenu("Mode");
	JMenu menuConf = new JMenu("Config");

	// menuFile
	JMenuItem menuItemFileOpen = new JMenuItem("Open Files...");
	JMenuItem menuItemFileAutoUpdate = new JMenuItem("AutoUpdate...");
	JMenuItem menuItemFileExit = new JMenuItem("Exit");
	
	// menuConf
	JMenu subMenuResample = new JMenu("Resampling");
	ButtonGroup resampleGroup = new ButtonGroup();
	JRadioButtonMenuItem menuItemRaw = new JRadioButtonMenuItem("Raw (normal)");
	String menuStrResampleBefore = "Last Value";
	JRadioButtonMenuItem menuItemBefore = new JRadioButtonMenuItem(menuStrResampleBefore);
	String menuStrResampleInterpolate = "Interpolate";
	JRadioButtonMenuItem menuItemInterpolate = new JRadioButtonMenuItem(menuStrResampleInterpolate);
	String menuStrResolution = "Resolution...";
	JMenuItem menuItemResolution = new JMenuItem(menuStrResolution);
	
	String menuStrAutoUpdateInterval = "AutoUpdate Interval...";
	JMenuItem menuItemAutoUpdateInterval = new JMenuItem(menuStrAutoUpdateInterval);

	JCheckBoxMenuItem menuItemCorrectByUptime = new JCheckBoxMenuItem("Diff mode correction by uptime");

	
	JCheckBoxMenuItem menuItemVisInfo = new JCheckBoxMenuItem("Information");
	JCheckBoxMenuItem menuItemVisGraphStyle = new JCheckBoxMenuItem("Graph Style");
	JCheckBoxMenuItem menuItemVisUnit = new JCheckBoxMenuItem("Unit");
	JCheckBoxMenuItem menuItemVisController = new JCheckBoxMenuItem("Controller");
	
	// menuGraphStyle
	JCheckBoxMenuItem menuItemGraphStyleLine = new JCheckBoxMenuItem("Line");
	JCheckBoxMenuItem menuItemGraphStyleBar = new JCheckBoxMenuItem("Bar");
	JCheckBoxMenuItem menuItemGraphStylePlot = new JCheckBoxMenuItem("Plot");
	
	// menuMode
	JCheckBoxMenuItem menuItemDifferentialMode = new JCheckBoxMenuItem("Diff");
	JCheckBoxMenuItem menuItemTotal = new JCheckBoxMenuItem("Total...");
	JCheckBoxMenuItem menuItemAutoUpdate = new JCheckBoxMenuItem("AutoUpdate...");
	JMenuItem menuItemRepaint = new JMenuItem("Refresh");

	// panelUnit
	JCheckBox checkTotal = new JCheckBox("Total...");
	JCheckBox checkAutoUpdate = new JCheckBox("AutoUpdate...");
	JCheckBox checkGraphStyleLine = new JCheckBox("Line");
	JCheckBox checkGraphStyleBar = new JCheckBox("Bar");
	JCheckBox checkGraphStylePlot = new JCheckBox("Plot");
	JCheckBox checkDifferentialMode = new JCheckBox("Diff");
	JCheckBox check8times = new JCheckBox("x8");
	JCheckBox check1000times = new JCheckBox("x1000");
	JCheckBox check100div = new JCheckBox("/100");
	
	// menuUnit
	public static String unitNone = "none";
	public static String unitbps = "bps";
	public static String unitKB = "KB";
	public static String unitLoad = "Load Average";
	ButtonGroup radioGroup = new ButtonGroup();
	JRadioButtonMenuItem radioMenuNone = new JRadioButtonMenuItem(unitNone);
	JRadioButtonMenuItem radioMenubps = new JRadioButtonMenuItem(unitbps);
	JRadioButtonMenuItem radioMenuKB = new JRadioButtonMenuItem(unitKB);
	JRadioButtonMenuItem radioMenuLoad = new JRadioButtonMenuItem(unitLoad);
	JCheckBoxMenuItem menuItem8times = new JCheckBoxMenuItem("x8");
	JCheckBoxMenuItem menuItem1000times = new JCheckBoxMenuItem("x1000");
	JCheckBoxMenuItem menuItem100div = new JCheckBoxMenuItem("/100");
	
	// panelController
	JComboBox comboUnit = new JComboBox();
	JComboBox comboHostname = new JComboBox();
	JComboBox comboEvent = new JComboBox();
	JLabel labelBeginDate = new JLabel("Begin Time");
	JLabel labelRange = new JLabel("Time Range");
	JLabel labelSecond = new JLabel("Sec.");
	JTextField textBeginTime = new JTextField(10);
	JTextField textRange = new JTextField(10);
	JButton buttonUpdateNow = new JButton("Refresh");
	JButton buttonFileOpen = new JButton("Open Files...");

	// export to be controlled component.
	JMenuBar menubar = new JMenuBar();
	GraphMonitor graphPane = new GraphMonitor();
	JPanel panelGraphStyle = new JPanel();
	JPanel panelUnit = new JPanel();	
	JPanel panelController = new JPanel();
	JPanel panelGraphMode = new JPanel();	
	JPanel panelAll = new JPanel();

	JPanel panelInformation = new JPanel();
	//JTextArea txtInfo = new JTextArea(5, 20);
	//JEditorPane txtInfo = new JEditorPane();
	JTextPane txtInfo = new JTextPane();
	JScrollPane txtInfoScroll;
	
	class GloggerDataFileFilter extends FileFilter
	{
		/* (non-Javadoc)
		 * @see javax.swing.filechooser.FileFilter#accept(java.io.File)
		 */
		public boolean accept(File f) {
			if(f.isDirectory())return true;
			return f.getName().endsWith(".glg");
		}

		/* (non-Javadoc)
		 * @see javax.swing.filechooser.FileFilter#getDescription()
		 */
		public String getDescription() {
			return "GLogger3 measurement datafile (.glg)"; 
		}
		
	}
	
	class UnitCellRenderer extends JLabel implements ListCellRenderer {
		public UnitCellRenderer() {
			setOpaque(true);
		}
		public Component getListCellRendererComponent(
		JList list,
		Object value,
		int index,
		boolean isSelected,
		boolean cellHasFocus)
		{
			setText(((JRadioButtonMenuItem)value).getText());
			setBackground(isSelected ? Color.blue : Color.white);
			setForeground(isSelected ? Color.white : Color.black);
			return this;
		}
	}
	
	class HostOidCellRenderer extends JLabel implements ListCellRenderer {
		public HostOidCellRenderer() {
			setOpaque(true);
		}
		public Component getListCellRendererComponent(
		JList list,
		Object value,
		int index,
		boolean isSelected,
		boolean cellHasFocus)
		{
			String v = (String) value;
			String name1, name2;
			try {
				name1 = getName1(v);
			} catch(Exception e){
				name1 = v;
			}
			try {
				name2 = getName2(v);
			} catch(Exception e){
				name2 = name1;
			}
			//setText(isSelected ? name2 : name1);
			setText(name1);
			list.setToolTipText(name2);

			setBackground(isSelected ? Color.blue : Color.white);
			setForeground(isSelected ? Color.white : Color.black);
			return this;
		}
		
	}
	
	protected String getName1(String v){
		return v.substring(0, v.indexOf('#'));
	}
	
	protected String getName2(String v){
		return v.substring(v.indexOf('#') + 1);
	}
	
	public void setToolTips(){
		String ls = System.getProperty("line.separator");
		
		// menubar
		menuItemFileOpen.setToolTipText("Choose data files of GLogger3");
		menuItemFileAutoUpdate.setToolTipText("Latest graph is displayed automatically");
		menuGraphStyle.setToolTipText("Graph Style");
		menuItem8times.setToolTipText("Byte -> bit");
		menuItemDifferentialMode.setToolTipText("The rate of increase (Per second)");
		menuItemTotal.setToolTipText("Choose Host Names for Total");
		menuItemAutoUpdate.setToolTipText(menuItemFileAutoUpdate.getToolTipText());
		menuItemRaw.setToolTipText("Do not resample");
		menuItemInterpolate.setToolTipText("v=v0+(v1-v0)/(t1-t0)*(t-t0), t0<t<=t1");
		menuItemBefore.setToolTipText("v=v0, t0<=t<t1");
		menuItemRepaint.setToolTipText("Refresh graph");
		
		// graph
		graphPane.setToolTipText("Left double click: x2, Right single click: /2, Right double click: Reset");
		
		txtInfo.setToolTipText("Information");
		
		//comboUnit.setToolTipText("");
		comboHostname.setToolTipText("Host Names");
		comboEvent.setToolTipText("Event Nick Names (correspond OIDs)");
		
		check8times.setToolTipText(menuItem8times.getToolTipText());
		checkTotal.setToolTipText(menuItemTotal.getToolTipText());
		checkDifferentialMode.setToolTipText(menuItemDifferentialMode.getToolTipText());
		checkAutoUpdate.setToolTipText(menuItemFileAutoUpdate.getToolTipText());
		
		textBeginTime.setToolTipText("Set \"yyyy/MM/dd HH:mm:ss\" and click Refresh");
		textRange.setToolTipText("Set range (second) to display and click Refresh");
		buttonFileOpen.setToolTipText(menuItemFileOpen.getToolTipText());
		buttonUpdateNow.setToolTipText(menuItemRepaint.getToolTipText());
	}
	
	public SimpleGrapherBaseUI()
	{
		setToolTips();
		
		// preparing file opening dialog.
		{
			dialogFileOpen.setFileFilter(new GloggerDataFileFilter());
			//dialogFileOpen.setFileSelectionMode(JFileChooser.FILES_AND_DIRECTORIES);
			dialogFileOpen.setMultiSelectionEnabled(true);
			
			dialogDirOpen.setFileSelectionMode(JFileChooser.DIRECTORIES_ONLY);
			dialogDirOpen.setMultiSelectionEnabled(false);
		}
		
		// preparing menubar.
		{
			menuFile.add(menuItemFileOpen);
			menuFile.add(menuItemFileAutoUpdate);
			menuFile.addSeparator();
			menuFile.add(menuItemFileExit);

			menuGraphStyle.add(menuItemGraphStyleLine);
			menuGraphStyle.add(menuItemGraphStyleBar);
			menuGraphStyle.add(menuItemGraphStylePlot);
	
			Vector l = new Vector();
			l.add(radioMenuNone);
			l.add(radioMenubps);
			l.add(radioMenuKB);
			l.add(radioMenuLoad);
			menuUnit.add(radioMenuNone);
			menuUnit.add(radioMenubps);
			menuUnit.add(radioMenuKB);
			menuUnit.add(radioMenuLoad);
			radioGroup.add(radioMenuNone);
			radioGroup.add(radioMenubps);
			radioGroup.add(radioMenuKB);
			radioGroup.add(radioMenuLoad);
			menuUnit.addSeparator();
			menuUnit.add(menuItem8times);
			//menuUnit.add(menuItem1000times);
			//menuUnit.add(menuItem100div);
			radioMenuNone.setSelected(true);

//System.out.println("size = " + l.size());
			DefaultComboBoxModel model = new DefaultComboBoxModel();
			JRadioButtonMenuItem[] tmp = new JRadioButtonMenuItem[l.size()];
			JRadioButtonMenuItem[] s = (JRadioButtonMenuItem[]) l.toArray(tmp);
			for(int i=0; i < l.size(); i++){
				model.addElement(s[i]);
				
			}
			model.setSelectedItem(s[0]);
			UnitCellRenderer renderer = new UnitCellRenderer();
			comboUnit.setRenderer(renderer);
			comboUnit.setModel(model);
			
			menuMode.add(menuItemRepaint);
			menuMode.addSeparator();
			menuMode.add(menuItemDifferentialMode);
			menuMode.add(menuItemTotal);
			menuMode.add(menuItemAutoUpdate);
			
			resampleGroup.add(menuItemRaw);
			resampleGroup.add(menuItemBefore);
			resampleGroup.add(menuItemInterpolate);
			subMenuResample.add(menuItemRaw);
			subMenuResample.add(menuItemInterpolate);
			subMenuResample.add(menuItemBefore);
			menuItemRaw.setSelected(true);
			subMenuResample.addSeparator();
			subMenuResample.add(menuItemResolution);
			
			menuConf.add(subMenuResample);
			menuConf.add(menuItemAutoUpdateInterval);
			menuConf.add(menuItemCorrectByUptime);
			menuConf.addSeparator();
			menuConf.add(menuItemVisInfo);
			menuConf.add(menuItemVisGraphStyle);
			menuConf.add(menuItemVisUnit);
			menuConf.add(menuItemVisController);

			menubar.add(menuFile);
			menubar.add(menuGraphStyle);
			menubar.add(menuUnit);
			menubar.add(menuMode);
			menubar.add(menuConf);
			//menubar.add(Box.createHorizontalGlue());
		}

		// preparing graph.
		// Nothing to do.
		
		// preparing panel for information
		{
			//GridBagLayout layoutManager = new GridBagLayout();
			panelInformation.setLayout(new BorderLayout());
			//panelInformation.setBorder(new TitledBorder("Information"));
			txtInfo.setMargin(new Insets(0, 5, 0, 5));
			txtInfo.setEditable(false);
			//txtInfo.setBackground(panelInformation.getBackground());
			//txtInfoScroll = new JScrollPane(txtInfo, JScrollPane.VERTICAL_SCROLLBAR_AS_NEEDED, JScrollPane.HORIZONTAL_SCROLLBAR_AS_NEEDED);
			txtInfoScroll = new JScrollPane(txtInfo);
			txtInfoScroll.setMinimumSize(new Dimension(300, 80));
			panelInformation.add(txtInfoScroll, BorderLayout.CENTER);
		}

		// preparing panel of graph style.
		{
			panelGraphStyle.setBorder(new TitledBorder("Graph Style"));
			BoxLayout layout = new BoxLayout(panelGraphStyle, BoxLayout.X_AXIS);
			panelGraphStyle.setLayout(layout);
			panelGraphStyle.add(checkGraphStyleLine);
			panelGraphStyle.add(checkGraphStyleBar);
			panelGraphStyle.add(checkGraphStylePlot);
			panelUnit.setBorder(new TitledBorder("Unit"));
			//comboUnit.setBackground(Color.WHITE);
			BoxLayout layout2 = new BoxLayout(panelUnit, BoxLayout.X_AXIS);
			panelUnit.setLayout(layout2);
			panelUnit.add(comboUnit);
			panelUnit.add(check8times);
			//panelUnit.add(check1000times);
			//panelUnit.add(check100div);
		}

		// preparing panel of controller.
		{
			HostOidCellRenderer renderer = new HostOidCellRenderer();
			comboHostname.setRenderer(renderer);
			comboEvent.setRenderer(renderer);

			GridBagLayout layoutManager = new GridBagLayout();
			panelController.setLayout(layoutManager);
			GridBagConstraints c = new GridBagConstraints();
			c.fill = GridBagConstraints.HORIZONTAL;
			c.gridx = GridBagConstraints.RELATIVE;
			c.gridy = 0;
			c.gridwidth = GridBagConstraints.RELATIVE;
			c.weightx = 1.0;
			//comboHostname.setBackground(Color.WHITE);
			panelController.add(comboHostname, c);
			c.weightx = 0.0;
			panelGraphMode.add(checkTotal);
			panelGraphMode.add(checkDifferentialMode);
			c.gridwidth = GridBagConstraints.REMAINDER;
			panelController.add(panelGraphMode, c);
			c.gridy = 1;
			c.weightx = 1.0;
			c.gridwidth = GridBagConstraints.RELATIVE;
			//comboEvent.setBackground(Color.WHITE);
			panelController.add(comboEvent, c);
			c.weightx = 0.0;
			c.gridwidth = GridBagConstraints.REMAINDER;
			panelController.add(checkAutoUpdate, c);
			c.gridwidth = 3;
			c.gridy = 2;
			panelController.add(labelBeginDate, c);
			c.weightx = 1.0;
			c.gridwidth = GridBagConstraints.RELATIVE;
			panelController.add(textBeginTime, c);
			c.weightx = 0.0;
			c.gridwidth = GridBagConstraints.REMAINDER;
			panelController.add(buttonFileOpen, c);
			c.gridy = 3;
			c.gridwidth = 3;
			panelController.add(labelRange, c);
			c.weightx = 1.0;
			c.gridwidth = GridBagConstraints.RELATIVE;
			panelController.add(textRange, c);
			c.weightx = 0.0;
			c.gridwidth = GridBagConstraints.REMAINDER;
			panelController.add(buttonUpdateNow, c);
		}
		
		// nested layout all panel.
		{
			GridBagConstraints c = new GridBagConstraints();
			panelAll.setLayout(new GridBagLayout());
			c.fill = GridBagConstraints.BOTH;
			c.gridwidth = 2;
			c.gridy = 0;
			c.weightx = 1.0;
			c.weighty = 1.0;
			graphPane.setPreferredSize(new Dimension(400, 220));
			//graphPane.setMinimumSize(new Dimension(100, 100));
			panelAll.add(graphPane, c);
			c.weightx = 0.0;
			c.weighty = 0.0;

			c.fill = GridBagConstraints.HORIZONTAL;
			c.gridy++;
			c.gridwidth = 2;
			c.ipadx = 10;
			panelAll.add(panelInformation, c);

			c.gridy++;
			c.gridwidth = 1;
			panelAll.add(panelGraphStyle, c);
			panelAll.add(panelUnit, c);
			c.gridy++;
			c.gridwidth = 2;
			panelAll.add(panelController, c);
		} 	
	}
	
	/**
	 * get menubar of SimpleGrapher application.
	 * @return menubar of SimpleGrapher.
	 */
	public JMenuBar getMenubar() {
		return menubar;
	}

	/**
	 * get operation/style panel of SimpleGrapher application.
	 * This panel contain components that getPanelController() and getPanelGraphStyle() returns.
	 * @return JPanel that contains controller and style.
	 */
	public JPanel getPanelAll() {
		return panelAll;
	}

	/**
	 * get operation panel of SimpleGrapher application.
	 * @return operation panel.
	 */
	public JPanel getPanelController() {
		return panelController;
	}

	/**
	 * get Information panel of SimpleGrapher application.
	 * @return style panel.
	 */
	public JPanel getPanelInformation() {
		return panelInformation;
	}

	/**
	 * get style panel of SimpleGrapher application.
	 * @return style panel.
	 */
	public JPanel getPanelGraphStyle() {
		return panelGraphStyle;
	}

	/**
	 * get Unit panel of SimpleGrapher application.
	 * @return style panel.
	 */
	public JPanel getPanelUnit() {
		return panelUnit;
	}

	/* TINY TEST */
	public static void main(String args[])
	{
		SimpleGrapherBaseUI s = new SimpleGrapherBaseUI();
		JFrame f = new JFrame();		
		//f.setSize(200, 500);
		f.setJMenuBar(s.menubar);
		Container c = f.getContentPane();
		c.add(s.panelAll);
/*
		try {
			UIManager.setLookAndFeel("com.sun.java.swing.plaf.windows.WindowsLookAndFeel");
			SwingUtilities.updateComponentTreeUI(f);
		} catch (ClassNotFoundException e) {
			// Windows-like UI is not supported, ignore.
		} catch (InstantiationException e) {
			// Windows-like UI is not supported, ignore.
		} catch (IllegalAccessException e) {
			// Cannot change UI, ignore.
		} catch (UnsupportedLookAndFeelException e) {
			// Windows-like UI is not supported, ignore.
		}
*/		
		f.setVisible(true);
		f.pack();
		f.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
	}
	/**
	 * @return
	 */
	public GraphMonitor getGraphPane() {
		return graphPane;
	}
	protected double getDoubleWithDefault(String key, double def)
	{
		String val = System.getProperty(key);
		if(val == null){
			return def;
		}
		return Double.parseDouble(val);
	}
	protected boolean getBooleanWithDefault(String key, boolean def)
	{
		String val = System.getProperty(key);
		if(val == null){
			return def;
		}
		if(val.equalsIgnoreCase("true") == true){
			return true;
		}
		return false;
	}
	protected ArrayList extractCSV(String str)
	{
		if(str == null){
			return null;
		}
		ArrayList list = new ArrayList();
		int a = 0;
		boolean statCapturing = false;
		for(int i = 0; i < str.length(); i++){
			char ch = str.charAt(i);
			if(statCapturing == true){
				if(ch == ','){
					// capturing a to i-1.
					String elm = str.substring(a, i - 1);
					statCapturing = false;
					a = -1;
					list.add(elm);
				}else{
					// nothing to do.
				}
			}else{
				if(ch == ','){
					// capturing next.
					a = i + 1;
					statCapturing = true;
				}else if(ch == ' ' || ch == '\t'){
					// skip, nothing to do.
				}else{
					// start capturing.
					a = i;
					statCapturing = true;
				}
			}
		}
		return list;
	}

}
