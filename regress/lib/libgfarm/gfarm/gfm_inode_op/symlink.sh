#!/bin/sh

. ./regress.conf
. ${regress}/lib/libgfarm/gfarm/gfm_inode_or_name_op_test/\
gfm_inode_or_name_op_test.conf

check_symlink() {
	testno=$1
	tgt=$2
	lnk=$3
	echo "check_symlink[$testno]: tgt=$tgt lnk=$lnk"
	ino_tgt=`$inode_op_nf $tgt`
	check_ok
	ino_lnk=`$inode_op_nf $lnk`
	check_ok
	ino_lnk_flw=`$inode_op $lnk`
	check_ok
	echo "check_symlink[$testno]:"\
	    "ino_tgt=$ino_tgt"\
	    "ino_lnk=$ino_lnk"
	if [ "$ino_tgt" = "" ]; then
		error ino_tgt is empty
	fi
	if [ "$ino_lnk" = "" ]; then
		error ino_lnk is empty
	fi
	if [ "$ino_lnk_flw" = "" ]; then
		error ino_lnk_flw is empty
	fi
	if [ "$ino_tgt" = "$ino_lnk" ]; then
		error ino_tgt is equal to ino_lnk
	fi
	if [ "$ino_tgt" != "$ino_lnk_flw" ]; then
		error ino_tgt is different to ino_lnk_flw
	fi
}

check_mid_symlink() {
	testno=$1
	tgt=$2
	echo "check_mid_symlink[$testno]: tgt=$tgt"
	ino_tgt=`$inode_op_nf $tgt`
	ino_tgt_flw=`$inode_op $tgt`
	echo "check_mid_symlink[$testno]:"\
	    "ino_tgt=$ino_tgt"\
	    "ino_tgt_flw=$ino_tgt_flw"
	if [ "$ino_tgt" = "" ]; then
		error ino_tgt is empty
	fi
	if [ "$ino_tgt_flw" = "" ]; then
		error ino_tgt_flw is empty
	fi
	if [ "$ino_tgt" != "$ino_tgt_flw" ]; then
		error ino_tgt is different to ino_tgt_flw
	fi
}

check_symlink_not_exist() {
	testno=$1
	tgt=$2
	lnk=$3
	echo "check_symlink_not_exist[$testno]: tgt=$tgt lnk=$lnk"
	ino_lnk=`$inode_op_nf $lnk`
	check_ok
	ino_lnk_flw=`$inode_op $lnk`
	check_fail
	ino_tgt=`$inode_op_nf $tgt`
	check_fail
	echo "check_symlink_not_exist[$testno]: ino_lnk=$ino_lnk"
	if [ "$ino_lnk" = "" ]; then
		error ino_lnk is empty
	fi
}

check_symlink_loop() {
	testno=$1
	tgt=$2
	echo "check_symlink_loop[$testno]: tgt=$tgt"
	ino_tgt=`$inode_op $tgt`
	if [ "$?" != "5" ]; then
	    	error invalid exit code : $?
	fi
	ino_tgt=`$inode_op_nf $tgt`
	check_ok
}

clean_test() {
	rm -f $localtmp 2>/dev/null
	clean_file_structure
}

#trap 'clean_test; exit $exit_trap' $trap_sigs

# setup conditions
setup_file_structure

# test symlink of directory
check_symlink 1 $A_d  $toA_l
check_symlink 2 $AA_d $toAA_l
check_symlink 3 $C_d  $toC_l
# test symlink of file
check_symlink 4 $a1_f $toa1_l
check_symlink 5 $a2_f $toa2_l
check_symlink 6 $a3_f $toa3_l
check_symlink 7 $a4_f $toa4_l
# test symlink of relative directory
check_symlink 8 $BB_d $toBB_l
# test symlink of relative file
check_symlink 9 $a5_f $toBB_l/toa5
# test file/dir of which middle component is symlink
check_mid_symlink 1 $toA_l/a1
check_mid_symlink 2 $toA_l/AA
check_mid_symlink 3 $toA_l/AA/a3
check_mid_symlink 4 $toAA_l/a3
check_mid_symlink 5 $toRoot_l$A_d
# test special path
check_symlink           8 / $toRoot_l
check_symlink_not_exist 1 $NotExist_f $toNotExist_l
check_symlink_loop      1 $toLL_l
# exit
clean_test
exit $exit_pass

