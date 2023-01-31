#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

test_gfchmod_M()
{
    mtime="$1"
    expect="$2"

    if gfmkdir $gftmp &&
       gfchmod -M $mtime 755 $gftmp &&
       [ x"`gfls -ldT $gftmp | awk '{ print $6,$7,$8,$9 }'`" = x"$expect" ]
    then
	exit_code=$exit_pass
    fi
    gfrmdir $gftmp
}

# $ touch -d @1234567890 /tmp/aaa; TZ=UTC ls -l --full-time /tmp/aaa
# -rw-r--r-- 1 user1 user1 0 2009-02-13 23:31:30.000000000 +0000 /tmp/aaa
#
# $ gfmkdir -M 1234567890 /tmp/AAA; TZ=UTC gfls -ldT /tmp/AAA
# drwxr-xr-x 2 user1    gfarmadm          0 Feb 13 23:31:30 2009 /tmp/AAA

TZ=UTC
export UTC
test_gfchmod_M 1234567890 "Feb 13 23:31:30 2009"

exit $exit_code
