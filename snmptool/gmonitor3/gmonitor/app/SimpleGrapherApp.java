/*
 * Created on 2003/07/14
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.app;

import gmonitor.gui.DataTimeSpace;
import gmonitor.gui.GraphMonitor;
import gmonitor.gui.GraphMonitorModel;
import gmonitor.gui.RawData;
import gmonitor.gui.RawDataElement;

import java.awt.BorderLayout;
import java.awt.Container;
import java.awt.Dimension;
import java.awt.DisplayMode;
import java.awt.GraphicsEnvironment;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.MouseEvent;
import java.awt.event.MouseListener;
import java.io.File;
import java.io.FileFilter;
import java.io.IOException;
import java.text.DateFormat;
import java.text.DecimalFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;

import javax.swing.BorderFactory;
import javax.swing.DefaultComboBoxModel;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JDialog;
import javax.swing.JFileChooser;
import javax.swing.JFrame;
import javax.swing.JList;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JRadioButtonMenuItem;
import javax.swing.JScrollPane;
import javax.swing.Timer;
import javax.swing.text.Document;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class SimpleGrapherApp extends SimpleGrapherBaseUI {
	String currentDirectoryPath = null;
	String setTargetHostName = null;
	String setTargetEventName = null;
	String setTargetUnitName = null;
	int initialDelay = 0;
	
	String[] selectedHostsForTotal = null;
	DataTimeSpace dtSpace = null;
	ArrayList rawDataSeries = new ArrayList();
	GraphMonitorModel model = new GraphMonitorModel();
	
	private int autoUpdateInterval = 10; // second
	Timer timer = new Timer(autoUpdateInterval * 1000,
		new TimerAutoUpdateAction());
	private boolean timerInitFlag;

	private int resampleResolution = 100;  // second

	File selectedDir = null; // for use of Auto updating.

	JFrame appFrame = new JFrame();
	SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy/MM/dd HH:mm:ss");

	String propDataPathPrefix = null;
	ArrayList propDataFileList = new ArrayList(); // String[]
	ArrayList propHostList = new ArrayList(); // String[]
	String propEvent = null;
	boolean propTotalMode = false;
	boolean propDiffMode = false; // direct.
	boolean propAutoUpdate = false;
	boolean propLineStyle = true;
	boolean propBarStyle = true;
	boolean propPlotStyle = true;
	double  propMultiplier = 1.0;
	boolean propControllerVisibility = true;
	boolean propStylePanelVisibility = true;
	boolean propUnitPanelVisibility = true;
	boolean propMenuBarVisibility = true;
	boolean propInformationPanelVisibility = true;
	
	private double savedInfoWidth = 0;
	private double savedInfoHeight = 0;
	private Dimension savedDimension = null;
	private Dimension savedDimension2 = null;

	private long magnificationY = 1;  // Y scale

	private boolean disableComboEvent = false;    // do not refresh
	private boolean setComboTargetsFlag = false;  // do not refresh in setComboTargets()

/*
	private static final String[] UNIT_PREFIX_TABLE = new String[]{
		"", //10^0
		"K",// 10^3
		"M",// 10^6
		"G",// 10^9
		"T",// 10^12
		"P",// 10^15
		"E",// 10^18
		"Z",// 10^21
		"Y",// 10^24
	};
	
*/
	private static final String[] UNIT_PREFIX_TABLE = new String[]{
		"", 
		"x 10^3",
		"x 10^6",
		"x 10^9",
		"x 10^12",
		"x 10^15",
		"x 10^18",
		"x 10^21",
		"x 10^24"
	};

	private void initializeFromProperty(String pref)
	{
		ArrayList propDataFileList = extractCSV(System.getProperty(pref + "DataFileList"));
		ArrayList propHostList = extractCSV(System.getProperty(pref + "HostList"));
		propTotalMode = getBooleanWithDefault(pref + "TotalMode", propTotalMode);
		propDiffMode  = getBooleanWithDefault(pref + "DiffMode",  propDiffMode);
		propAutoUpdate= getBooleanWithDefault(pref + "AutoUpdate",propAutoUpdate);
		propLineStyle = getBooleanWithDefault(pref + "LineStyle", propLineStyle);
		propBarStyle  = getBooleanWithDefault(pref + "BarStyle",  propBarStyle);
		propPlotStyle = getBooleanWithDefault(pref + "PlotStyle", propPlotStyle);
		propMultiplier =getDoubleWithDefault(pref  + "Multiplier",propMultiplier);
		propControllerVisibility = getBooleanWithDefault(pref + "ControllerVisibility",
			propControllerVisibility);
		propStylePanelVisibility = getBooleanWithDefault(pref + "StylePanelVisibility",
		 	propStylePanelVisibility);
		propMenuBarVisibility = getBooleanWithDefault(pref + "MenuBarVisibility",
			propMenuBarVisibility);
	}

	private void initializeEventHandlers()
	{
		MenuItemFileOpenAction a = new MenuItemFileOpenAction();
		menuItemFileOpen.addActionListener(a);
		buttonFileOpen.addActionListener(a);
		menuItemFileAutoUpdate.addActionListener(new FileAutoUpdateAction());
		menuItemFileExit.addActionListener(new MenuItemFileExitAction());
		buttonUpdateNow.addActionListener(new RepaintAction());
		comboEvent.addActionListener(new ComboEventSelectionAction());
		comboHostname.addActionListener(new ComboHostnameSelectionAction());
		checkGraphStyleBar.addActionListener(new CheckBoxBarAction());
		checkGraphStyleLine.addActionListener(new CheckBoxLineAction());
		checkGraphStylePlot.addActionListener(new CheckBoxPlotAction());
		checkDifferentialMode.addActionListener(new CheckBoxDifferentialModeAction());
		check8times.addActionListener(new CheckBox8timesAction());
		check1000times.addActionListener(new CheckBox1000timesAction());
		check100div.addActionListener(new CheckBox100divAction());
		checkTotal.addActionListener(new CheckBoxTotalAction());
		checkAutoUpdate.addActionListener(new CheckBoxAutoUpdateAction());
		menuItemGraphStyleBar.addActionListener(new MenuItemGraphStyleBarAction());
		menuItemGraphStyleLine.addActionListener(new MenuItemGraphStyleLineAction());
		menuItemGraphStylePlot.addActionListener(new MenuItemGraphStylePlotAction());
		menuItemDifferentialMode.addActionListener(new MenuItemDifferentialModeAction());
		menuItem8times.addActionListener(new MenuItem8timesAction());
		menuItem1000times.addActionListener(new MenuItem1000timesAction());
		menuItem100div.addActionListener(new MenuItem100divAction());
		menuItemTotal.addActionListener(new MenuItemTotalAction());
		menuItemAutoUpdate.addActionListener(new MenuItemAutoUpdateAction());
		menuItemCorrectByUptime.addActionListener(new CorrectByUptimeAction());
		
		RadioAction ra = new RadioAction();
		radioMenuNone.addActionListener(ra);
		radioMenubps.addActionListener(ra);
		radioMenuKB.addActionListener(ra);
		radioMenuLoad.addActionListener(ra);
		comboUnit.addActionListener(new ComboUnitSelectionAction());
		
		menuItemAutoUpdateInterval.addActionListener(new AutoUpdateIntervalAction());
		menuItemAutoUpdateInterval.setText(menuStrAutoUpdateInterval + " ("+String.valueOf(autoUpdateInterval)+")");
		menuItemResolution.addActionListener(new ResampleResoAction());
		menuItemResolution.setText(menuStrResolution + " ("+String.valueOf(resampleResolution)+")");
		menuItemRepaint.addActionListener(new RepaintAction());

		RepaintNow rn = new RepaintNow();
		menuItemRaw.addActionListener(rn);
		ResampleAction resampleAct = new ResampleAction(); 
		menuItemBefore.addActionListener(resampleAct);
		menuItemInterpolate.addActionListener(resampleAct);
		
		// visible
		menuItemVisInfo.addActionListener(new VisibleInfoAction());
		menuItemVisInfo.setSelected(true);
		menuItemVisGraphStyle.addActionListener(new VisibleStyleAction());
		menuItemVisGraphStyle.setSelected(true);
		menuItemVisUnit.addActionListener(new VisibleUnitAction());
		menuItemVisUnit.setSelected(true);
		menuItemVisController.addActionListener(new VisibleControllerAction());
		menuItemVisController.setSelected(true);
		
		graphPane.addMouseListener(new GraphMouseAction());
	}
	
	private void initializeGUIComponents()
	{
		appFrame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
		textBeginTime.setText(dateFormat.format(new Date()));
		textRange.setText("3600");
		checkAutoUpdate.setSelected(propAutoUpdate);
		checkTotal.setSelected(propTotalMode);
		checkGraphStyleBar.setSelected(propBarStyle);
		menuItemGraphStyleBar.setSelected(propBarStyle);
		checkGraphStylePlot.setSelected(propPlotStyle);
		menuItemGraphStylePlot.setSelected(propPlotStyle);
		checkGraphStyleLine.setSelected(propLineStyle);
		menuItemGraphStyleLine.setSelected(propLineStyle);
	}
	
	private String selectedUptimeEventForCorrection;
	private JComboBox comboUptimeSelection = null;
	private void actionCorrect(boolean b){
		selectedUptimeEventForCorrection = null;
		if(b){
			if(dtSpace == null){
				menuItemCorrectByUptime.setSelected(false);
				return;
			}

			JDialog dialog = new JDialog(appFrame, "select an event of uptime", true);
			JPanel panel = new JPanel();
			panel.setLayout(new BorderLayout());
			comboUptimeSelection = new JComboBox();
			comboUptimeSelection.setRenderer(new HostOidCellRenderer());

			//defineNewComboBox(comboUptimeSelection, dtSpace.getEvents());
			DefaultComboBoxModel emodel = (DefaultComboBoxModel) comboEvent.getModel();
			DefaultComboBoxModel cmodel = (DefaultComboBoxModel) comboUptimeSelection.getModel();			
			int count = emodel.getSize();
			for(int i=0; i < count; i++){
				cmodel.addElement(emodel.getElementAt(i));
			}
			//comboUptimeSelection.setModel(cmodel);
			
			for(int i=0; i < comboUptimeSelection.getItemCount(); i++){
	  			Object sel = comboUptimeSelection.getItemAt(i);
				if(((String)sel).startsWith("uptime")){
					comboUptimeSelection.setSelectedItem(sel);
					break;
				}
			}
			
			JButton ok = new JButton("OK");
			ok.addActionListener(new ActionListener() {
				public void actionPerformed(ActionEvent e) {
					selectedUptimeEventForCorrection = (String) comboUptimeSelection.getSelectedItem();
					((JButton) e.getSource()).getTopLevelAncestor().setVisible(false);
					paintGraphAccordingToCurrentGUIStatus();
//System.out.println("CorrectByUptime: " + selectedUptimeEventForCorrection);
				}
			});
			JButton cancel = new JButton("Cancel");
			cancel.addActionListener(new ActionListener() {
				public void actionPerformed(ActionEvent e) {
					((JButton) e.getSource()).getTopLevelAncestor().setVisible(false);
				}
			});
			panel.add(comboUptimeSelection, BorderLayout.NORTH);
			panel.add(ok, BorderLayout.CENTER);
			panel.add(cancel, BorderLayout.EAST);
			panel.setBorder(BorderFactory.createEmptyBorder(10, 10, 10, 10));
			dialog.setContentPane(panel);
			dialog.pack();
			dialog.setLocationRelativeTo(appFrame);
			dialog.setVisible(true);
			
			if(selectedUptimeEventForCorrection == null){
				menuItemCorrectByUptime.setSelected(false);
			}
		} else {
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	
	class CorrectByUptimeAction implements ActionListener
	{
		public void actionPerformed(ActionEvent ac) {
			actionCorrect(menuItemCorrectByUptime.isSelected());
		}
	}	
	
	class GraphMouseAction implements MouseListener {
		private boolean rightFlag;
		int click;
		public void mouseClicked(MouseEvent e) {
		}

		public void mouseEntered(MouseEvent e) {
		}

		public void mouseExited(MouseEvent e) {
		}

		public void mousePressed(MouseEvent e) {
			click = e.getClickCount();
			if (click >= 2){
//System.out.println("click = " + click);
			}

			rightFlag = false;
			// Linux
			event(e);
		}

		public void mouseReleased(MouseEvent e) {
			// Windows
			event(e);
			if(rightFlag == false && click >= 2){
//System.out.println("x2");
				magnificationY *= 2;
				paintGraphAccordingToCurrentGUIStatus();
			}
		}
		
		private void event(MouseEvent e){
			if (e.isPopupTrigger() && rightFlag == false) {
				if (click == 1){
//System.out.println("/2");
					magnificationY /= 2;
					if(magnificationY <= 0){
						magnificationY = 1;
					}
				} else if (click == 2){
//System.out.println("Reset");
					magnificationY = 1;  // reset
				}
				paintGraphAccordingToCurrentGUIStatus();
				rightFlag = true;
			}
		}
	}
	
	class ResampleAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			dtSpace.initResampingMode();
			paintGraphAccordingToCurrentGUIStatus();
		}
	}

	class VisibleInfoAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			panelInformation.setVisible(menuItemVisInfo.isSelected());
			//paintGraphAccordingToCurrentGUIStatus();
		}
	}

	class VisibleStyleAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			panelGraphStyle.setVisible(menuItemVisGraphStyle.isSelected());
			//paintGraphAccordingToCurrentGUIStatus();
		}
	}

	class VisibleUnitAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			panelUnit.setVisible(menuItemVisUnit.isSelected());
			//paintGraphAccordingToCurrentGUIStatus();
		}
	}

	class VisibleControllerAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			panelController.setVisible(menuItemVisController.isSelected());
			//paintGraphAccordingToCurrentGUIStatus();
		}
	}

	class RadioAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
