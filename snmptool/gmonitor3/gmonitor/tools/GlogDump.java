/*
 * Created on 2003/07/07
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.tools;

import java.io.IOException;
import java.util.ArrayList;

import gmonitor.logdata.DataBlock;
import gmonitor.logdata.DataFile;
import gmonitor.logdata.FirstMetaBlock;
import gmonitor.logdata.SecondMetaBlock;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class GlogDump {

	protected boolean swFMBOutput = true;
	protected boolean swSMBOutput = true;
	protected boolean swDBOutput = true;
	protected String filename = "glogger.bin";

	public GlogDump()
	{
	}
	public GlogDump(String args[])
	{
		for(int i = 0; i < args.length; i++){
			if(args[i].startsWith("-")){
				// オプションの処理
			}else{
				// ファイル名の処理
				filename = args[i];
			}
		}
	}

	public void run()
	{
		FirstMetaBlock fmb;
		SecondMetaBlock smb;
		try {
			DataFile f = DataFile.getInstance(filename);
			/* FirstMetaBlock */{
				fmb = f.getFirstMetaBlock();
			}
						
			/* SecondMetaBlock */{
				smb = f.getSecondMetaBlock();
			}

			if(swFMBOutput == true){
				System.out.println(fmb.asDump());
			}
			if(swSMBOutput == true){
				System.out.println(smb.asDump());
			}
			if(swDBOutput == true){
				int count = (int) f.getLength();
				int dbc = 0;
				for(int i = 0; i < count; i++){
					ArrayList l = f.getDataBlockGroup((long) i);
					for(int j = 0; j < l.size(); j++){
						DataBlock db = (DataBlock) l.get(j);
						System.out.print(db.asDump(dbc, smb.getIntervalDefBlock()));
						dbc++;
					}
				}
			}
		} catch (IOException e) {
			e.printStackTrace();
		}
	}

	public static void main(String args[])
	{
		GlogDump dlf = new GlogDump(args);
		dlf.run();
	}
}
