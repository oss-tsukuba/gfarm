CREATE TABLE Host (
	hostname	VARCHAR(256)	PRIMARY KEY,
	architecture	VARCHAR(128)	NOT NULL,
	ncpu		INTEGER
);


CREATE TABLE HostAliases (
	hostalias	VARCHAR(256)	PRIMARY KEY,
	hostname	VARCHAR(256)	REFERENCES Host(hostname) ON DELETE CASCADE
);

CREATE INDEX HostAliasesByHostname ON HostAliases (hostname);


CREATE TABLE Path (
	pathname	VARCHAR(1024)	PRIMARY KEY,
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


CREATE TABLE FileSection (
	pathname	VARCHAR(1024),
	section		VARCHAR(256),
	filesize	INT8		NOT NULL,
	checksumType	VARCHAR(32),
	checksum	VARCHAR(256),
	PRIMARY KEY(pathname, section)
);

CREATE INDEX fileSectionByPath ON FileSection (pathname);


CREATE TABLE FileSectionCopy (
	pathname	VARCHAR(1024),
	section		VARCHAR(256),
	hostname	VARCHAR(256),
	PRIMARY KEY(pathname, section, hostname)
);

CREATE INDEX fileSectionCopyByPath ON FileSectionCopy (pathname);

CREATE INDEX fileSectionCopyByFileSection ON FileSectionCopy (pathname, section);
