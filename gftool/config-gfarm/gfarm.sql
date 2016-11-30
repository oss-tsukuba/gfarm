--
-- When a table is added to or removed from the file, please update
-- 'config-gfarm.postgresql' too.
--

CREATE TABLE Host (
	hostname	VARCHAR(256)	PRIMARY KEY,
	port		INTEGER		NOT NULL,
	architecture	VARCHAR(128)	NOT NULL,
	ncpu		INTEGER		NOT NULL,
	flags		INTEGER		NOT NULL,
	fsngroupname	VARCHAR(256)
);


CREATE TABLE HostAliases (
	hostalias	VARCHAR(256)	PRIMARY KEY,
	hostname	VARCHAR(256)
		REFERENCES Host(hostname) ON DELETE CASCADE
);

CREATE INDEX HostAliasesByHostname ON HostAliases (hostname);


CREATE TABLE GfarmUser (
	username	VARCHAR(64)	PRIMARY KEY,
	homedir		VARCHAR(1024)	NOT NULL,
	realname	VARCHAR(256)	NOT NULL,
	gsiDN		VARCHAR(1024)
);

CREATE TABLE GfarmGroup (
	groupname	VARCHAR(8192)	PRIMARY KEY
);

CREATE TABLE GfarmGroupAssignment (
	username	VARCHAR(64)
		REFERENCES GfarmUser(username) ON DELETE CASCADE,
	groupname	VARCHAR(8192)
		REFERENCES GfarmGroup(groupname) ON DELETE CASCADE,
	PRIMARY KEY(username, groupname)
);


CREATE TABLE INode (
	inumber		INT8		PRIMARY KEY,
	igen		INT8		NOT NULL,
	nlink		INT8		NOT NULL,
	size		INT8		NOT NULL,
	mode		INTEGER		NOT NULL,
	username	VARCHAR(64)	NOT NULL,
	groupname	VARCHAR(8192)	NOT NULL,
	atimesec	INT8		NOT NULL,
	atimensec	INTEGER		NOT NULL,
	mtimesec	INT8		NOT NULL,
	mtimensec	INTEGER		NOT NULL,
	ctimesec	INT8		NOT NULL,
	ctimensec	INTEGER		NOT NULL
);

CREATE TABLE FileInfo (
	inumber		INT8		PRIMARY KEY
		REFERENCES INode(inumber) ON DELETE CASCADE,
	checksumType	VARCHAR(32)	NOT NULL,
	checksum	VARCHAR(256)	NOT NULL
);

CREATE TABLE FileCopy (
	inumber		INT8		NOT NULL,
	hostname	VARCHAR(256)	NOT NULL,
	PRIMARY KEY(inumber, hostname)
);

CREATE INDEX fileCopyByINode ON FileCopy (inumber);

CREATE TABLE DeadFileCopy (
	inumber		INT8		NOT NULL,
	igen		INT8		NOT NULL,
	hostname	VARCHAR(256)	NOT NULL,
	PRIMARY KEY(inumber, igen, hostname)
);

CREATE INDEX deadFileCopyByHostname ON DeadFileCopy (hostname);

CREATE TABLE DirEntry (
	dirINumber	INT8		NOT NULL,
	entryName	VARCHAR(255)	NOT NULL,
	entryINumber	INT8		NOT NULL,
	PRIMARY KEY(dirINumber, entryName)
);

CREATE INDEX dirEntryByINode ON DirEntry (dirINumber);

CREATE TABLE Symlink (
	inumber		INT8		PRIMARY KEY,
	sourcePath	VARCHAR(1024)	NOT NULL
);

CREATE TABLE XAttr (
	inumber		INT8		NOT NULL
		REFERENCES INode(inumber) ON DELETE CASCADE,
	attrname	VARCHAR(256)	NOT NULL,
	attrvalue	BYTEA	NOT NULL,
	PRIMARY KEY(inumber, attrname)
);

