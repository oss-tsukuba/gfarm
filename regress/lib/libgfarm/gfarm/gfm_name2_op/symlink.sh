#!/bin/sh

. ./regress.conf
. ${regress}/lib/libgfarm/gfarm/gfm_inode_or_name_op_test/\
gfm_inode_or_name_op_test.conf

check_symlink() {
	testno=$1
	open_last=$2
	tgt1=$3
	tgt2=$4
	echo "check_symlink[$testno]:"\
	    "open_last=$open_last"\
	    "tgt1=$tgt1"\
	    "tgt2=$tgt2"
	bn_tgt1=`basename $tgt1`
	bn_tgt2=`basename $tgt2`
	tgt2_parent=`dirname $tgt2`
	ino_tgt1=`$inode_op_nf $tgt1`
	ino_tgt2_parent=`$inode_op_nf $tgt2_parent`
	if $open_last; then
		$name2_op_ol $tgt1 $tgt2 > $localtmp
	else
		$name2_op $tgt1 $tgt2 > $localtmp
	fi
	check_ok
	ino_tgt1_test=
	bn_tgt1_test=
	ino_tgt2_parent_test=
	bn_tgt2_test=
	read ino_tgt1_test bn_tgt1_test \
	     ino_tgt2_parent_test bn_tgt2_test < $localtmp
	check_ok
	echo "check_symlink[$testno]:"\
	    "ino_tgt1=$ino_tgt1"\
	    "ino_tgt1_test=$ino_tgt1_test"\
	    "bn_tgt1=$bn_tgt1"\
	    "bn_tgt1_test=$bn_tgt1_test"\
	    "ino_tgt2_parent=$ino_tgt2_parent"\
	    "ino_tgt2_parent_test=$ino_tgt2_parent_test"\
	    "bn_tgt2=$bn_tgt2"\
	    "bn_tgt2_test=$bn_tgt2_test"
	if [ "$ino_tgt1" = "" ]; then
		error ino_tgt1 is empty
	fi
	if [ "$ino_tgt1_test" = "" ]; then
		error ino_tgt1_test is empty
	fi
	if [ "$ino_tgt1_test" != "$ino_tgt1" ]; then
		error ino_tgt1_test is different to ino_tgt1
	fi
	if [ "$bn_tgt1_test" = "" ]; then
		error bn_tgt1_test is empty
	fi
	if [ ! $open_last -a "$bn_tgt1_test" != "$bn_tgt1" ]; then
		error bn_tgt1_test is different to bn_tgt1
	fi
	if [ "$ino_tgt2_parent" = "" ]; then
		error ino_tgt2_parent is empty
	fi
	if [ "$ino_tgt2_parent_test" = "" ]; then
		error ino_tgt2_parent_test is empty
	fi
	if [ "$bn_tgt2_test" = "" ]; then
		error bn_tgt2_test is empty
	fi
	if [ "$ino_tgt2_parent_test" != "$ino_tgt2_parent" ]; then
		error ino_tgt2_parent_test is different to ino_tgt2_parent
	fi
	if [ "$bn_tgt2_test" != "$bn_tgt2" ]; then
		error bn_tgt2_test is different to bn_tgt2
	fi
}

check_symlink_root() {
	testno=$1
	tgt1=$2
	tgt2=$3
	echo "check_symlink_root[$testno]: tgt1=$tgt1 tgt2=$tgt2"
	$name2_op $tgt1 $tgt2
	if [ "$?" != "2" ]; then
		error invalid exit code $?
	fi
}

check_symlink_root_ol() {
	testno=$1
	tgt1=$2
	tgt2=$3
	echo "check_symlink_root_ol[$testno]: tgt1=$tgt1 tgt2=$tgt2"
	$name2_op_ol $tgt1 $tgt2
	if [ "$?" != "2" ]; then
		error invalid exit code $?
	fi
}

check_symlink_loop() {
	testno=$1
	tgt1=$2
	tgt2=$3
	echo "check_symlink_loop[$testno]: tgt1=$tgt1 tgt2=$tgt2"
	ino_tgt=`$name2_op $tgt1 $tgt2`
	if [ "$?" != "5" ]; then
		error invalid exit code : $?
	fi
}

