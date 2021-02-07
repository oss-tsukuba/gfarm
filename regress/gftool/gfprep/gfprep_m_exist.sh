#!/bin/sh

. ./regress.conf

GFPREP_DIR=`dirname $0`
. ${GFPREP_DIR}/setup_gfprep.sh

[ `gfsched -w | wc -l` -ge 2 ] || exit $exit_unsupported
setup_test

$GFPREP -N 2 gfarm:$gf_dir1/dir/1byte
check_N $gf_dir1/dir/1byte 2
if gfwhere $gf_dir1/dir/1byte > $local_tmpfile1; then
  :
else
  echo gfwhere failed
  clean_test
  exit $exit_fail
fi

GFSD1=`cut -d " " -f 1 $local_tmpfile1`
GFSD2=`cut -d " " -f 2 $local_tmpfile1`

OPT=""
CMD="$GFPREP $OPT -m -S $GFSD1 -D $GFSD2 gfarm:${gf_dir1}"
if $CMD; then
  :
else
  echo "failed: $CMD"
  clean_test
  exit $exit_fail
fi
check_N $gf_dir1/dir/1byte 1

GFSD2_MIGRATED=`gfwhere $gf_dir1/dir/1byte`

if [ $GFSD2 != $GFSD2_MIGRATED ]; then
  echo "different target"
  clean_test
  exit $exit_fail
fi

clean_test
exit $exit_pass
