#!/bin/sh

. ./regress.conf

trap 'rm -f $localtmp; exit $exit_trap' $trap_sigs

localtmp=$localtop/RT_cp_exec.$$

cp $data/1byte $localtmp
chmod 755 $localtmp

$testbase/cp.sh $localtmp
exit_code=$?

rm -f $localtmp
exit $exit_code
