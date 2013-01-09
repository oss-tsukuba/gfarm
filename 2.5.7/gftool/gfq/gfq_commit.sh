#! /bin/sh
#
# $Id$

error() {
	echo $*
	exit 1
}

[ X$GFQ_DIR = X ] && GFQ_DIR=/tmp/.gfq-$USER
[ -d $GFQ_DIR ] || error "$GFS_DIR: no such directory"

gfqbase=`basename $GFQ_DIR`
gfrm -rf gfarm:~/$gfqbase >& /dev/null
gfreg -r $GFQ_DIR gfarm:~ || error "cannot copy $GFQ_DIR to gfarm:~"

pushd $GFQ_DIR > /dev/null
for h in *
do
	gfrcmd $h gfchmod 0755 gfarm:~/$gfqbase/$h
	gfrcmd $h gfexec gfarm:~/$gfqbase/$h &
done
wait

popd > /dev/null
gfrm -rfq gfarm:~/$gfqbase
rm -rf $GFQ_DIR
