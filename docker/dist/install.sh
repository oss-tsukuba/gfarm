#!/bin/sh
set -xeu

: ${PKG:=gfarm}

cd $PKG

CONF_OPT=
case $PKG in
gfarm)
	CONF_OPT=--with-globus #--with-infiniband
	;;
gfarm2fs)
	CONF_OPT=--with-gfarm=/usr/local
	;;
esac

./configure $CONF_OPT
make clean > /dev/null
make -j $(nproc) > /dev/null

for h in c1 c2 c3 c4
do
	ssh $h "(cd $PWD; sudo make install > /dev/null)"
done
