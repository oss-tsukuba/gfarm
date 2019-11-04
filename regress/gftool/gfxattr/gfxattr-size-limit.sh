#!/bin/sh

. ./regress.conf

xattr_size_limit=65536
attrname=user.longvalue

case $# in
1) xattr_size_limit=$1;;
2) xattr_size_limit=$1; option=$2;;
esac

trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs
trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_code' 0

gendata()
{
awk 'BEGIN{
sz='$1';
a64="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
a1024=a64 a64 a64 a64 a64 a64 a64 a64 a64 a64 a64 a64 a64 a64 a64 a64;
a="";
while (sz>=1024) { a=a a1024; sz-=1024 };
while (sz>=64) { a=a a64; sz -= 64};
while (sz>=1) { a = a "a"; --sz };
printf "%s", a;
exit}'
}

gfreg $data/0byte $gftmp || exit
( gendata $xattr_size_limit | gfxattr -s $option $gftmp $attrname ) || exit
( gendata `expr $xattr_size_limit + 1` |
	gfxattr -s $option $gftmp $attrname 2>$localtmp ) && exit
grep 'argument list too long' $localtmp >/dev/null || exit
exit_code=0
