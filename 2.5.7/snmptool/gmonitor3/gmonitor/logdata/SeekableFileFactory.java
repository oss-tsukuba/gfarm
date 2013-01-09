/*
 * Created on 2003/06/20
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;

import java.io.File;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class SeekableFileFactory {
	
	/**
	 * URL u で示されるファイルを用いて SeekableFile を生成する。
	 * u のプロトコルが file: であれば LocalSeekableFile が、http: であれば HttpSeekableFile　が
	 * 生成される。それ以外のプロトコルでは MalformedURLException が発生する
	 * @param u SeekableFileを示す URL
	 * @return u で示されたファイルから生成された SeekableFile
	 * @throws IOException
	 * @throws MalformedURLException u のプロトコルがfile:もしくはhttp:以外であった場合。
	 */
	public static SeekableFile create(URL u) throws IOException {
		SeekableFile file = null;
		String p = u.getProtocol();
		if(p.equalsIgnoreCase("file")){
			// file: なので LocalSeekableFile を生成
			File f = new File(u.getPath());
			file = new LocalSeekableFile(f);
		}else if(p.equalsIgnoreCase("http")){
			// http: なので HttpSeekableFile を生成
			file = new HttpSeekableFile(u);
		}else{
			// 未知のプロトコル
			throw new MalformedURLException("Unsupported Protocol" + p);
		}
		return file;
	}
	
	public static SeekableFile create(File f) throws IOException {
		// File であらわされるファイルを示す SeekableFile を生成する
		return new LocalSeekableFile(f);
	}

	public static SeekableFile create(String url) throws IOException {
		SeekableFile file = null;
		if(url.startsWith("http:")){
			URL u = new URL(url);
			file = create(u);
		}else{
			File f = new File(url);
			file = create(f);
		}
		return file;
	}
}
