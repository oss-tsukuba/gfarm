#!/bin/sh

. ./regress.conf

GFPREP_DIR=`dirname $0`
. ${GFPREP_DIR}/setup_gfprep.sh

[ `gfsched -w | wc -l` -ge 4 ] || exit $exit_unsupported
setup_test

$GFPREP -N 3 gfarm:$gf_dir1/dir/1byte
check_N $gf_dir1/dir/1byte 3
if gfwhere $gf_dir1/dir/1byte > $local_tmpfile1; then
  :
else
  echo gfwhere failed
  clean_test
  exit $exit_fail
fi

GFSD1=`cut -d " " -f 1 $local_tmpfile1`
GFSD2=`cut -d " " -f 2 $local_tmpfile1`
GFSD3=`cut -d " " -f 3 $local_tmpfile1`

GFSD4=`gfsched | egrep -v ^"$GFSD1"\$ | egrep -v ^"$GFSD2"\$ | egrep -v ^"$GFSD3"\$ | head -1`

SRC_HOSTLIST=$local_tmpfile2
DST_HOSTLIST=$local_tmpfile3

echo $GFSD1 > $SRC_HOSTLIST
echo $GFSD2 >> $SRC_HOSTLIST

echo $GFSD3 > $DST_HOSTLIST
echo $GFSD4 >> $DST_HOSTLIST

OPT=""
CMD="$GFPREP $OPT -m -h $SRC_HOSTLIST -H $DST_HOSTLIST gfarm:${gf_dir1}"
if $CMD; then
  :
else
  echo "failed: $CMD"
  clean_test
  exit $exit_fail
fi

# removed and migrated
check_N $gf_dir1/dir/1byte 2

if gfwhere $gf_dir1/dir/1byte > $local_tmpfile1; then
  :
else
  echo gfwhere failed
  clean_test
  exit $exit_fail
fi

GFSD3_MIGRATED=`cut -d " " -f 1 $local_tmpfile1`
GFSD4_MIGRATED=`cut -d " " -f 2 $local_tmpfile1`

echo $GFSD3_MIGRATED > $local_tmpfile1
echo $GFSD4_MIGRATED >> $local_tmpfile1

if egrep -q ^"$GFSD3"\$ $local_tmpfile1 \
   && egrep -q ^"$GFSD4"\$ $local_tmpfile1; then
    :
else
  echo "unexpected destinations"
  clean_test
  exit $exit_fail
fi

clean_test
exit $exit_pass
