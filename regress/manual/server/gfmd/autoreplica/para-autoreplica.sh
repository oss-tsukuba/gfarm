#!/bin/bash

##### parameters #####
MAXRETRY=30 # sec.
DEBUG=1

##### arguments #####
if [ $# -lt 4 ]; then
  name=`basename $0`
  echo "usage: $name n_attempt size(MB) n_para gfarm.ncopy [hostname]"
  exit 1
fi
n_attempt=$1
filesize=$2
n_para=$3
n_copy=$4
if [ $# -ge 5 ]; then
  host=$5
else
  host=
fi

pwd=`pwd`
name=`basename $pwd`
if [ x${name} != xregress -o ! -f regress.conf ]; then
  cd regress > /dev/null 2>&1
  pwd=`pwd`
  name=`basename $pwd`
  if [ x${name} != xregress -o ! -f regress.conf ]; then
    echo Please change the working directory to {Gfarm source}/regress
    exit $exit_unsupported
  fi
fi

[ ! -f ./regress.conf ] && exit $exit_unsupported
. ./regress.conf
[ -z $localtmp ] && exit $exit_unsupported

TMPFILE=$localtmp
GDIR=$gftmp
GFILE=$GDIR/file

##### functions #####
clean_all() {
  for ((i = 1; i <= $n_para; i++)) {
    gfrm -f $GFILE-$i
  }
  gfrmdir $GDIR > /dev/null 2>&1
  rm ${TMPFILE}* > /dev/null 2>&1
}

stop_childlen() {
  #echo interrupted
  for ((i = 1; i <= $n_para; i++)) {
    if [ -n ${procs[i]} ]; then
      kill ${procs[i]}
    fi
  }
  wait
}

my_lock() {
  if [ $FLOCK_OK -eq 1 ]; then
    flock -x 10
  fi
}

my_unlock() {
  if [ $FLOCK_OK -eq 1 ]; then
    flock -u 10
  fi
}

my_print() {
  my_lock
  echo $@
  my_unlock
}

interrupt() {
  intr=1
}

### child process
test_overwrite() {
  trap 'interrupt' $trap_sigs
  intr=0
  N=$1  # process index
  parent_pid=$2
  F=$GFILE-$N
  req_ok=0
  size=17
  tmp_data=${TMPFILE}-${N}-data
  tmp_data2=${TMPFILE}-${N}-data2

  if [ $FLOCK_OK -eq 1 ]; then
    exec 10>> $TMPFILE
  fi
  for ((i = 1; i <= $n_attempt && $intr == 0; i++)) {
    offset=$i
    if ((`expr $i % 1000` == 0)); then
      my_print [$N:$i] progress
    fi
    dd if=/dev/urandom bs=1 count=${size} 2> /dev/null > $tmp_data
    if cat $tmp_data | gfreg -o $offset -s ${size} - $F; then
      :
    else
      kill $parent_pid
      exit 1
    fi
    if false; then ### true for debug
      my_lock
      echo [$N:$i] start
      od -x $tmp_data
      gfwhere -la $F
      gfstat $F
      my_unlock
    fi

    gfncopy -w $F
    if [ $? -ne 0 ]; then
      my_lock
      echo [$N:$i]@$h: replicas were not created
      gfwhere -al $F
      my_unlock
      kill $parent_pid
      exit 1
    fi

    ### compare data
    for h in `gfwhere $F`; do
      gfexport -s ${size} -o $offset -h $h $F > $tmp_data2
      cmp $tmp_data $tmp_data2 > /dev/null 2>&1
      if [ $? -ne 0 ]; then
        my_lock
        echo [$N:$i]@$h: different data
        od -x $tmp_data
        od -x $tmp_data2
        gfwhere -al $F
        my_unlock
        kill $parent_pid
        exit 1
      elif false; then ### true for debug
        my_lock
        echo [$N:$i]@$h: same data
        od -x $tmp_data
        od -x $tmp_data2
        gfwhere -al $F
        my_unlock
      fi
    done

    num=`gfncopy -c $F`
    if (($DEBUG != 0 && $intr == 0)); then
      my_print [$N:$i] OK num=$num
    fi
  }
  exit 0
}

set_ncopy() {
  if gfncopy -s $1 $2; then
    :
   else
    echo failed gfncopy -s
    clean_test
    exit $exit_fail
  fi
}

clean_exit() {
  clean_all
  exit $1
}

##### main #####
clean_all

if [ ! -e $GDIR ]; then
  gfmkdir $GDIR || exit $exit_fail
fi
trap 'clean_all; exit $exit_trap' $trap_sigs

echo "craete tmpfile: $TMPFILE: $filesize MB"
dd if=/dev/urandom of=$TMPFILE bs=1M count=$filesize > /dev/null 2>&1
if (($? != 0)); then
  echo dd failed
  exit $exit_fail
fi
echo "register files to Gfarm: $filesize MB * $n_para"
for ((i = 1; i <= $n_para; i++)) {
  if [ -z $host ]; then
    gfreg $TMPFILE $GFILE-$i || clean_exit $exit_fail
  else
    gfreg -h $host $TMPFILE $GFILE-$i || clean_exit $exit_fail
  fi
}

FLOCK_OK=0
exec 10>> $TMPFILE
flock -e 10 > /dev/null 2>&1
if [ $? -eq 0 ]; then
  FLOCK_OK=1
  flock -u 10
  exec 10>&-
fi

set_ncopy $n_copy $GDIR

printf '%s' "set gfarm.ncopy="
gfxattr -g $GDIR gfarm.ncopy
echo

echo "start overwrite test: $n_attempt times * $n_para parallels"
trap 'echo interrupt; stop_childlen; clean_all; exit $exit_fail' $trap_sigs
procs=()
for ((i = 1; i <= $n_para; i++)) {
  test_overwrite $i $$ &
  procs[$i]=$!
}
wait

clean_all

exit $exit_pass
