CREATE TABLE Host (
	hostname	VARCHAR(256)	NOT NULL UNIQUE,
	hostid		SERIAL		PRIMARY KEY,
	architecture	VARCHAR(128)	NOT NULL,
	ncpu		INTEGER,
	available	BOOLEAN		NOT NULL
);


CREATE TABLE HostAliases (
	hostalias	VARCHAR(256)	PRIMARY KEY,
	hostname	VARCHAR(256)	REFERENCES Host(hostname)
);

CREATE INDEX HostAliasesByHostname ON HostAliases (hostname);


CREATE TABLE Path (
	pathname	VARCHAR(1024)	PRIMARY KEY,
	pathid		BIGSERIAL	NOT NULL UNIQUE,
	mode		INTEGER		NOT NULL,
	username	VARCHAR(64)	NOT NULL,
	groupname	VARCHAR(64)	NOT NULL,
	atimesec	INT8		NOT NULL,
	atimensec	INTEGER		NOT NULL,
	mtimesec	INT8		NOT NULL,
	mtimensec	INTEGER		NOT NULL,
	ctimesec	INT8		NOT NULL,
	ctimensec	INTEGER		NOT NULL,
	nsections	INTEGER
);

CREATE INDEX PathByPathid ON Path (pathid);

CREATE TABLE PathXAttr (
	pathid		INT8		PRIMARY KEY
		REFERENCES Path(pathid) ON DELETE CASCADE,
	xattribute	VARCHAR
);

CREATE TABLE FileSection (
	pathid		INT8		NOT NULL REFERENCES Path(pathid),
	section		VARCHAR(256)	NOT NULL,
	filesize	INT8		NOT NULL,
	checksumType	VARCHAR(32),
	checksum	VARCHAR(256),
	PRIMARY KEY(pathid, section)
);

CREATE INDEX fileSectionByPath ON FileSection (pathid);


CREATE TABLE FileSectionCopy (
	pathid		INT8		NOT NULL REFERENCES Path(pathid),
	section		VARCHAR(256)	NOT NULL,
	hostid		INTEGER		NOT NULL REFERENCES Host(hostid),
	PRIMARY KEY(pathid, section, hostid),
	FOREIGN KEY(pathid, section) REFERENCES FileSection
);

CREATE INDEX fileSectionCopyByPath ON FileSectionCopy (pathid);

CREATE INDEX fileSectionCopyByFileSection ON FileSectionCopy (pathid, section);
