package gmonitor.logdata;
import java.io.UnsupportedEncodingException;
import java.lang.reflect.Field;
import java.math.BigInteger;
import java.util.ArrayList;

/*
 * Created on 2003/05/14
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class UTY {
	public static final String ENCODING = "ASCII";

	public static final String byte2String(byte b[], int off, int len) throws UnsupportedEncodingException
	{
		return new String(b, off, len, ENCODING);
	}
	public static final long byte2long(byte b[]){
		return 
			( ((b[0]&0xFFL)<<56) | ((b[1]&0xFFL)<<48)
			| ((b[2]&0xFFL)<<40) | ((b[3]&0xFFL)<<32)
			| ((b[4]&0xFFL)<<24) | ((b[5]&0xFFL)<<16)
			| ((b[6]&0xFFL)<<8)  | (b[7]&0xFFL)
			);

//		long val = 0;
//		for(int i = 0; i < 8; i++){
//			val |= (b[i] & 0xFF) << (8*(7-i));
//		}
//		return val;

		//BigInteger bi = new BigInteger(b);
		//return bi.longValue();
	}
	public static final int byte2int4(byte b[]){
		return 
			( ((b[0]&0xFF)<<24) | ((b[1]&0xFF)<<16)
			| ((b[2]&0xFF)<<8)  | (b[3]&0xFF)
			);
	}
	public static final int byte2int(byte b[]){
		BigInteger bi = new BigInteger(b);
		return bi.intValue();
	}

	public static String toStringLine(String[] fields, char delim)
	{
		StringBuffer b = new StringBuffer();
		b.append(fields[0]);
		for(int i = 1; i < fields.length; i++){
			b.append(delim);
			b.append(fields[i]);
		}
		return b.toString();
	}
	public static String toStringLine(ArrayList fields, char delim)
	{
		String[] tokens = new String[fields.size()];
		tokens = (String[]) fields.toArray(tokens);
		return toStringLine(tokens, delim);		
	}
	public static String toStringLine(Object obj, String name, String info, String[] label, String[] fields)
	{
		return toStringLine(obj, name, info, label, fields, ':');
	}
	public static String toStringLine(Object obj, String name, String info, String[] label, String[] fields, char delim)
	{
		StringBuffer b = new StringBuffer();
		Class c = obj.getClass();
		Field f = null;
		b.append(name);
		b.append(delim);
		b.append(info);
		for(int i = 0; i < label.length; i++){
			try {
				f = c.getDeclaredField(fields[i]);
				Object val = f.get(obj);
				b.append(delim);
				b.append(val);
			} catch (SecurityException e) {
				b.append("sssss");
			} catch (NoSuchFieldException e) {
				b.append("nnnnn");
			} catch (IllegalArgumentException e1) {
				b.append("xxxxx");
			} catch (IllegalAccessException e1) {
				b.append("zzzzz");
			}
		}
		return b.toString();
	}

	public static String toString(Object obj, String name, String info, String[] label, String[] fields)
	{
		StringBuffer b = new StringBuffer();
		Class c = obj.getClass();
		Field f = null;
		b.append("[ ");
		if(name != null && name.equals("") == false){
			b.append(name);
			b.append(' ');
		}
		if(info != null && info.equals("") == false){
			b.append('(');
			b.append(info);
			b.append(") ");
		}
		b.append("]\n");
		for(int i = 0; i < label.length; i++){
			b.append(" - ");
			b.append(label);
			b.append(" = ");
			try {
				f = c.getDeclaredField(fields[i]);
				Object val = f.get(obj);
				b.append(val);
				b.append("\n");
				b.append(val);
				b.append("\n");
			} catch (SecurityException e) {
				b.append("--- Security Violation ---\n");
			} catch (NoSuchFieldException e) {
				b.append("--- No Such Field ");
				b.append(fields[i]);
				b.append(" ---\n");
			} catch (IllegalArgumentException e1) {
				b.append("--- Illegal Argument Exception ---\n");
			} catch (IllegalAccessException e1) {
				b.append("--- IllegalAccessException ---\n");
			}
		}
		return b.toString();
	}

	public static final int byte2int(byte b)
	{
		return (((int)b) & 0x000000ff);
	}
}
