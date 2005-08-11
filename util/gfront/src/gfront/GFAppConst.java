package gfront;

public interface GFAppConst {
	public static final int DEFAULT_METASERVER_PORT = 9000;
	public static final String DEFAULT_SNMP_COMMUNITY = "public";
	public static final String DEFAULT_SNMPGET = "/usr/bin/snmpget";

	public static final String DEFAULT_MIBPREFIX_HOSTNAME = "system.sysName";
	public static final String DEFAULT_MIBPREFIX_LOAD_AVG =
		"enterprises.ucdavis.laTable.laEntry.laLoad";
	public static final String DEFAULT_MIBPREFIX_IF_TX =
		"interfaces.ifTable.ifEntry.ifOutOctets";
	public static final String DEFAULT_MIBPREFIX_IF_RX =
		"interfaces.ifTable.ifEntry.ifInOctets";
	public static final String DEFAULT_MIBPREFIX_SPOOL_USAGE =
		"enterprises.ucdavis.dskTable.dskEntry.dskUsed";
	public static final String DEFAULT_MIBPREFIX_SPOOL_AVAIL =
		"enterprises.ucdavis.dskTable.dskEntry.dskAvail";
	public static final String[] DEFAULT_MIBPREFIX =
		{
			DEFAULT_MIBPREFIX_HOSTNAME,
			DEFAULT_MIBPREFIX_LOAD_AVG,
			DEFAULT_MIBPREFIX_IF_TX,
			DEFAULT_MIBPREFIX_IF_RX,
			DEFAULT_MIBPREFIX_SPOOL_USAGE,
			DEFAULT_MIBPREFIX_SPOOL_AVAIL,
			};

	public static final String DEFAULT_MIB_HOSTNAME =
		DEFAULT_MIBPREFIX_HOSTNAME + ".1";
	public static final String DEFAULT_MIB_LOAD_AVG =
		DEFAULT_MIBPREFIX_LOAD_AVG + ".1";
	public static final String DEFAULT_MIB_IF_TX =
		DEFAULT_MIBPREFIX_IF_TX + ".5";
	public static final String DEFAULT_MIB_IF_RX =
		DEFAULT_MIBPREFIX_IF_RX + ".5";
	public static final String DEFAULT_MIB_SPOOL_USAGE =
		DEFAULT_MIBPREFIX_SPOOL_USAGE + ".1";
	public static final String DEFAULT_MIB_SPOOL_AVAIL =
		DEFAULT_MIBPREFIX_SPOOL_AVAIL + ".1";

	public static final int DEFAULT_INTERVAL = 1000; // milli sec.
}
