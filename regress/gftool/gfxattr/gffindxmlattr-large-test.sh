#!/bin/bash

mountp="./fusetest"
xmltest="./xmltest"

gfmkdir A
gfmkdir B
gfmkdir C
gfmkdir D
gfmkdir A/A
gfmkdir A/B
gfmkdir A/C
gfmkdir A/D
gfmkdir B/A
gfmkdir B/B
gfmkdir B/C
gfmkdir B/D
gfmkdir C/A
gfmkdir C/B
gfmkdir C/C
gfmkdir C/D
gfmkdir D/A
gfmkdir D/B
gfmkdir D/C
gfmkdir D/D

mkdir -p $mountp
gfarm2fs $mountp

touch $mountp/a
touch $mountp/b
touch $mountp/c
touch $mountp/d
touch $mountp/A/a
touch $mountp/A/b
touch $mountp/A/c
touch $mountp/A/d
touch $mountp/B/a
touch $mountp/B/b
touch $mountp/B/c
touch $mountp/B/d
touch $mountp/C/a
touch $mountp/C/b
touch $mountp/C/c
touch $mountp/C/d
touch $mountp/D/a
touch $mountp/D/b
touch $mountp/D/c
touch $mountp/D/d
touch $mountp/A/A/a
touch $mountp/A/A/b
touch $mountp/A/A/c
touch $mountp/A/A/d
touch $mountp/A/B/a
touch $mountp/A/B/b
touch $mountp/A/B/c
touch $mountp/A/B/d
touch $mountp/A/C/a
touch $mountp/A/C/b
touch $mountp/A/C/c
touch $mountp/A/C/d
touch $mountp/A/D/a
touch $mountp/A/D/b
touch $mountp/A/D/c
touch $mountp/A/D/d
touch $mountp/B/A/a
touch $mountp/B/A/b
touch $mountp/B/A/c
touch $mountp/B/A/d
touch $mountp/B/B/a
touch $mountp/B/B/b
touch $mountp/B/B/c
touch $mountp/B/B/d
touch $mountp/B/C/a
touch $mountp/B/C/b
touch $mountp/B/C/c
touch $mountp/B/C/d
touch $mountp/B/D/a
touch $mountp/B/D/b
touch $mountp/B/D/c
touch $mountp/B/D/d
touch $mountp/C/A/a
touch $mountp/C/A/b
touch $mountp/C/A/c
touch $mountp/C/A/d
touch $mountp/C/B/a
touch $mountp/C/B/b
touch $mountp/C/B/c
touch $mountp/C/B/d
touch $mountp/C/C/a
touch $mountp/C/C/b
touch $mountp/C/C/c
touch $mountp/C/C/d
touch $mountp/C/D/a
touch $mountp/C/D/b
touch $mountp/C/D/c
touch $mountp/C/D/d
touch $mountp/D/A/a
touch $mountp/D/A/b
touch $mountp/D/A/c
touch $mountp/D/A/d
touch $mountp/D/B/a
touch $mountp/D/B/b
touch $mountp/D/B/c
touch $mountp/D/B/d
touch $mountp/D/C/a
touch $mountp/D/C/b
touch $mountp/D/C/c
touch $mountp/D/C/d
touch $mountp/D/D/a
touch $mountp/D/D/b
touch $mountp/D/D/c
touch $mountp/D/D/d

gfxattr -s -c -x -f $xmltest/aclxml    a acl
gfxattr -s -c -x -f $xmltest/detailxml b detail
gfxattr -s -c -x -f $xmltest/logxml    c log
gfxattr -s -c -x -f $xmltest/testxml   d test
gfxattr -s -c -x -f $xmltest/aclxml    A/a acl
gfxattr -s -c -x -f $xmltest/detailxml A/b detail
gfxattr -s -c -x -f $xmltest/logxml    A/c log
gfxattr -s -c -x -f $xmltest/testxml   A/d test
gfxattr -s -c -x -f $xmltest/aclxml    B/a acl
gfxattr -s -c -x -f $xmltest/detailxml B/b detail
gfxattr -s -c -x -f $xmltest/logxml    B/c log
gfxattr -s -c -x -f $xmltest/testxml   B/d test
gfxattr -s -c -x -f $xmltest/aclxml    C/a acl
gfxattr -s -c -x -f $xmltest/detailxml C/b detail
gfxattr -s -c -x -f $xmltest/logxml    C/c log
gfxattr -s -c -x -f $xmltest/testxml   C/d test
gfxattr -s -c -x -f $xmltest/aclxml    D/a acl
gfxattr -s -c -x -f $xmltest/detailxml D/b detail
gfxattr -s -c -x -f $xmltest/logxml    D/c log
gfxattr -s -c -x -f $xmltest/testxml   D/d test

