CREATE TABLE XmlAttr (
	inumber		INT8		NOT NULL
		REFERENCES INode(inumber) ON DELETE CASCADE,
	attrname	VARCHAR(256)	NOT NULL,
	attrvalue	XML	NOT NULL,
	PRIMARY KEY(inumber, attrname)
);
