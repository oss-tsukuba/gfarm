#!/bin/sh

. ./regress.conf

srcpath=$gftmp/a-src
dstpath=$gftmp/a-dst
srcdir=$gftmp/b-src
dstdir=$gftmp/b-dst

clean_test() {
	rm -f $localtmp 2> /dev/null
	gfrm $dstpath $dstdir $srcpath 2> /dev/null
	gfrmdir $srcdir $gftmp 2> /dev/null
}

_exit_fail() {
	clean_test
	exit $exit_fail
}

trap 'clean_test; exit $exit_trap' $trap_sigs

gfmkdir $gftmp $srcdir
if [ "$?" != "0" ]; then
	echo failed: gfmkdir $gftmp $srcdir
	_exit_fail
fi

echo test > $localtmp
gfreg $localtmp $srcpath
if [ "$?" != "0" ]; then
	echo failed: gfreg $localtmp $srcpath
	_exit_fail
fi

gfln -s $srcpath $dstpath
if [ "$?" != "0" ]; then
	echo failed: gfln -s $srcpath $dstpath
	_exit_fail
fi

gfls -l $dstpath | grep " -> $srcpath"'$' >/dev/null
if [ "$?" != "0" ]; then
	gfls -lR $gftmp
	echo output of gfls does not include \"-\> $srcpath\"
	_exit_fail
fi

gfln -s $srcdir $dstdir
if [ "$?" != "0" ]; then
	echo failed: gfln -s $srcdir $dstdir
	_exit_fail
fi

gfls -l $dstdir/.. | grep " -> $srcdir"'$' >/dev/null
if [ "$?" != "0" ]; then
	echo output of gfls does not include \"-\> $srcdir\"
	_exit_fail
fi

clean_test
exit $exit_pass
