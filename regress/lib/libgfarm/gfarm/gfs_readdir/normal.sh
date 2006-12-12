#!/bin/sh

. ./regress.conf

#trap 'gfrm -rf $gftmp; exit $exit_trap' $trap_sigs

if $testbin/../gfs_mkdir/mkdir $gftmp 0755 2>&1 1>/dev/null &&

    # 0x00000401 is GFARM_FILE_WRONLY|GFARM_FILE_TRUNC
    $testbin/../gfs_pio_create/create $gftmp/tmpfile.$$ 0x00000401 0666 &&

    # arguments are <directory> <concatinated string of filenames>
    $shell $testbase/readdir.sh $gftmp ".""..""tmpfile.$$"

then
    exit_code=$?
fi

gfrm -r $gftmp
exit $exit_code
