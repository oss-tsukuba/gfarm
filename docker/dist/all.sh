#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo All set || echo NG: $PROG; exit $status' 0 1 2 15

[ $# -gt 0 ] && build_pkg=true || build_pkg=false

# sanity
[ -f ./install.sh ]
[ -f ./config.sh ]
DISTDIR=$PWD

[ -f ~/.gfarm2rc ] && mv ~/.gfarm2rc ~/.gfarm2rc.bak

# set up .nodelist
sh ./setup.sh

# install Gfarm
if $build_pkg; then
	(cd && sh $DISTDIR/mkrpm.sh)
	sh ./install-rpm.sh
else
	(cd ~/gfarm && sh $DISTDIR/install.sh)
fi
gfarm-pcp -p ~/.nodelist .

# set up certificates
sh ./key.sh
sh ./userkey.sh
sh ./cert.sh
sh ./usercert.sh

# set up Gfarm-1 with 5 nodes
echo c1 c2 c3 c4 c5 | sh ./config.sh -

# set up Gfarm-2 to Gfarm-4 with 1 node
for h in c6 c7 c8; do
	echo $h | ssh $h sh $PWD/config.sh -
done

# Check installation
sh ./check.sh
for h in c6 c7 c8; do
	ssh $h sh $PWD/check.sh
done

# install Gfarm2fs
if $build_pkg; then
	(cd ~/gfarm && PKG=gfarm2fs sh $DISTDIR/mkrpm.sh)
	PKG=gfarm2fs sh ./install-rpm.sh
else
	(cd ~/gfarm/gfarm2fs && PKG=gfarm2fs sh $DISTDIR/install.sh)
fi

status=0
