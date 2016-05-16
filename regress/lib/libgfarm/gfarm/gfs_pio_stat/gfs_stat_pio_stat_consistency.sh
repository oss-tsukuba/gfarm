#!/bin/sh

# test SF.net #942 - mtime inconsistency between gfs_pio_stat() and gfs_stat()
# NOTE: failure of "-c" is just OK, because gfs_pio_write(3) changes st_ctime

. ./regress.conf
datafile=$data/1byte

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

if remote=`$regress/bin/get_remote_gfhost`; then
  :
else
  # SF.net #942 doesn't happen if it's local
  exit $exit_unsupported
fi

if
  gfreg -h $remote $data/1byte $gftmp &&
  $testbase/gfs_stat_pio_stat_consistency -h $remote -amNr $gftmp &&
  gfrm -f $gftmp &&
  $testbase/gfs_stat_pio_stat_consistency -h $remote -amNw $gftmp &&
  gfrm -f $gftmp &&
  gfreg -h $remote $data/1byte $gftmp &&
  $testbase/gfs_stat_pio_stat_consistency -h $remote -amRr $gftmp &&
  gfrm -f $gftmp &&
  $testbase/gfs_stat_pio_stat_consistency -h $remote -amR  $gftmp &&
  gfrm -f $gftmp &&
  $testbase/gfs_stat_pio_stat_consistency -h $remote -amWw $gftmp <$datafile
then
  # although #942 doesn't happen, test local case to make sure
  if local=`$regress/bin/get_local_gfhost`; then
    gfrm -f $gftmp
    if
      gfreg -h $local $data/1byte $gftmp &&
      $testbase/gfs_stat_pio_stat_consistency -h $local -amNr $gftmp &&
      gfrm -f $gftmp &&
      $testbase/gfs_stat_pio_stat_consistency -h $local -amNw $gftmp &&
      gfrm -f $gftmp &&
      gfreg -h $local $data/1byte $gftmp &&
      $testbase/gfs_stat_pio_stat_consistency -h $local -amRr $gftmp &&
      gfrm -f $gftmp &&
      $testbase/gfs_stat_pio_stat_consistency -h $local -amR  $gftmp &&
      gfrm -f $gftmp &&
      $testbase/gfs_stat_pio_stat_consistency -h $local -amWw $gftmp <$datafile
    then
      exit_code=$exit_pass
    fi
  else
    exit_code=$exit_pass
  fi
fi

gfrm -f $gftmp
exit $exit_code
