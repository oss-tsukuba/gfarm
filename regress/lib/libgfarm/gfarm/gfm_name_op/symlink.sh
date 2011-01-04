#!/bin/sh

. ./regress.conf
. ${regress}/lib/libgfarm/gfarm/gfm_inode_or_name_op_test/\
gfm_inode_or_name_op_test.conf

name_op_parent="$testcmd -Np"

check_symlink() {
	testno=$1
	tgt=$2
	lnk=$3
	bn_tgt=`basename $tgt`
	bn_lnk=`basename $lnk`
	echo "check_symlink[$testno]: tgt=$tgt lnk=$lnk"
	ino_tgt=`$inode_op $tgt`
	check_ok
	ino_lnk=`$inode_op_nf $lnk`
	check_ok
	ino_lnk_flw=`$inode_op $lnk`
	check_ok
	$name_op $tgt > $localtmp
	check_ok
	ino_tgt_test=
	bn_tgt_test=
	read ino_tgt_test bn_tgt_test < $localtmp
	check_ok
	$name_op $lnk > $localtmp
	check_ok
	ino_lnk_test=
	bn_lnk_test=
	read ino_lnk_test bn_lnk_test < $localtmp
	check_ok
	echo "check_symlink[$testno]:"\
	    "ino_tgt=$ino_tgt"\
	    "ino_tgt_test=$ino_tgt_test"\
	    "ino_lnk=$ino_lnk"\
	    "ino_lnk_test=$ino_lnk_test"\
	    "bn_tgt_test=$bn_tgt_test"\
	    "bn_lnk_test=$bn_lnk_test"
	if [ "$ino_tgt" = "" ]; then
		error ino_tgt is empty
	fi
	if [ "$ino_lnk" = "" ]; then
		error ino_lnk is empty
	fi
	if [ "$ino_lnk_flw" = "" ]; then
		error ino_lnk_flw is empty
	fi
	if [ "$ino_tgt_test" = "" ]; then
		error ino_tgt_test is empty
	fi
	if [ "$ino_tgt" != "$ino_tgt_test" ]; then
		error ino_tgt is different to ino_tgt_test
	fi
	if [ "$ino_lnk" != "$ino_lnk_test" ]; then
		error ino_lnk is different to ino_lnk_test
	fi
	if [ "$ino_tgt" != "$ino_lnk_flw" ]; then
		error ino_tgt is different to ino_lnk_flw
	fi
	if [ "$ino_lnk" = "$ino_tgt" ]; then
		error ino_lnk is equal to ino_tgt
	fi
	if [ "$bn_tgt" != "$bn_tgt_test" ]; then
		error bn_tgt is different to bn_tgt_test
	fi
	if [ "$bn_lnk" != "$bn_lnk_test" ]; then
		error bn_lnk is different to bn_lnk_test
	fi
}

check_mid_symlink() {
	testno=$1
	tgt=$2
	echo "check_mid_symlink[$testno]: tgt=$tgt"
	ino_tgt=`$inode_op $tgt`
	check_ok
	ino_tgt_test=
	bn_tgt_test=
	$name_op $tgt > $localtmp
	check_ok
	read ino_tgt_test bn_tgt_test < $localtmp
	check_ok
	echo "check_mid_symlink[$testno]:"\
	    "ino_tgt=$ino_tgt"\
	    "ino_tgt_test=$ino_tgt_test"

	if [ "$ino_tgt" = "" ]; then
		error ino_tgt is empty
	fi
	if [ "$ino_tgt_test" = "" ]; then
		error ino_tgt_flw is empty
	fi
	if [ "$ino_tgt" != "$ino_tgt_test" ]; then
		error ino_tgt is different to ino_tgt_test
	fi
}

check_symlink_not_exist() {
	testno=$1
	tgt=$2
	lnk=$3
	echo "check_symlink_not_exist[$testno]: tgt=$tgt lnk=$lnk"
	tgt_parent=`dirname $tgt`
	ino_tgt_parent=`$inode_op $tgt_parent`
	check_ok
	ino_tgt_parent_test=
	bn_tgt_test=
	$name_op_parent $tgt > $localtmp
	check_ok
	read ino_tgt_parent_test bn_tgt_test < $localtmp
	check_ok
	echo "check_symlink_not_exist[$testno]:"\
	    "ino_tgt_parent=$ino_tgt_parent"\
	    "ino_tgt_parent_test=$ino_tgt_parent_test"

	if [ "$ino_tgt_parent" = "" ]; then
		error ino_tgt_parent is empty
	fi
	if [ "$ino_tgt_parent_test" = "" ]; then
		error ino_tgt_parent_test is empty
	fi
	if [ "$ino_tgt_parent" != "$ino_tgt_parent_test" ]; then
		error ino_tgt_parent is different to ino_tgt_parent_test
	fi
}

check_symlink_root() {
	testno=$1
	tgt=$2
	echo "check_symlink_root[$testno]: tgt=$tgt"
	dummy=`$name_op $tgt`
	if [ "$?" != 6 ]; then
		error invalid exit code : $?
	fi
}

check_symlink_loop() {
	testno=$1
	tgt=$2
	echo "check_symlink_loop[$testno]: tgt=$tgt"
	ino_tgt=`$name_op $tgt`
	check_ok
	if [ "$ino_tgt" = "" ]; then
		echo ino_tgt is empty
	fi
}

clean_test() {
	rm -f $localtmp 2>/dev/null
	clean_file_structure
}

trap 'clean_test; exit $exit_trap' $trap_sigs

# setup conditions
setup_file_structure

# test symlink of directory
check_symlink 1 $A_d   $toA_l
check_symlink 2 $AA_d  $toAA_l
check_symlink 3 $C_d   $toC_l
# test symlink of file
check_symlink 4 $a1_f  $toa1_l
check_symlink 5 $a2_f  $toa2_l
check_symlink 6 $a3_f  $toa3_l
check_symlink 7 $a4_f  $toa4_l
# test file/dir of which middle component is symlink
check_mid_symlink 1 $toA_l/a1
check_mid_symlink 2 $toA_l/AA
check_mid_symlink 3 $toA_l/AA/a3
check_mid_symlink 4 $toAA_l/a3
check_mid_symlink 5 $toRoot_l$A_d
# test special path
check_symlink_root      1 /
check_symlink_not_exist 1 $NotExist_f $toNotExist_l
check_symlink_loop      1 $toLL_l
# exit
clean_test
exit $exit_pass

