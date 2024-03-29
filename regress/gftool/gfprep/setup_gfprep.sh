gf_dir1=$gftmp/dir1
gf_dir2=$gftmp/dir2
local_dir1=$localtmp/dir1
local_dir2=$localtmp/dir2
local_tmpfile1=$localtmp/tmpfile1
local_tmpfile2=$localtmp/tmpfile2
local_tmpfile3=$localtmp/tmpfile3

GFPREP=$regress/bin/gfprep_for_test
GFPCOPY=$regress/bin/gfpcopy_for_test

clean_test() {
  rm -rf $localtmp
  gfrm -rf $gftmp
}

trap 'clean_test; exit $exit_trap' $trap_sigs

set_ncopy() {
  if gfncopy -s $1 $2; then
    :
  else
    echo failed gfxattr -s
    clean_test
    exit $exit_fail
  fi
}

setup_test() {
  if mkdir $localtmp &&
    gfmkdir $gftmp &&
    gfmkdir $gf_dir1 &&
    gfmkdir $gf_dir1/dir &&
    gfreg $data/0byte $gf_dir1/dir/0byte &&
    gfreg $data/1byte $gf_dir1/dir/1byte &&
    gfln -s 1byte $gf_dir1/dir/symlink; then
    :
  else
    exit $exit_fail
  fi
}

check_N() {
  P=$1
  N=$2
  local_tmpfile_gfstat=$localtmp/tmpfile_gfstat
  if gfstat $P > $local_tmpfile_gfstat; then
    :
  else
    echo failed gfstat
    cat $local_tmpfile_gfstat
    clean_test
    exit $exit_fail
  fi
  if [ `awk '/Ncopy/{print $NF}' $local_tmpfile_gfstat` -eq $N ]; then
    :
  else
    echo "unexpected number of replicas (not $N)"
    gfwhere -al $P
    cat $local_tmpfile_gfstat
    clean_test
    exit $exit_fail
  fi
}