gfxattr -s -c -x -f $xmltest/aclxml    A/A/a acl
gfxattr -s -c -x -f $xmltest/detailxml A/A/b detail
gfxattr -s -c -x -f $xmltest/logxml    A/A/c log
gfxattr -s -c -x -f $xmltest/testxml   A/A/d test
gfxattr -s -c -x -f $xmltest/aclxml    A/B/a acl
gfxattr -s -c -x -f $xmltest/detailxml A/B/b detail
gfxattr -s -c -x -f $xmltest/logxml    A/B/c log
gfxattr -s -c -x -f $xmltest/testxml   A/B/d test
gfxattr -s -c -x -f $xmltest/aclxml    A/C/a acl
gfxattr -s -c -x -f $xmltest/detailxml A/C/b detail
gfxattr -s -c -x -f $xmltest/logxml    A/C/c log
gfxattr -s -c -x -f $xmltest/testxml   A/C/d test
gfxattr -s -c -x -f $xmltest/aclxml    A/D/a acl
gfxattr -s -c -x -f $xmltest/detailxml A/D/b detail
gfxattr -s -c -x -f $xmltest/logxml    A/D/c log
gfxattr -s -c -x -f $xmltest/testxml   A/D/d test

gfxattr -s -c -x -f $xmltest/aclxml    B/A/a acl
gfxattr -s -c -x -f $xmltest/detailxml B/A/b detail
gfxattr -s -c -x -f $xmltest/logxml    B/A/c log
gfxattr -s -c -x -f $xmltest/testxml   B/A/d test
gfxattr -s -c -x -f $xmltest/aclxml    B/B/a acl
gfxattr -s -c -x -f $xmltest/detailxml B/B/b detail
gfxattr -s -c -x -f $xmltest/logxml    B/B/c log
gfxattr -s -c -x -f $xmltest/testxml   B/B/d test
gfxattr -s -c -x -f $xmltest/aclxml    B/C/a acl
gfxattr -s -c -x -f $xmltest/detailxml B/C/b detail
gfxattr -s -c -x -f $xmltest/logxml    B/C/c log
gfxattr -s -c -x -f $xmltest/testxml   B/C/d test
gfxattr -s -c -x -f $xmltest/aclxml    B/D/a acl
gfxattr -s -c -x -f $xmltest/detailxml B/D/b detail
gfxattr -s -c -x -f $xmltest/logxml    B/D/c log
gfxattr -s -c -x -f $xmltest/testxml   B/D/d test

gfxattr -s -c -x -f $xmltest/aclxml    C/A/a acl
gfxattr -s -c -x -f $xmltest/detailxml C/A/b detail
gfxattr -s -c -x -f $xmltest/logxml    C/A/c log
gfxattr -s -c -x -f $xmltest/testxml   C/A/d test
gfxattr -s -c -x -f $xmltest/aclxml    C/B/a acl
gfxattr -s -c -x -f $xmltest/detailxml C/B/b detail
gfxattr -s -c -x -f $xmltest/logxml    C/B/c log
gfxattr -s -c -x -f $xmltest/testxml   C/B/d test
gfxattr -s -c -x -f $xmltest/aclxml    C/C/a acl
gfxattr -s -c -x -f $xmltest/detailxml C/C/b detail
gfxattr -s -c -x -f $xmltest/logxml    C/C/c log
gfxattr -s -c -x -f $xmltest/testxml   C/C/d test
gfxattr -s -c -x -f $xmltest/aclxml    C/D/a acl
gfxattr -s -c -x -f $xmltest/detailxml C/D/b detail
gfxattr -s -c -x -f $xmltest/logxml    C/D/c log
gfxattr -s -c -x -f $xmltest/testxml   C/D/d test

gfxattr -s -c -x -f $xmltest/aclxml    D/A/a acl
gfxattr -s -c -x -f $xmltest/detailxml D/A/b detail
gfxattr -s -c -x -f $xmltest/logxml    D/A/c log
gfxattr -s -c -x -f $xmltest/testxml   D/A/d test
gfxattr -s -c -x -f $xmltest/aclxml    D/B/a acl
gfxattr -s -c -x -f $xmltest/detailxml D/B/b detail
gfxattr -s -c -x -f $xmltest/logxml    D/B/c log
gfxattr -s -c -x -f $xmltest/testxml   D/B/d test
gfxattr -s -c -x -f $xmltest/aclxml    D/C/a acl
gfxattr -s -c -x -f $xmltest/detailxml D/C/b detail
gfxattr -s -c -x -f $xmltest/logxml    D/C/c log
gfxattr -s -c -x -f $xmltest/testxml   D/C/d test
gfxattr -s -c -x -f $xmltest/aclxml    D/D/a acl
gfxattr -s -c -x -f $xmltest/detailxml D/D/b detail
gfxattr -s -c -x -f $xmltest/logxml    D/D/c log
gfxattr -s -c -x -f $xmltest/testxml   D/D/d test

gffindxmlattr //name /A/B
gffindxmlattr //name /A
gffindxmlattr //name /