check_symlink_not_exist() {
	testno=$1
	tgt1=$2
	tgt2=$3
	echo "check_symlink_not_exist[$testno]: tgt1=$tgt1 tgt2=$tgt2"
	$name2_op $tgt1 $tgt2
	if [ "$?" != "3" ]; then
		echo $name2_op invalid exit code $?
	fi
}

clean_test() {
	rm -f $localtmp 2>/dev/null
	clean_file_structure
}

trap 'clean_test; exit $exit_trap' $trap_sigs

# set conditions
setup_file_structure

# test symlink of directory/file
check_symlink  1 false $A_d          $AA_d
check_symlink  2 false $AA_d         $A_d
check_symlink  3 false $C_d          $A_d
check_symlink  4 false $A_d          $C_d
check_symlink  5 false $C_d          $AA_d
check_symlink  6 false $AA_d         $C_d
check_symlink  7 false $a1_f         $a2_f
check_symlink  8 false $a2_f         $a1_f
check_symlink  9 false $a2_f         $a3_f
check_symlink 10 false $a3_f         $a2_f
check_symlink 11 false $a3_f         $a4_f
check_symlink 12 false $a4_f         $a3_f
check_symlink 13 false $a1_f         $a4_f
check_symlink 14 false $a4_f         $a1_f
check_symlink 15 false $toA_l        $AA_d
check_symlink 16 false $AA_d         $toA_l
check_symlink 17 false $toA_l        $toAA_l
check_symlink 18 false $toAA_l       $toA_l
check_symlink 19 false $toC_l        $A_d
check_symlink 20 false $A_d          $toC_l
check_symlink 21 false $toA_l        $toC_l
check_symlink 22 false $toC_l        $toA_l
check_symlink 23 false $AA_d         $toC_l
check_symlink 24 false $toC_l        $AA_d
check_symlink 25 false $toAA_l       $toC_l
check_symlink 26 false $toC_l        $toAA_l
check_symlink 27 false $toRoot_l     $AA_d
check_symlink 28 false $AA_d         $toRoot_l
check_symlink 27 false $toRoot_l$A_d $AA_d
check_symlink 27 false $toRoot_l     $toRoot_l
check_symlink 28 false $AA_d         $toRoot_l$A_d
check_symlink 29 false $toRoot_l$A_d $toRoot_l$A_d
check_symlink 30 false $a1_f         $toa2_l
check_symlink 31 false $toa2_l       $a1_f
check_symlink 32 false $toa1_l       $toa2_l
check_symlink 33 false $a2_f         $toa3_l
check_symlink 34 false $toa3_l       $a2_f
check_symlink 35 false $toa2_l       $toa3_l
check_symlink 36 false $toa3_l       $toa2_l
check_symlink 37 false $a3_f         $toa4_l
check_symlink 38 false $toa4_l       $a3_f
check_symlink 39 false $toa3_l       $toa4_l
check_symlink 40 false $toa4_l       $toa3_l
check_symlink 41 false $a1_f         $toa4_l
check_symlink 42 false $toa4_l       $a1_f
check_symlink 43 false $toa4_l       $toa1_l
check_symlink 44 false $toa1_l       $toa4_l
check_symlink 51 true  $A_d          $C_d
check_symlink 52 true  $C_d          $A_d
check_symlink 53 true  $toRoot_l     $toRoot_l
check_symlink 54 true  $toRoot_l$A_d $AA_d
check_symlink 55 true  $a1_f         $a2_f
check_symlink 56 true  $a2_f         $a1_f
check_symlink 57 true  $a3_f         $a2_f
check_symlink 58 true  $a4_f         $a3_f
# test special path
check_symlink_root      1 /           $A_d
check_symlink_root      2 $A_d        /
check_symlink_root      3 /           /
check_symlink_root_ol   1 $A_d        /
check_symlink_not_exist 1 $NotExist_f $a1_f
check_symlink_loop      1 $toLL_l/x   $A_d
check_symlink_loop      2 $A_d        $toLL_l/x
# exit
clean_test
exit $exit_pass

