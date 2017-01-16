#!/bin/sh

. ./regress.conf

GFPREP_DIR=`dirname $0`
. ${GFPREP_DIR}/setup_gfprep.sh

setup_test

check_local_entries() {
  BASE=$1
  failed=0
  if [ -d $BASE/dir ]; then
    :
  else
    failed=1
    echo not a directory: $BASE/dir
  fi
  if [ -f $BASE/dir/0byte ]; then
    :
  else
    failed=1
    echo not a file: $BASE/dir/0byte
  fi
  if [ -s $BASE/dir/0byte ]; then
    failed=1
    echo not a zero byte file: $BASE/dir/0byte
  fi
  if [ -s $BASE/dir/1byte ]; then
    :
  else
    failed=1
    echo not a file: $BASE/dir/1byte
  fi
  if [ -h $BASE/dir/symlink ]; then
    :
  else
    failed=1
    echo not a symlink: $BASE/dir/symlink
  fi
  if [ $failed -eq 1 ]; then
    find $BASE
    clean_test
    exit $exit_fail
  fi
}

OPT="-d -B"

if gfpcopy $OPT gfarm:$gf_dir1 file:$local_dir1; then
  :
else
  echo gfpcopy failed [gfarm to local][1]
  clean_test
  exit $exit_fail
fi
check_local_entries $local_dir1

if gfpcopy $OPT file:$local_dir1 gfarm:$gf_dir2; then
  :
else
  echo gfpcopy failed [local to gfarm]
  clean_test
  exit $exit_fail
fi

if gfpcopy $OPT gfarm:$gf_dir2 file:$local_dir2; then
  :
else
  echo gfpcopy failed [gfarm to local]
  clean_test
  exit $exit_fail
fi
check_local_entries $local_dir2

if gfpcopy $OPT gfarm:$gf_dir2 file:$local_dir2; then
  :
else
  echo gfpcopy failed [gfarm to local][2]
  clean_test
  exit $exit_fail
fi
BASENAME=`basename $gf_dir2`
check_local_entries $local_dir2/$BASENAME

clean_test
exit $exit_pass
