/*
 * Created on 2003/05/14
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
import java.io.UnsupportedEncodingException;

import gmonitor.logdata.UTY;
import junit.framework.TestCase;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class UTYTest extends TestCase {

	/**
	 * Constructor for UTYTest.
	 * @param arg0
	 */
	public UTYTest(String arg0) {
		super(arg0);
	}

	public static void main(String[] args) {
		junit.swingui.TestRunner.run(UTYTest.class);
	}

	/*
	 * @see TestCase#setUp()
	 */
	protected void setUp() throws Exception {
		super.setUp();
	}

	/*
	 * @see TestCase#tearDown()
	 */
	protected void tearDown() throws Exception {
		super.tearDown();
	}

	public void testByte2String() {
		String t = "abcdefghijklmnopqrstuvwxyz01234567890!@#$%^&*()-=\\`_+|~[]{};':\",.<>/?";
		byte[] b = { (byte)0x61, (byte)0x62, (byte)0x63, (byte)0x64,
			         (byte)0x65, (byte)0x66, (byte)0x67, (byte)0x68,
			         (byte)0x69, (byte)0x6a, (byte)0x6b, (byte)0x6c,
			         (byte)0x6d, (byte)0x6e, (byte)0x6f, (byte)0x70,
			         (byte)0x71, (byte)0x72, (byte)0x73, (byte)0x74,
			         (byte)0x75, (byte)0x76, (byte)0x77, (byte)0x78,
			         (byte)0x79, (byte)0x7a, (byte)0x30, (byte)0x31,
			         (byte)0x32, (byte)0x33, (byte)0x34, (byte)0x35,
			         (byte)0x36, (byte)0x37, (byte)0x38, (byte)0x39,
			         (byte)0x30, (byte)0x21, (byte)0x40, (byte)0x23,
			         (byte)0x24, (byte)0x25, (byte)0x5e, (byte)0x26,
			         (byte)0x2a, (byte)0x28, (byte)0x29, (byte)0x2d,
			         (byte)0x3d, (byte)0x5c, (byte)0x60, (byte)0x5f,
			         (byte)0x2b, (byte)0x7c, (byte)0x7e, (byte)0x5b,
			         (byte)0x5d, (byte)0x7b, (byte)0x7d, (byte)0x3b,
			         (byte)0x27, (byte)0x3a, (byte)0x22, (byte)0x2c,
			         (byte)0x2e, (byte)0x3c, (byte)0x3e, (byte)0x2f,
			         (byte)0x3f,
		};
		String r = null;
		try {
			r = UTY.byte2String(b, 0, b.length);
		} catch (UnsupportedEncodingException e1) {
			fail("Encoding ASCII not supported.");
		}
		assertEquals(t, r);
	}

	protected void _testByte2long_0_1(int k){
		// 0L
		long t = 0;
		byte[] b = new byte[k];
		for(int i = 0; i < k; i++){
			b[i] = (byte)0;
		}
		long a = UTY.byte2long(b);
		assertEquals(a, t);
	}

	protected void _testByte2long_INTMAXp1_1(){
		// Integer.MAX_VALUE + 1L (== Integer.MIN_VALUE, int cannot represent) 
		long t = (long)((long)Integer.MAX_VALUE + 1L);
		byte[] b = { (byte)0x80, (byte)0x00, (byte)0x00, (byte)0x00 };
		long a = UTY.byte2long(b);
		assertEquals(a, t * -1L); // bit pattern represents negative value.
	}

	protected void _testByte2long_INTMAXp1_2(){
		// Integer.MAX_VALUE + 1L (== Integer.MIN_VALUE, int cannot represent) 
		long t = (long)((long)Integer.MAX_VALUE + 1L);
		byte[] b = { (byte)0x00, (byte)0x00, (byte)0x00, (byte)0x00,
			         (byte)0x80, (byte)0x00, (byte)0x00, (byte)0x00 };
		long a = UTY.byte2long(b);
		assertEquals(a, t);
	}

	public void testByte2long() {
		// 0L
		for(int i = 1; i < 8; i++){ _testByte2long_0_1(i); }
		// Integer.MAX_VALUE + 1L
		_testByte2long_INTMAXp1_1();
		_testByte2long_INTMAXp1_2();
	}

	/*
	 * int byte2int ‚ÌƒeƒXƒg(byte[])
	 */
	public void testByte2int() {
	}

}
