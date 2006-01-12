package gfront;

import java.awt.BorderLayout;
import java.awt.Insets;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;

import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JDialog;
import javax.swing.JFrame;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTextArea;

public class GFrontCommon {

	public void showWarning(String titile, String message){
		JOptionPane.showMessageDialog(
			null,
			message,
			titile,
			JOptionPane.WARNING_MESSAGE);
	}

	private JDialog jd;
	public void showTextArea(JFrame parent, String title, String str) {
		showTextArea(parent, title, str, 10, 40);
	}
	
	public void showTextArea(JFrame parent, String title, String str, int rows, int colums) {
		jd = new JDialog(parent, title, true);
		jd.setLocationRelativeTo(parent);
		//jd.setSize(500, 200);

		JPanel jp = new JPanel();
		jp.setLayout(new BorderLayout());
		//jp.setLayout(new FlowLayout(FlowLayout.CENTER));

		JTextArea txtOut = new JTextArea(rows, colums);
		txtOut.setMargin(new Insets(5, 5, 5, 5));
		txtOut.setEditable(false);
		txtOut.append(str);
		
		JButton b = new JButton("OK");
		b.addActionListener(new ActionListener() {
			public void actionPerformed(ActionEvent e) {
				jd.setVisible(false);
				jd = null;
			}
		});

		jp.add(new JScrollPane(txtOut), BorderLayout.NORTH);
		jp.add(b, BorderLayout.SOUTH);
		jp.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));

		jd.setContentPane(jp);
		jd.pack();
		jd.setVisible(true);
	}

	private String errStringOfRunCommand = null;
	public String runCommand(JFrame parent, String cmd, String name, boolean outWindow){
		errStringOfRunCommand = null;
		
		System.out.println("$ " + cmd);

		Runtime r = Runtime.getRuntime();
		Process p = null;
		try {
			p = r.exec(cmd);
		} catch (IOException ioe) {
			System.out.println(name + " is not found");
			return null;
		}
		SaveStringOfInputStream stdOut= new SaveStringOfInputStream(p.getInputStream());
		SaveStringOfInputStream errorOut= new SaveStringOfInputStream(p.getErrorStream());
		stdOut.start();
		errorOut.start();
		
		try {
			p.waitFor();
		} catch (InterruptedException e) {
			e.printStackTrace();
		}
		System.out.println("exit value: " + p.exitValue());

		String stdMsg = stdOut.toString();
		String errorMsg = errorOut.toString();
		if(outWindow){
			if (stdMsg.length() > 0) {  // stdout
				System.out.println(stdMsg);
				showTextArea(parent, name + ": stdout", stdMsg);
			}
			if (errorMsg.length() > 0) {  // stderr
				System.out.println(errorMsg);
				showTextArea(parent, name + ": stderr", errorMsg);
			}
		}
		errStringOfRunCommand = errorMsg;
		return stdMsg;
	}

	public void runNomalCommand(JFrame parent, String cmd, String name){
		runCommand(parent, cmd, name, true);
	}

	public String runNomalCommandToString(JFrame parent, String cmd, String name){
		return runCommand(parent, cmd, name, false);
	}

	public String getErrorString(){
		return errStringOfRunCommand;
	}

	class SaveStringOfInputStream extends java.lang.Thread {
		private BufferedReader br;
		private StringBuffer lines;
		
		SaveStringOfInputStream(InputStream is){
			br = new BufferedReader(new InputStreamReader(is));
			lines = new StringBuffer();
		}
		
		public void run() {
			try {
				while (true) {
					String ln = br.readLine();
					if (ln == null) {
						// End of stream.
						break;
					}
					lines.append(ln + System.getProperty("line.separator"));
				}
			} catch (IOException ioe) {
				return;
			}
		}

		public String toString(){
			return lines.toString();
		}
	}
}
