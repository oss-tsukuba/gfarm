/*
 * Created on 2003/07/08
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.tools;

import gmonitor.logdata.DataFile;

import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class ScanLogDir {

	ArrayList files = new ArrayList();
	
	public ScanLogDir(String args[])
	{
		for(int i = 0; i < args.length; i++){
			if(args[i].startsWith("-") == true){
				// option parsing.
			}else{
				// add to target list.
				files.add(args[i]);
			}
		}
	}

	public void run()
	{
		SimpleDateFormat df = new SimpleDateFormat("yyyy/MM/dd HH:mm:ss");
		System.out.println("Processing files are:");
		for(int i = 0; i < files.size(); i++){
			String fn = (String) files.get(i);
			System.out.print(fn + "\t");
			try {
				DataFile f = DataFile.getInstance(fn);
				long begin = f.getBeginDateTime();
				long end   = f.getLatestDateTime();
				System.out.print("Begin:" + df.format(new Date(begin)));
				System.out.print(" End:"  + df.format(new Date(end)));
				System.out.print('\n');
			} catch (IOException e) {
				System.out.println("*** IOException ***");
			}
			
		}
	}
	
	public static void main(String args[])
	{
		ScanLogDir app = new ScanLogDir(args);
		long b = System.currentTimeMillis();
		app.run();
		long e = System.currentTimeMillis();
		System.out.println("Elapsed in realtime: " + (e - b) + " milli-seconds.");
	}
}