CREATE TABLE QuotaUser (
	username	VARCHAR(64)	PRIMARY KEY
		REFERENCES GfarmUser(username) ON DELETE CASCADE,
	gracePeriod	INT8	NOT NULL,
	fileSpace	INT8	NOT NULL,
	fileSpaceExceed	INT8	NOT NULL,
	fileSpaceSoft	INT8	NOT NULL,
	fileSpaceHard	INT8	NOT NULL,
	fileNum		INT8	NOT NULL,
	fileNumExceed	INT8	NOT NULL,
	fileNumSoft	INT8	NOT NULL,
	fileNumHard	INT8	NOT NULL,
	phySpace	INT8	NOT NULL,
	phySpaceExceed	INT8	NOT NULL,
	phySpaceSoft	INT8	NOT NULL,
	phySpaceHard	INT8	NOT NULL,
	phyNum		INT8	NOT NULL,
	phyNumExceed	INT8	NOT NULL,
	phyNumSoft	INT8	NOT NULL,
	phyNumHard	INT8	NOT NULL
);

CREATE TABLE QuotaGroup (
	groupname	VARCHAR(8192)	PRIMARY KEY
		 REFERENCES GfarmGroup(groupname) ON DELETE CASCADE,
	gracePeriod	INT8	NOT NULL,
	fileSpace	INT8	NOT NULL,
	fileSpaceExceed	INT8	NOT NULL,
	fileSpaceSoft	INT8	NOT NULL,
	fileSpaceHard	INT8	NOT NULL,
	fileNum		INT8	NOT NULL,
	fileNumExceed	INT8	NOT NULL,
	fileNumSoft	INT8	NOT NULL,
	fileNumHard	INT8	NOT NULL,
	phySpace	INT8	NOT NULL,
	phySpaceExceed	INT8	NOT NULL,
	phySpaceSoft	INT8	NOT NULL,
	phySpaceHard	INT8	NOT NULL,
	phyNum		INT8	NOT NULL,
	phyNumExceed	INT8	NOT NULL,
	phyNumSoft	INT8	NOT NULL,
	phyNumHard	INT8	NOT NULL
);

CREATE TABLE QuotaDirSet (
	username	VARCHAR(64)	NOT NULL
		REFERENCES GfarmUser(username) ON DELETE CASCADE,
	dirSetName	VARCHAR(64)	NOT NULL,
	gracePeriod	INT8	NOT NULL,
	fileSpace	INT8	NOT NULL,
	fileSpaceExceed	INT8	NOT NULL,
	fileSpaceSoft	INT8	NOT NULL,
	fileSpaceHard	INT8	NOT NULL,
	fileNum		INT8	NOT NULL,
	fileNumExceed	INT8	NOT NULL,
	fileNumSoft	INT8	NOT NULL,
	fileNumHard	INT8	NOT NULL,
	phySpace	INT8	NOT NULL,
	phySpaceExceed	INT8	NOT NULL,
	phySpaceSoft	INT8	NOT NULL,
	phySpaceHard	INT8	NOT NULL,
	phyNum		INT8	NOT NULL,
	phyNumExceed	INT8	NOT NULL,
	phyNumSoft	INT8	NOT NULL,
	phyNumHard	INT8	NOT NULL,
	PRIMARY KEY(username, dirSetName)
);

CREATE TABLE QuotaDirectory (
	inumber		INT8		PRIMARY KEY
		REFERENCES INode(inumber) ON DELETE CASCADE,
	username	VARCHAR(64)	NOT NULL,
	dirSetName	VARCHAR(64)	NOT NULL
);

CREATE TABLE SeqNum (
	name		VARCHAR(256)	PRIMARY KEY,
	value		INT8	NOT NULL
);

CREATE TABLE MdHost (
	hostname	VARCHAR(256)	PRIMARY KEY,
	port		INTEGER		NOT NULL,
	clustername	VARCHAR(256)	NOT NULL,
	flags		INTEGER		NOT NULL
);
