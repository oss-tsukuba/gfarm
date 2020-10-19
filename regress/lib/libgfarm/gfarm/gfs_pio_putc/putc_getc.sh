#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# putc
$gfs_pio_test -c -U 65 $gftmp || exit $exit_code
[ X`gfexport $gftmp` = XA ] || exit $exit_code
# getc
o=`$gfs_pio_test -G $gftmp`
[ X$o = XA ] || exit $exit_code
# ungetc
$gfs_pio_test -G -N $o $gftmp > /dev/null || exit $exit_code
[ X`gfexport $gftmp` = XA ] || exit $exit_code

# getc & putc are not supported in case of GFARM_FILE_UNBUFFERED
[ X`$gfs_pio_test -u -G $gftmp` = XA ] && exit $exit_code
$gfs_pio_test -u -N 65 $gftmp && exit $exit_code
$gfs_pio_test -u -U 65 $gftmp && exit $exit_code
exit_code=$exit_pass

gfrm $gftmp
exit $exit_code
