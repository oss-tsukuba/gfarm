#!/bin/sh

. ./regress.conf

GFPREP_DIR=`dirname $0`
. ${GFPREP_DIR}/setup_gfprep.sh

[ `gfsched -w | wc -l` -ge 2 ] || exit $exit_unsupported
setup_test

check_N $gf_dir1/dir/1byte 1
if gfwhere $gf_dir1/dir/1byte > $local_tmpfile1; then
  :
else
  echo gfwhere failed
  clean_test
  exit $exit_fail
fi

OPT=""
if cat $local_tmpfile1 | $GFPREP $OPT -m -h - gfarm:${gf_dir1}; then
  :
else
  echo failed: gfprep $OPT -m
  clean_test
  exit $exit_fail
fi
check_N $gf_dir1/dir/1byte 1

if gfwhere $gf_dir1/dir/1byte | diff - $local_tmpfile1 > /dev/null; then
  echo not migrated
  clean_test
  exit $exit_fail
fi

clean_test
exit $exit_pass
