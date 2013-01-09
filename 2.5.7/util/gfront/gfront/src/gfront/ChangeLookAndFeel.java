package gfront;

import java.awt.Component;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.util.Vector;

import javax.swing.JMenuItem;
import javax.swing.LookAndFeel;
import javax.swing.SwingUtilities;
import javax.swing.UIManager;
import javax.swing.UnsupportedLookAndFeelException;

public class ChangeLookAndFeel {
	LookAndFeel laf;
	UIManager.LookAndFeelInfo lafinfo[];	
	//Component main;
	Vector compolist;
	
	ChangeLookAndFeel(){
		init();
	}
	
	ChangeLookAndFeel(Component _main){
		this();
		//main = _main;
		compolist = new Vector();
		compolist.add(_main);
	}

	void init(){
		laf = UIManager.getLookAndFeel();
		lafinfo = UIManager.getInstalledLookAndFeels();		
	}
	
	public void addComponent(Component c){
		compolist.add(c);
	}
	
	public void addMenuItem(JMenuItem mi){
		for(int i = 0; i < lafinfo.length; i++){
			JMenuItem jmi = new JMenuItem(lafinfo[i].getName());
			jmi.addActionListener(new ClickAction(lafinfo[i].getClassName()));
			mi.add(jmi);
		}
	}
	
	public static void main(String args[]){
		ChangeLookAndFeel clf = new ChangeLookAndFeel();
		
		for(int i = 0; i < clf.lafinfo.length; i++){
			System.out.println(clf.lafinfo[i].getClassName());
			System.out.println(clf.lafinfo[i].getName());
		}
	}


	protected class ClickAction implements ActionListener {
		String uiName;
		//Component c;
	
		ClickAction(String _uiName){
			uiName = _uiName;
		}
	
		public void actionPerformed(ActionEvent ae) {
			try {
				UIManager.setLookAndFeel(uiName);
			} catch (ClassNotFoundException e) {
				//e.printStackTrace();
			} catch (InstantiationException e) {
				//e.printStackTrace();
			} catch (IllegalAccessException e) {
				//e.printStackTrace();
			} catch (UnsupportedLookAndFeelException e) {
				//e.printStackTrace();
			}

			int size = compolist.size();
			Component[] tmp = new Component[size];
			Component[] cmp = (Component[]) (compolist.toArray(tmp));
			for(int i=0; i < size; i++){
				SwingUtilities.updateComponentTreeUI(cmp[i]);
			}
		}
	}

}
