package gfront;

public class CommandOutput {
	String[] output;
	int retcode;
	int idx;

	CommandOutput(int num, int r) {
		output = new String[num];
		retcode = r;
	}
}
