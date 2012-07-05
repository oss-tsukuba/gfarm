#!/bin/sh

. ./regress.conf

GFPREP_DIR=`dirname $0`
. ${GFPREP_DIR}/setup_gfprep.sh

[ `gfsched -w | wc -l` -ge 2 ] || exit $exit_unsupported
setup_test

OPT='-d'
if gfprep $OPT -N 2 gfarm:${gf_dir1}; then
  :
else
  echo failed: gfprep $OPT -N 2
  clean_test
  exit $exit_fail
fi
check_N $gf_dir1/dir/1byte 2

if gfprep $OPT -N 1 -x gfarm:${gf_dir1}; then
  :
else
  echo failed: gfprep $OPT -N 1 -x
  clean_test
  exit $exit_fail
fi
check_N $gf_dir1/dir/1byte 1

clean_test
exit $exit_pass
