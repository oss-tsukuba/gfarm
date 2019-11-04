#!/bin/sh

. ./regress.conf

GFPREP_DIR=`dirname $0`
. ${GFPREP_DIR}/setup_gfprep.sh

setup_test

if mkdir $local_dir1 &&
   mkdir $local_dir2; then
  :
else
    echo mkdir failed: $local_dir1 $local_dir2
    clean_test
    exit $exit_fail
fi

test_copy() {
  SIZE=$1
  filename=COPYFILE
  OPT="-b 65536 -f -d -B"
  lfile=$local_dir1/$filename
  gfile=$gf_dir1/$filename
  if dd if=/dev/urandom of=$lfile bs=$SIZE count=1 > /dev/null; then
    :
  else
    echo dd failed
    clean_test
    exit $exit_fail
  fi
  if gfpcopy $OPT file:$lfile gfarm:$gf_dir1; then
    :
  else
    echo gfpcopy failed [local to gfarm]
    clean_test
    exit $exit_fail
  fi
  if gfpcopy $OPT gfarm:$gfile file:$local_dir2; then
    :
  else
    echo gfpcopy failed [gfarm to local]
    clean_test
    exit $exit_fail
  fi
  if cmp $lfile $local_dir2/$filename; then
    :
  else
    echo copied data is different
    clean_test
    exit $exit_fail
  fi
}

test_copy 1
test_copy 65535
test_copy 65536
test_copy 65537

clean_test
exit $exit_pass
