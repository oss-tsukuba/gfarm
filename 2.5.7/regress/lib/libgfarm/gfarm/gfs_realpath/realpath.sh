#!/bin/sh

. ./regress.conf
. ${regress}/lib/libgfarm/gfarm/gfm_inode_or_name_op_test/\
gfm_inode_or_name_op_test.conf

check_realpath() {
	testno=$1
	expected=$2
	tgt=$3
	echo "check_realpath[$testno]: expected=$expected tgt=$tgt"
	tgt_test=`$gfs_realpath $tgt`
	check_ok
	echo "check_realpath[$testno]: tgt_test=$tgt_test"
	if [ "$tgt_test" != "$expected" ]; then
		error tgt_test is different to expected
	fi
}

check_realpath_not_exist() {
	testno=$1
	tgt=$2
	echo "check_realpath_not_exist[$testno]: tgt=$tgt"
	tgt_test=`$gfs_realpath $tgt`
	if [ "$?" != "3" ]; then
		error invalid exit code : $?
	fi
}

check_realpath_loop() {
	testno=$1
	tgt=$2
	echo "check_realpath_loop[$testno]: tgt=$tgt"
	ino_tgt=`$gfs_realpath $tgt`
	if [ "$?" != "5" ]; then
		error invalid exit code : $?
	fi
}

clean_test() {
	rm -f $localtmp 2>/dev/null
	clean_file_structure
}

trap 'clean_test; exit $exit_trap' $trap_sigs

# setup conditions
setup_file_structure

mds=gfarm://`$get_svr`
prefix=$mds$R_d

# check realpath
check_realpath  1 $prefix/A/a1      $R_d/toa1
check_realpath  2 $prefix/A/a1      $mds$R_d/toa1
check_realpath  3 $prefix/A         $R_d/B/toA
check_realpath  4 $prefix/A/AA      $R_d/B/BB/toAA
check_realpath  5 $prefix/A/AA/a3   $R_d/B/BB/toa3
check_realpath  6 $mds/             ..
check_realpath  7 $mds/             /..
check_realpath  8 $mds/             $R_d/../../../../../..
check_realpath  9 $prefix           $R_d/../../../../../..$R_d
check_realpath 10 $mds/             $R_d/C/toRoot

check_realpath 11 $prefix/A/a1      ////$R_d/toa1
check_realpath 12 $prefix/A/a1      //././././//$R_d/toa1
check_realpath 13 $prefix/A/a1      $R_d////././/.//.//toa1
check_realpath 14 $prefix/A/a1      $R_d/A/../toa1
check_realpath 15 $prefix/A/a1      $R_d/A/..///toa1
check_realpath 16 $prefix/A/a1      $R_d/A/..//././/toa1

check_realpath 21 $prefix/A/AA/a3   $R_d/B/BB/toAA/a3
check_realpath 22 $prefix/A/AA/a3   ////$R_d/B/BB/toAA/a3
check_realpath 23 $prefix/A/AA/a4   $R_d/B/.//./BB/.//./toAA/././toC/toa4
check_realpath 24 $prefix/A         $R_d/B/./BB/../BB/./toAA/../AA/./toC/../A

check_realpath_not_exist 1 $toNotExist_l
check_realpath_loop      1 $toLL_l

# exit
clean_test
exit $exit_pass

