#!/bin/sh

# test SF.net #942 - mtime inconsistency between gfs_pio_stat() and gfs_stat()
# NOTE: failure of "-c" is just OK, because gfs_pio_write(3) changes st_ctime

. ./regress.conf
datafile=$data/1byte
gftmpfile=$gftmp/file

exit_code=$exit_trap
trap 'gfrm -rf $gftmp; exit $exit_code' 0 $trap_sigs

if remote=`$regress/bin/get_remote_gfhost`; then
  :
else
  # SF.net #942 doesn't happen if it's local
  exit_code=$exit_unsupported
  exit
fi

if
  # disable automatic replication
  gfmkdir $gftmp &&
  gfncopy -s 1 $gftmp &&

  gfreg -h $remote $data/1byte $gftmpfile &&
  $testbase/gfs_stat_pio_stat_consistency -h $remote -amNr $gftmpfile &&
  gfrm -f $gftmpfile &&
  $testbase/gfs_stat_pio_stat_consistency -h $remote -amNw $gftmpfile &&
  gfrm -f $gftmpfile &&
  gfreg -h $remote $data/1byte $gftmpfile &&
  $testbase/gfs_stat_pio_stat_consistency -h $remote -amRr $gftmpfile &&
  gfrm -f $gftmpfile &&
  $testbase/gfs_stat_pio_stat_consistency -h $remote -amR  $gftmpfile &&
  gfrm -f $gftmpfile &&
  $testbase/gfs_stat_pio_stat_consistency -h $remote -amWw $gftmpfile \
	<$datafile
then
  # although #942 doesn't happen, test local case to make sure
  if local=`$regress/bin/get_local_gfhost`; then
    gfrm -f $gftmpfile
    if
      gfreg -h $local $data/1byte $gftmpfile &&
      $testbase/gfs_stat_pio_stat_consistency -h $local -amNr $gftmpfile &&
      gfrm -f $gftmpfile &&
      $testbase/gfs_stat_pio_stat_consistency -h $local -amNw $gftmpfile &&
      gfrm -f $gftmpfile &&
      gfreg -h $local $data/1byte $gftmpfile &&
      $testbase/gfs_stat_pio_stat_consistency -h $local -amRr $gftmpfile &&
      gfrm -f $gftmpfile &&
      $testbase/gfs_stat_pio_stat_consistency -h $local -amR  $gftmpfile &&
      gfrm -f $gftmpfile &&
      $testbase/gfs_stat_pio_stat_consistency -h $local -amWw $gftmpfile \
	<$datafile
    then
      exit_code=$exit_pass
    else
      exit_code=$exit_fail
    fi
  else
    exit_code=$exit_pass
  fi
else
  exit_code=$exit_fail
fi