//System.out.println("RadioAction");
			if(((JRadioButtonMenuItem) arg0.getSource()).equals(radioMenubps)){
				menuItem8times.setSelected(true);
				check8times.setSelected(true);
				menuItemDifferentialMode.setSelected(true);
				checkDifferentialMode.setSelected(true);
			} else {
				menuItem8times.setSelected(false); 
				check8times.setSelected(false);
				menuItemDifferentialMode.setSelected(false);
				checkDifferentialMode.setSelected(false);
			}
			comboUnit.setSelectedItem((JRadioButtonMenuItem) arg0.getSource());
			//paintGraphAccordingToCurrentGUIStatus();
		}
	}
	
	class AutoUpdateIntervalAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			if(checkAutoUpdate.isSelected()){
				timer.stop();
			}
			String interval =
				JOptionPane.showInputDialog(
					appFrame,
					"AutoUpdate Interval (second)",
					String.valueOf(autoUpdateInterval));
			try {
				if(interval != null){
//System.out.println("AutoUpdateInterval " + interval);
					int tmp = Integer.parseInt(interval);
					if(tmp <= 0){
						return;
					}
					autoUpdateInterval = tmp;
					menuItemAutoUpdateInterval.setText(menuStrAutoUpdateInterval + " ("+interval+")");
					timer.setDelay(autoUpdateInterval * 1000);
				}
			} catch(Exception e){
			}
			if(checkAutoUpdate.isSelected()){
				timer.start();
			}
		}
	}

	class ResampleResoAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			String interval =
				JOptionPane.showInputDialog(
					appFrame,
					"Resampling Resolution (second)",
					String.valueOf(resampleResolution));
			try {
				if(interval != null){
//System.out.println("ResampleResolution " + interval);
					int tmp = Integer.parseInt(interval);
					if(tmp <= 0){
						return;
					}
					resampleResolution = tmp;
					menuItemResolution.setText(menuStrResolution + " ("+interval+")");
					paintGraphAccordingToCurrentGUIStatus();
				}
			} catch(Exception e){
			}
		}
	}
	
	class ComboUnitSelectionAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			((JRadioButtonMenuItem)comboUnit.getSelectedItem()).setSelected(true);
			((JRadioButtonMenuItem)comboUnit.getSelectedItem()).doClick();
			if(setComboTargetsFlag == true){
				return;
			}
			paintGraphAccordingToCurrentGUIStatus();
		}
	}

	class CheckBoxBarAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			menuItemGraphStyleBar.setSelected(checkGraphStyleBar.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class CheckBoxLineAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			menuItemGraphStyleLine.setSelected(checkGraphStyleLine.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class CheckBoxPlotAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			menuItemGraphStylePlot.setSelected(checkGraphStylePlot.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class CheckBoxDifferentialModeAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			menuItemDifferentialMode.setSelected(checkDifferentialMode.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class CheckBox8timesAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			menuItem8times.setSelected(check8times.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class CheckBox1000timesAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			menuItem1000times.setSelected(check1000times.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class CheckBox100divAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			menuItem100div.setSelected(check100div.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	
	private JDialog jd;
	private JList list;
	private boolean totalOkFlag;
	private void actionTotal(boolean b){
		totalOkFlag = false;
		
		boolean timerFlag;
		if(timer.isRunning()){
			timer.stop();
			timerFlag = true;
		} else {
			timerFlag = false;
		}
		
		if(b == true){
			jd = new JDialog(appFrame, "select hosts", true);
			JPanel jp = new JPanel();
			jp.setLayout(new BorderLayout());
			ArrayList hostList = new ArrayList();
			DefaultComboBoxModel cmbmodel = (DefaultComboBoxModel) comboHostname.getModel();
			int count = cmbmodel.getSize();
			if(count <= 1){
				checkTotal.setSelected(false);
				menuItemTotal.setSelected(false);
				comboHostname.setEnabled(true);
				return;
			}
			for(int i = 0; i < count; i++){
				String host = (String) cmbmodel.getElementAt(i);
				hostList.add(host);
				//hostList.add("****************************");
			}
			String[] tmp = new String[hostList.size()];
			String[] listData = (String[]) hostList.toArray(tmp);
			list = new JList(listData);
			JButton ok = new JButton("OK");
			list.setCellRenderer(new HostOidCellRenderer());
			
			ok.addActionListener(new ActionListener() {
				public void actionPerformed(ActionEvent e) {
					Object[] h = list.getSelectedValues();
					if(h == null || h.length <= 1){
						return;
					}
					selectedHostsForTotal = new String[h.length];
					System.out.println("--- Total ---");
					for(int i=0; i<h.length; i++){
						selectedHostsForTotal[i] = (String) h[i];
						System.out.println(h[i]);
					}
					
					jd.setVisible(false);
					jd = null;
					totalOkFlag = true;
				}
			});
			JButton cancel = new JButton("Cancel");
			cancel.addActionListener(new ActionListener() {
				public void actionPerformed(ActionEvent e) {
					jd.setVisible(false);
					jd = null;
					totalOkFlag = false;
				}
			});
			jp.add(new JScrollPane(list), BorderLayout.NORTH);
			jp.add(ok, BorderLayout.CENTER);
			jp.add(cancel, BorderLayout.EAST);
			jp.setBorder(BorderFactory.createEmptyBorder(10, 10, 10, 10));
			jd.setContentPane(jp);
			jd.pack();
			jd.setLocationRelativeTo(appFrame);
			jd.setVisible(true);
		}
		if(totalOkFlag == true){
			comboHostname.setEnabled(false);
		} else{
			checkTotal.setSelected(false);
			menuItemTotal.setSelected(false);
			comboHostname.setEnabled(true);
		}
		if(timerFlag == true){
			timer.start();
		} else if(totalOkFlag == true){
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	
	class CheckBoxTotalAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			JCheckBox c = checkTotal;
			menuItemTotal.setSelected(c.isSelected());
			actionTotal(c.isSelected());
		}
	}
	
	private void actionAutoUpdate(boolean check, boolean choice){
		if(check == true){
			if(choice == true) {
				initialDelay = -1;
				dialogDirOpen.setDialogTitle("Choose the directory where .glg files are contained");
				if(currentDirectoryPath != null){
					String path;
					try {
						File f = new File(currentDirectoryPath);
						if(f.exists()){
							path = f.getCanonicalPath();
						} else {
							path = "";
						}
					} catch (IOException e) {
						path = currentDirectoryPath;
					}
//System.out.println(path);
					dialogDirOpen.setCurrentDirectory(new File(path));
				}
				int ret = dialogDirOpen.showOpenDialog(appFrame);
				if(ret == JFileChooser.APPROVE_OPTION){
					selectedDir = dialogDirOpen.getSelectedFile();
					//System.out.println(selectedDir.getPath());
					currentDirectoryPath = selectedDir.getPath();
				}
				else {
					menuItemAutoUpdate.setSelected(false);
					checkAutoUpdate.setSelected(false);
					timer.stop();
					return;
				}
			} else {
				selectedDir = new File(currentDirectoryPath);
				if(selectedDir.exists() == false){
					menuItemAutoUpdate.setSelected(false);
					checkAutoUpdate.setSelected(false);
					timer.stop();
					return;
				}
				//System.out.println(selectedDir.getPath());
			}

			if(selectedDir == null){
				// Cannot auto update because no data folder is selected.
				JOptionPane.showMessageDialog(appFrame,
					"No logdata is specified. Open logfile first.");
				//c.setEnabled(false);
				//c.setSelected(false);
				//c.setEnabled(true);
				menuItemAutoUpdate.setSelected(false);
				checkAutoUpdate.setSelected(false);
				timer.stop();
				return;
			}
			textBeginTime.setEnabled(false);
			//textRange.setEnabled(false);
			if(timer.isRunning()){
				timer.stop();
			}
			// start Swing timer to update.
			timerInitFlag = false;
			timer.start();
			return;
		}else{
			textBeginTime.setEnabled(true);
			textRange.setEnabled(true);
			// stop Swing timer.
			timer.stop();
			return;
		}		
	}
	
	class FileAutoUpdateAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			checkAutoUpdate.setSelected(true);
			menuItemAutoUpdate.setSelected(true);
			actionAutoUpdate(true, true);
		}
	}
	class CheckBoxAutoUpdateAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			JCheckBox c = checkAutoUpdate;
			menuItemAutoUpdate.setSelected(c.isSelected());
			actionAutoUpdate(c.isSelected(), true);
		}
	}
	
	private boolean lockTimer = false;
	class TimerAutoUpdateAction implements ActionListener
	{
		// This listener is invoked when the Swing timer was exceeded
		// the update interval.
		synchronized public void actionPerformed(ActionEvent arg0) {
		//public void actionPerformed(ActionEvent arg0) {
			if(lockTimer == true){
//System.out.println("Cancel: Timer");
				return;
			}
			lockTimer = true;
//System.out.println("AutoUpdate");
			// scan directory contents in selectedDir and rebuild
			// data-time space.
			File[] files = selectedDir.listFiles(new FileFilter(){
				public boolean accept(File f){
					if( (f.isFile() == true) && (f.getName().endsWith(".glg") == true) ){
						return true;
					}else{
						return false;
					}
				}
			});
			if(files == null){
				lockTimer = false;
				System.out.println("AutoUpdate: no .glg file");
				return;
			}
			String[] filenames = new String[files.length];
			for(int i = 0; i < files.length; i++){
				filenames[i] = files[i].getPath();
			}
			try {
				dtSpace = new DataTimeSpace(filenames);
				// hostname, event name, count of them should not be changed. 
				// obtain the latest time from date-time space, and back to past in range seconds.
				long t = dtSpace.getLatestDateTime();
				long r = Long.parseLong(textRange.getText()) * 1000L;
				String dt = dateFormat.format(new Date(t - r));
				textBeginTime.setText(dt);

				if(timerInitFlag == false){
					defineNewComboBox(comboHostname, dtSpace.getHostnames());
					defineNewComboBox(comboEvent, dtSpace.getEvents());
					setComboTargets();
					timerInitFlag = true;
				}

				// repaint graph according to GUI switches.
				paintGraphAccordingToCurrentGUIStatus();
			} catch (IOException e) {
				// cannot create data-time space.
				String msg = "File open error.";
				//JOptionPane.showMessageDialog(appFrame, msg);
				System.out.println(msg);				
				//e.printStackTrace();
				
//				checkAutoUpdate.setSelected(false);
//				menuItemTotal.setSelected(false);
//				actionAutoUpdate(false, false);
			} catch (NumberFormatException e) {
				System.out.println("invalid Time Range.");
			} catch (Exception e) {
//				System.out.println("");
				e.printStackTrace();
			}
			lockTimer = false;
		}
	}
	class MenuItemGraphStyleBarAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			checkGraphStyleBar.setSelected(menuItemGraphStyleBar.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class MenuItemGraphStyleLineAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			checkGraphStyleLine.setSelected(menuItemGraphStyleLine.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class MenuItemGraphStylePlotAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			checkGraphStylePlot.setSelected(menuItemGraphStylePlot.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class MenuItemDifferentialModeAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			checkDifferentialMode.setSelected(menuItemDifferentialMode.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class MenuItem8timesAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			check8times.setSelected(menuItem8times.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class MenuItem1000timesAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			check1000times.setSelected(menuItem1000times.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class MenuItem100divAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			check100div.setSelected(menuItem100div.isSelected());
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	class MenuItemTotalAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			checkTotal.setSelected(menuItemTotal.isSelected());
			actionTotal(menuItemTotal.isSelected());			
		}
	}
	class MenuItemAutoUpdateAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			checkAutoUpdate.setSelected(menuItemAutoUpdate.isSelected());
			actionAutoUpdate(menuItemAutoUpdate.isSelected(), true);
		}
	}

	/*
	 * Event selection is changed event handler of ComboBox.
	 * @author hkondo
	 */
	class ComboEventSelectionAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			if(setComboTargetsFlag == true){
				return;
			}
			setTargetEventName = (String) comboEvent.getSelectedItem();
			if(disableComboEvent == false){
				paintGraphAccordingToCurrentGUIStatus();
			}
		}
	}
	/*
	 * Hostname selection is changed event handler of ComboBox. 
	 * @author hkondo
	 */
	class ComboHostnameSelectionAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			if(setComboTargetsFlag == true){
				return;
			}
			setTargetHostName = (String) comboHostname.getSelectedItem();
			if(disableComboEvent == false){
				defineNewComboBox(comboEvent, dtSpace.getEvents());
				paintGraphAccordingToCurrentGUIStatus();
			}
		}
	}
	/*
	 * "Repaint" Event handler of Button.
	 * @author hkondo
	 */
	class RepaintAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			System.gc();
			defineNewComboBox(comboHostname, dtSpace.getHostnames());
			defineNewComboBox(comboEvent, dtSpace.getEvents());
			setComboTargets();
			if(menuItemAutoUpdate.isSelected()){
				timer.stop();
				timer.start();
			} else{
				paintGraphAccordingToCurrentGUIStatus();
			}
		}
	}
	
	class RepaintNow implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			paintGraphAccordingToCurrentGUIStatus();
		}
	}
	
	/*
	 * "File" - "Exit" Event handler of Menu item.
	 * @author hkondo
	 */
	class MenuItemFileExitAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			System.exit(0);
		}
	}

	private void defineNewComboBox(JComboBox box, String[] items)
	{
		DefaultComboBoxModel cmodel = new DefaultComboBoxModel();
		for(int i = 0; i < items.length; i++){
			cmodel.addElement(items[i]);
		}
//		if(items.length > 0){
//			cmodel.setSelectedItem(items[0]);
//		}
		box.setModel(cmodel);
	}

	private long getBeginDate() throws ParseException
	{
		Date d = dateFormat.parse(textBeginTime.getText());
		return d.getTime();
	}
	private long getTimeRange() throws ParseException
	{
		return Long.parseLong(textRange.getText()) * 1000;
	}
	private String createTitle(String host, String event)
	{
		return host + " " + event;
	}
	private String createAxisLabelX(String event)
	{
		return "Date and Time";
	}
	private String createAxisLabelY(String event)
	{
		String ret = event;
		String e = event.toLowerCase();
		if(e.startsWith("available") == true){
			ret = "Available Disk space";
		}else if(e.startsWith("used") == true){
			ret = "Used Disk space";
		}else if(e.startsWith("eth") == true){
			if(e.indexOf("out") > 0){
				ret = "TX Bandwidth";
			}else if(e.indexOf("in") > 0){
				ret = "RX Bandwidth";
			}else{
				ret = "Network bandwidth";
			}
		}else if(e.startsWith("loadavg") == true){
			ret = "Load average (x100)";
		}
		return ret;
	}
	private String createUnit(String event)
	{
		String ret = ".";
		String e = event.toLowerCase();
		if(e.startsWith("available") == true){
			ret = "bytes";
		}else if(e.startsWith("used") == true){
			ret = "bytes";
		}else if(e.startsWith("eth") == true){
			ret = "bps";
		}else if(e.startsWith("loadavg") == true){
			ret = "";
		}
		return ret;
	}

	protected int prefixIndex(long v)
	{
		long factor = 0;
		int prefix_index = 0;
	
		for(int i = 1; i < UNIT_PREFIX_TABLE.length; i++){
			factor = (long) Math.pow(10, i * 3);
			prefix_index = i - 1;
			if( (v / factor) <= 1 ){
				// i - 1 hit.
				break;
			}
		}
//System.out.println("prefix_index " + prefix_index + " / " + v);
		return prefix_index;
	}
	
	
	private boolean initFlag = true;
	private boolean checkHostFlag;
	private boolean checkEventFlag;
	private boolean lockRefresh = false;
	
	private void paintGraphAccordingToCurrentGUIStatus()
	{
		if(lockRefresh == true){
//System.out.println("Cancel: Refresh");
			return;
		}
//System.out.println("Refresh");
		checkHostFlag = true;
		checkEventFlag = true;
		lockRefresh = true;
		paintGraphAccordingToCurrentGUIStatus2();
		lockRefresh = false;
//System.out.println("Refresh done");
	}

	private void paintGraphAccordingToCurrentGUIStatus2()
	{
		if(dtSpace == null){
			// No data-time space, Nothing to do.
			return;
		}
		String unit = "";
		String unitName = ((JRadioButtonMenuItem)comboUnit.getSelectedItem()).getText();
		if(unitName.equals(SimpleGrapherBaseUI.unitKB)){
			unit += " KB";
		} else if(unitName.equals(SimpleGrapherBaseUI.unitNone)){
			unit += "";
		} else if(unitName.equals(SimpleGrapherBaseUI.unitbps)){
			unit += " bps";
		} else if(unitName.equals(SimpleGrapherBaseUI.unitLoad)){
			unit += " %";
		}

		boolean diffmode = checkDifferentialMode.isSelected();
		boolean totalmode= checkTotal.isSelected();
		boolean automode = checkAutoUpdate.isSelected();

		GraphMonitor gm = SimpleGrapherApp.this.getGraphPane();
		String event= (String) comboEvent.getSelectedItem();
		ArrayList hostList = new ArrayList();
		if(totalmode == true){
			for(int i = 0; i < selectedHostsForTotal.length; i++){
				hostList.add(selectedHostsForTotal[i]);
			}
//			DefaultComboBoxModel cmbmodel = (DefaultComboBoxModel) comboHostname.getModel();
//			int count = cmbmodel.getSize();
//			for(int i = 0; i < count; i++){
//				String host = (String) cmbmodel.getElementAt(i);
//				hostList.add(host);
//			}
		}else{
			String host = (String) comboHostname.getSelectedItem();
			hostList.add(host);
		}
		long begin = 0;
		long term = 0;
		rawDataSeries.clear();
		try{
			begin = getBeginDate();
//System.out.println("getBeginDate() " + begin);
			term = getTimeRange();
			model.setBegin(begin);
			model.setTerm(term);
		}catch(ParseException e1) {
			JOptionPane.showMessageDialog(appFrame,
				"Invalid date time format, correct Begin Time or Time Range");
			return;
		}

		for(int h = 0; h < hostList.size(); h++){
			String host = (String) hostList.get(h);
			try {
				// extract data from data-time space.
				ArrayList dt = dtSpace.getMeasurementData(host, event, begin, term);
				
				ArrayList rde;
				if(diffmode && menuItemCorrectByUptime.isSelected() && selectedUptimeEventForCorrection != null){
					ArrayList uptimeList = dtSpace.getMeasurementData(host, selectedUptimeEventForCorrection, begin, term);
					if(menuItemBefore.isSelected()){
						dt = dtSpace.resampleBeforeDateDataPairListToRawDataElements(dt, resampleResolution);
						uptimeList = dtSpace.resampleBeforeDateDataPairListToRawDataElements(uptimeList, resampleResolution);
					} else if(menuItemInterpolate.isSelected()){
						dt = dtSpace.resampleEstimateDateDataPairListToRawDataElements(dt, resampleResolution);
						uptimeList = dtSpace.resampleEstimateDateDataPairListToRawDataElements(uptimeList, resampleResolution);
					}
					rde = dtSpace.convertDateDataPairListToRawDataElementsByUptime(dt, uptimeList);
				} else {
					if(menuItemBefore.isSelected()){
						dt = dtSpace.resampleBeforeDateDataPairListToRawDataElements(dt, resampleResolution);
					} else if(menuItemInterpolate.isSelected()){
						dt = dtSpace.resampleEstimateDateDataPairListToRawDataElements(dt, resampleResolution);
					}					
					rde = dtSpace.convertDateDataPairListToRawDataElements(dt, diffmode);
				}
				
				RawData rd = new RawData();
				rd.setValid(true);
				RawDataElement[] rda = new RawDataElement[rde.size()];
				rda = (RawDataElement[]) rde.toArray(rda);
				int magnification = 1;
				int rate = 1;
				if(check8times.isSelected() == true){
					magnification *= 8;
				}
				if(check1000times.isSelected() == true){
					magnification *= 1000;
				}
				if(check100div.isSelected() == true){
					rate *= 100;
				}
				if(magnification > 1 || rate > 1){
					for(int i=0; i<rda.length; i++){
						rda[i].setValue(rda[i].getValue() * magnification / rate);	
					}
				}

				rd.setData(rda);
				rawDataSeries.add(rd);
			} catch (Exception e) {
				// cannot create model because specified time is out of range.
				// null clear.
				//e.printStackTrace();
//System.out.println("no data: " + host + " / " + event + "");

				// next event
				int id = comboEvent.getSelectedIndex();
				comboEvent.removeItemAt(id);
				if(checkEventFlag){
					id = 0;
					checkEventFlag = false;
				} else {
//					id++;
				}
				if(id < comboEvent.getItemCount()){
					disableComboEvent = true;
					comboEvent.setSelectedIndex(id);
					disableComboEvent = false;
					paintGraphAccordingToCurrentGUIStatus2();
					return;
				}
				defineNewComboBox(comboEvent, dtSpace.getEvents());
				
				// next host
				checkEventFlag = true;

				disableComboEvent = true;
				comboEvent.setSelectedIndex(0);
				disableComboEvent = false;
				
				id = comboHostname.getSelectedIndex();		
				comboHostname.removeItemAt(id);
				if(checkHostFlag){
					id = 0;
					checkHostFlag = false;
				} else {
//					id++;
				}
				if(id < comboHostname.getItemCount()){
					disableComboEvent = true;
					comboHostname.setSelectedIndex(id);
					disableComboEvent = false;
					paintGraphAccordingToCurrentGUIStatus2();
					return;
				}
				
				actionAutoUpdate(false, false);
				checkAutoUpdate.setSelected(false);
				menuItemTotal.setSelected(false);
				JOptionPane.showMessageDialog(appFrame,
					"Error: no data");
				gm.setModel(null);
				if(totalmode == true){
					// total off
					checkTotal.setSelected(false);
					menuItemTotal.setSelected(false);
					comboHostname.setEnabled(true);
				}
//				checkHostFlag = true;
//				checkEventFlag = true;
				return;
			}
		}
		
		if(totalmode == true){
			rawDataSeries = totalofRawDataSeries(rawDataSeries);
		}
		for(int z = 0; z < rawDataSeries.size(); z++){
			RawData rd = (RawData) rawDataSeries.get(z);
			rd.setDiffMode(diffmode);
			rd.setMax(model.getTopValue());
		}
		
		model.setRawDataSeries(rawDataSeries, magnificationY);

		//int prefixIndex = prefixIndex(model.getMaxValue());
		int prefixIndex = prefixIndex(model.getTopValue());
		int cooked_prefixIndex = cookPrefixIndex(prefixIndex, event);
		String prefix = UNIT_PREFIX_TABLE[cooked_prefixIndex];
		long prefixFactor = (long) Math.pow(1000, prefixIndex); // not cooked.
		model.setPrefixFactor(prefixFactor);

		model.setSumMode(totalmode);
		String labelAxisX = createAxisLabelX(event);
		
		String labelAxisY = "";
		//String labelAxisY = createAxisLabelY(event);
/*
		String unit = createUnit(event);
		if(unit.length() > 0){
			//labelAxisY += " (" + prefix + createUnit(event) + ")";
		}
*/
		if(unit == null || unit.equals("")){
			labelAxisY = prefix;
		} else {
			labelAxisY = prefix + " (" + unit + ")";
		}
		
		gm.setStyleFill(checkGraphStyleBar.isSelected());
		gm.setStyleJoin(checkGraphStyleLine.isSelected());
		gm.setStylePlot(checkGraphStylePlot.isSelected());
		String host;
		if(totalmode == true){
			host = "Total of ";
		}else{
			host = (String) comboHostname.getSelectedItem();
			host = getName1(host);
		}
		event = getName1(event);
		
		gm.setTitle(createTitle(host, event));
		gm.setModel(model);
		gm.setAxisLabelX(labelAxisX); // set label of x-axis
		gm.setAxisLabelY(labelAxisY); // set label of y-axis

		Document doc = txtInfo.getDocument();
		String info = "";
		info += "Latest (" + dateString(model.getLatestValueTime()) + "): "+ decimalf.format(model.getLatestValue()) + unit + lineSep;
 		info += "Max (" + dateString(model.getMaxValueTime()) + "): " + decimalf.format(model.getMaxValue()) + unit + lineSep;
		info += "Min (" + dateString(model.getMinValueTime()) + "): " + decimalf.format(model.getMinValue()) + unit + lineSep;
		//info += "min (" + dateString(model.getMinValueTime()) + "): " + df.format(Long.MAX_VALUE) + unit + ls;
		info += "Average: " + decimalf.format((long) Math.rint(model.getAvgValue())) + unit;
		if(totalmode == true){
			info += lineSep + "Total: " + event + " (";
			int i = 0;
			while(true){
				info += getName1(selectedHostsForTotal[i]);
				if( i < selectedHostsForTotal.length - 1){
					info += ", ";
				} else {
					info += ")";
					break;
				}
				i++;
			}
		}
		txtInfo.setText(info);
		txtInfo.repaint();
		
		if(initFlag){
			initFlag = false;
		}

		//gm.repaint();
		appFrame.repaint();
	}
	
	String lineSep = System.getProperty("line.separator");
	DecimalFormat decimalf   = new DecimalFormat("###,###,##0");
	
	public static String dateString(long t){
		DateFormat df = new SimpleDateFormat("yyyy/MM/dd HH:mm:ss");
		return df.format(new Date(t));
	}

	private int cookPrefixIndex(int prefixIndex, String event) {
		int ret = prefixIndex;
		String e = event.toLowerCase();
		if(e.startsWith("available") == true){
			//ret++;
		}else if(e.startsWith("used") == true){
			//ret++;
		}else if(e.startsWith("eth") == true){
			// Nothing to do.
		}else if(e.startsWith("loadavg") == true){
			//ret = 0;
		}
		return ret;
	}

	private ArrayList totalofRawDataSeries(ArrayList rawDataSeries) {
		RawData[] rda = new RawData[rawDataSeries.size()];
		rda = (RawData[]) rawDataSeries.toArray(rda);
		RawDataElement[] rde = total(rda);
		ArrayList newList = new ArrayList();
		RawData newData = new RawData();
		
		newData.setMax(rda[0].getMax());
		newData.setBarColor(rda[0].getBarColor());
		newData.setLevelColor(rda[0].getLevelColor());
		newData.setLineColor(rda[0].getLineColor());
		newData.setPlotColor(rda[0].getPlotColor());
		newData.setDiffMode(rda[0].isDiffmode());
		newData.setValid(true);
		newData.setData(rde);
		newList.add(newData);
		return newList;
	}

	/*
	 * "File" - "Open..." Event handler of Menu item.
	 * @author hkondo
	 */
	class MenuItemFileOpenAction implements ActionListener
	{
		public void actionPerformed(ActionEvent arg0) {
			actionAutoUpdate(false, false);
			menuItemAutoUpdate.setSelected(false);
			checkAutoUpdate.setSelected(false);
			// stop Swing timer.
			timer.stop();

			if(currentDirectoryPath != null){
				String path;
				try {
					File f = new File(currentDirectoryPath);
					if(f.exists()){
						path = f.getCanonicalPath();
					} else {
						path = "";
					}
				} catch (IOException e) {
					path = currentDirectoryPath;
				}
//System.out.println(path);
				dialogFileOpen.setCurrentDirectory(new File(path));
			}
			int ret = dialogFileOpen.showOpenDialog(appFrame);
			if(ret == JFileChooser.APPROVE_OPTION){
				// rebuild data-time space because files are specified.
				File[] files = dialogFileOpen.getSelectedFiles();
				selectedDir = files[0].getParentFile();
				//currentDirectoryPath = selectedDir.getParent();
				currentDirectoryPath = selectedDir.getAbsolutePath();
				String[] filenames = new String[files.length];
				for(int i = 0; i < files.length; i++){
					filenames[i] = files[i].getPath();
				}
				try {
					dtSpace = new DataTimeSpace(filenames);					
					long begin = dtSpace.getBeginDateTime() - 500;
					long latest = dtSpace.getLatestDateTime();
					String dt = dateFormat.format(new Date(begin));
					textBeginTime.setText(dt);
					long r =(latest-begin)/1000;
					if(r <= 0){
						r = 1;
					}
					textRange.setText(""+r);
//System.out.println("dtSpace.getBeginDateTime(): " + begin + " -> " + latest);
					
					defineNewComboBox(comboHostname, dtSpace.getHostnames());
					defineNewComboBox(comboEvent, dtSpace.getEvents());
					paintGraphAccordingToCurrentGUIStatus();
				} catch (Exception e) {
					// Cannot create datat-time space.
//e.printStackTrace();
					String msg = "File open error.";
					JOptionPane.showMessageDialog(appFrame, msg);
				}
			}
		}
	}


	public void setOptions2(String args[]) {
		if(args.length >= 1){
			currentDirectoryPath = args[0];
		} else {
			currentDirectoryPath = null;
		}
		panelInformation.setVisible(false);
		menuItemVisInfo.setSelected(false);
		panelController.setVisible(false);
		menuItemVisController.setSelected(false);
	}

	public void printHelp(){
		String ls = System.getProperty("line.separator");
		String str = "usage: java -jar gmonitor3.jar [ -d TargetAutoUpdateDirectory ]" + ls;
		str += "       [ -h TargetHostname ] [ -e TargetEventNickName ] " + ls;
		str += "       [ -u UnitName(bps/KB/Load) ] [ --help ]" + ls;
		str += "       [ --no-line ] [ --no-bar ] [ --no-plot ]" + ls;
		str += "       [ --no-information ] [ --no-style ] [ --no-unit ] [ --no-controller ]";
		System.err.println(str);
	}

	public void setOptions(String args[]) {
		int len = args.length;
		
		currentDirectoryPath = null;
		setTargetHostName = null;
		setTargetEventName = null;
		setTargetUnitName = null;
		//initialDelay = 0;  // default 3 seconds
		
		for(int i = 0; i < len; i++){
			String now = args[i];
			if(now.startsWith("-")){
				String opt = now.substring(1);
				if(opt.startsWith("d")){
					if(++i < len){
						currentDirectoryPath = args[i];
					} else {
						System.err.println("");
						printHelp();
						System.exit(1);
					}
				}
				else if(opt.startsWith("h")){
					if(++i < len){
						setTargetHostName = args[i];
					} else {
						System.err.println("");
						printHelp();
						System.exit(1);
					}
				} else if(opt.startsWith("e")){
					if(++i < len){
						setTargetEventName = args[i];
					} else {
						System.err.println("");
						printHelp();
						System.exit(1);
					}
				} else if(opt.startsWith("u")){
					if(++i < len){
						setTargetUnitName = args[i];
					} else {
						System.err.println("");
						printHelp();
						System.exit(1);
					}
				} else if(opt.startsWith("-")){
					String longopt = opt.substring(1);
					if(longopt.equals("no-line")){
						menuItemGraphStyleLine.setSelected(false);
						checkGraphStyleLine.setSelected(false);
					} else if(longopt.equals("no-bar")){
						menuItemGraphStyleBar.setSelected(false);
						checkGraphStyleBar.setSelected(false);
					} else if(longopt.equals("no-plot")){
						menuItemGraphStylePlot.setSelected(false);
						checkGraphStylePlot.setSelected(false);
					} else if(longopt.equals("no-information")){
						panelInformation.setVisible(false);
						menuItemVisInfo.setSelected(false);
					} else if(longopt.equals("no-style")){
						panelGraphStyle.setVisible(false);
						menuItemVisGraphStyle.setSelected(false);
					} else if(longopt.equals("no-unit")){
						panelUnit.setVisible(false);
						menuItemVisUnit.setSelected(false);
					} else if(longopt.equals("no-controller")){
						panelController.setVisible(false);
						menuItemVisController.setSelected(false);
					} else if(longopt.equals("help")){
						printHelp();
						System.exit(1);
					} else {
						System.err.println("invalid option -- " + longopt);
						printHelp();
						System.exit(1);
					}
				} else {
					System.err.println("invalid option -- " + opt);
					printHelp();
					System.exit(1);
				}
			}
		}
		
		if(currentDirectoryPath != null){
			checkAutoUpdate.setSelected(true);
			menuItemAutoUpdate.setSelected(true);
			timer.setInitialDelay(1000);
			actionAutoUpdate(true, false);
		}
		timer.setInitialDelay(0);
	}

	public void setComboTargets(){
		setComboTargetsFlag = true;
		if(setTargetHostName != null){
			for(int i=0; i<comboHostname.getItemCount(); i++){
//System.out.println((String)comboHostname.getItemAt(i));					
				//if(setTargetHostName.equals((String)comboHostname.getItemAt(i))){
				Object chn = comboHostname.getItemAt(i);
				if(chn != null && ((String)chn).indexOf(setTargetHostName) >= 0){
					comboHostname.setSelectedItem(chn);
					break;
				}
			}
//System.out.println(setTargetHostName);
		}
		if(setTargetEventName != null){
			for(int i=0; i<comboEvent.getItemCount(); i++){
//System.out.println((String)comboEvent.getItemAt(i));
				//if(setTargetEventName.equals((String)comboEvent.getItemAt(i))){
				Object ce = comboEvent.getItemAt(i);
				if(ce != null && ((String)ce).indexOf(setTargetEventName) >= 0){
					comboEvent.setSelectedItem(ce);
					break;
				}
			}
//System.out.println(setTargetEventName);
		}
		if(setTargetUnitName != null){
			if(setTargetUnitName.equals("bps")){
				comboUnit.setSelectedItem(radioMenubps);
			} else if(setTargetUnitName.equals("KB")){
				comboUnit.setSelectedItem(radioMenuKB);
			} else if(setTargetUnitName.equals("Load")){
				comboUnit.setSelectedItem(radioMenuLoad);
			}
			//setTargetUnitName = null;
		}
		setComboTargetsFlag = false;
	}

	public SimpleGrapherApp(String args[])
	{
		initializeFromProperty("GMonitor.");
		initializeEventHandlers();
		initializeGUIComponents();
		JFrame f = appFrame;
		if(propMenuBarVisibility == true){
			f.setJMenuBar(getMenubar());
		}
		Container c = f.getContentPane();
		JPanel allPanel = new JPanel();
		GridBagConstraints gbc = new GridBagConstraints();
		allPanel.setLayout(new GridBagLayout());
		
		gbc.gridwidth = 2;
		gbc.gridy = 0;
		gbc.weightx = 1.0;
		gbc.weighty = 1.0;
		gbc.fill = GridBagConstraints.BOTH;
		allPanel.add(getGraphPane(), gbc);
		gbc.gridy++;
		gbc.weightx = 0.0;
		gbc.weighty = 0.0;

		gbc.fill = GridBagConstraints.HORIZONTAL;
		if(propInformationPanelVisibility == true){
			allPanel.add(getPanelInformation(), gbc);
		}
		gbc.gridy++;
		gbc.gridwidth = 1;
		if(propStylePanelVisibility == true){
			allPanel.add(getPanelGraphStyle(), gbc);
		}
		if(propUnitPanelVisibility == true){
			allPanel.add(getPanelUnit(), gbc);
		}
		gbc.gridy++;
		gbc.gridwidth = 2;
		if(propControllerVisibility == true){
			allPanel.add(getPanelController(), gbc);
		}
		c.add(allPanel);

// Swing paint debugging with diabling double-bufferring.
//RepaintManager rm = RepaintManager.currentManager(getGraphPane());
//rm.setDoubleBufferingEnabled(false);
	}
	
	public static void main(String[] args) {
		SimpleGrapherApp app = new SimpleGrapherApp(args);

		app.setTitle("GMonitor");
		app.pack();
		app.setOptions(args);

		try {
			Dimension d = app.appFrame.getSize();
			DisplayMode dm = GraphicsEnvironment.getLocalGraphicsEnvironment().getDefaultScreenDevice().getDisplayMode();
			app.appFrame.setLocation(dm.getWidth()/2 - d.width/2, dm.getHeight()/2 - d.height/2);
		} catch(Exception e){
		}
		app.show();
	}
	/**
	 * set frame size.
	 * @param arg0
	 * @param arg1
	 */
	public void setSize(int arg0, int arg1) {
		appFrame.setSize(arg0, arg1);
	}
	/**
	 * set title of frame.
	 * @param arg0
	 */
	public void setTitle(String arg0) {
		appFrame.setTitle(arg0);
	}
	/**
	 * control visibility of application frame.
	 * @param arg0
	 */
	public void setVisible(boolean arg0) {
		appFrame.setVisible(arg0);
	}
	/**
	 * control visibility of application frame. 
	 */
	public void show() {
		appFrame.show();
	}
	
	public void pack(){
		appFrame.pack();
	}

	private RawDataElement[] total(RawData[] rda)
	{
		RawData newData = new RawData();
		ArrayList newList = new ArrayList();
		long time = 0;
		int series = rda.length;

		RawDataElement[] totalBuffer = new RawDataElement[series];
		int[] scanIndex = new int[series];
		long endTime[] = new long[series];
		fillEndTime(endTime, rda);

		while(true){
			// scan next element.
			time = scanNext(time, rda, scanIndex, totalBuffer);
	
			// add up values latched in total buffer and create new element.
			long v = addup(totalBuffer);
			RawDataElement ne = new RawDataElement();
			ne.setTime(time);
			ne.setValue(v);
			ne.setValid(true);
			newList.add(ne);

			// termination ?
			if(isFinishedTime(time, endTime) == true){
				break;
			}
		}
		
		RawDataElement[] rde = new RawDataElement[newList.size()];
		rde = (RawDataElement[]) newList.toArray(rde);
		return rde;
	}

	private long scanNext(long time, RawData[] rda, int[] si, RawDataElement[] tb)
	{
		int series = rda.length;

		// with all series...
		long t = Long.MAX_VALUE;
		for(int s = 0; s < series; s++){
			RawDataElement[] rde = rda[s].getData();
			// finding the index of element that is exceeded time...
			int idx = findFirstExceededTime(time, rde);
			// ... and adopt the oldest element.
			if(idx < 0){
				// not matched in this series.
			}else{
				// temporary keeping.
				si[s] = idx;
				long tt = rde[idx].getTime();
				if(t > tt){
					t = tt;
				}
			}
		}

		// matched elements are latched in tb. If plural elements are matched, process them all. 
		for(int s = 0; s < series; s++){
			RawDataElement[] rde = rda[s].getData();
			if(rde[si[s]].getTime() == t){
				// should be latched in tb.
				tb[s] = rde[si[s]];
			}
		}
		return t;
	}

	/**
	 * @param time
	 * @param rde
	 * @return
	 */
	private int findFirstExceededTime(long time, RawDataElement[] rde) {
		for(int i = 0; i < rde.length; i++){
			if(rde[i].isValid() == true){
				if(rde[i].getTime() > time){
					return i;
				}
			}
		}
		return -1;
	}

	private boolean isFinishedTime(long t, long[] endTime)
	{
		for(int i = 0; i < endTime.length; i++){
			if(t < endTime[i]){
				return false;
			}
		}
		// t >= endTime[all of];
		return true;
	}

	private void fillEndTime(long[] endTime, RawData[] rda)
	{
		for(int i = 0; i < rda.length; i++){
			RawDataElement[] rde = rda[i].getData();
			endTime[i] = rde[rde.length - 1].getTime();
		}
		return;
	}
	
	private long addup(RawDataElement[] rde)
	{
		long s = 0;
		for(int i = 0; i < rde.length; i++){
			if(rde[i] != null){
				s += rde[i].getValue();
			}
		}
		return s;
	}

}
