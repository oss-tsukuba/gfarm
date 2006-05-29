CREATE TABLE Host (
	hostname	VARCHAR(256)	PRIMARY KEY,
	port		INTEGER		NOT NULL,
	architecture	VARCHAR(128)	NOT NULL,
	ncpu		INTEGER		NOT NULL,
	flags		INTEGER		NOT NULL
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
	groupname	VARCHAR(64)	PRIMARY KEY
);

CREATE TABLE GfarmGroupAssignment (
	username	VARCHAR(64)
		REFERENCES GfarmUser(username) ON DELETE CASCADE,
	groupname	VARCHAR(64)
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
	groupname	VARCHAR(64)	NOT NULL,
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
	entryName	VARCHAR(1024)	NOT NULL,
	entryINumber	INT8		NOT NULL,
	PRIMARY KEY(dirINumber, entryName)
);

CREATE INDEX dirEntryByINode ON DirEntry (dirINumber);
