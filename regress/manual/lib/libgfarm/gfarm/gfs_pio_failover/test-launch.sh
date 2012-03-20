#!/bin/sh

. ./env.sh

setup_rep() {
	get_gfsd
	gfrep -D $GFSD0 $GF_TMPF
	gfrm -h $GFSD1 $GF_TMPF
}

type=$1
mode=$2

if [ "$mode" = "auto" ]; then
	autoopt="-auto"
fi

case "$type" in

realpath|rename|statfs|chmod|lchmod|chown|lchown|\
stat|lstat|fstat|utimes|lutimes|remove|unlink|link|symlink|\
sched-read|sched-open-write|sched-create-write|close|close-open|close-open2|\
read|read-stat|open-read-loop|getc|seek|seek-dirty|\
write|write-stat|putc|truncate|flush|sync|datasync|\
getxattr|lgetxattr|getattrplus|lgetattrplus|setxattr|lsetxattr|\
removexattr|lremovexattr|fgetxattr|fsetxattr|fremovexattr|\
listxattr|llistxattr|getxmlattr|lgetxmlattr|setxmlattr|lsetxmlattr|\
listxmlattr|llistxmlattr|removexmlattr|lremovexmlattr|\
findxmlattr|getxmlent|closexmlattr|\
shhosts-domainfile|rep-info|rep-list)
	$PROG $autoopt $type $GF_TMPF
	;;

write-long-loop)
	$PROG $autoopt $type $GF_TMPF 120 10 1
	;;

statfsnode)
	get_gfsd
	$PROG $autoopt $type $GFSD0 $GF_TMPF
	;;

readlink)
	$PROG $autoopt $type $GF_TMPF_SLNK
	;;

rmdir|mkdir|\
opendir|opendirplus|opendirplusxattr|\
readdir|readdir2|readdirplus|readdirplusxattr|\
seekdir|seekdirplusxattr)
	$PROG $autoopt $type $GF_TMPF $GF_TMPD
	;;

closedir|closedirplus|closedirplusxattr)
	$PROG $autoopt $type $GF_TMPD
	;;

shhosts|shhosts-domainall)
	get_gfsd
	$PROG $autoopt $type $GFSD0
	;;

rep-to|migrate-to)
	setup_rep
	$PROG $autoopt $type $GF_TMPF $GFSD1 $GFSD1_PORT
	;;

rep-fromto|migrate-fromto)
	setup_rep
	$PROG $autoopt $type $GF_TMPF $GFSD0 $GFSD0_PORT $GFSD1 $GFSD1_PORT
	;;

rep-toreq)
	setup_rep
	$PROG $autoopt $type $GF_TMPF $GFSD1
	;;

rep-fromtoreq)
	setup_rep
	$PROG $autoopt $type $GF_TMPF $GFSD0 $GFSD1
	;;

rep-remove)
	get_gfsd
	gfrep -D $GFSD0 $GF_TMPF
	gfrep -D $GFSD1 $GF_TMPF
	$PROG $autoopt $type $GF_TMPF $GFSD1
	;;

*)
	echo Invalid type: $type
	exit 1
	;;
esac

EXIT_CODE=$?
./teardown.sh

exit $EXIT_CODE
