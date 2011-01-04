#!/bin/sh

. ./regress.conf
. ${regress}/lib/libgfarm/gfarm/gfm_inode_or_name_op_test/\
gfm_inode_or_name_op_test.conf

# environment:
#     GFARM_TEST_MDS2=<host>:<port>
#     GFARM_TEST_MDS3=<host>:<port>
#     GFARM_TEST_MDS4=<host>:<port>

check_env_mds4

check_symlink() {
	testno=$1
	tgt=$2
	lnk=$3
	echo "check_symlink[$testno]: tgt=$tgt lnk=$lnk"
	ino_tgt=`$inode_op_nf $tgt`
	check_ok
	ino_lnk=`$inode_op_nf $lnk`
	check_ok
	ino_lnk_follow=`$inode_op $lnk`
	check_ok
	echo "check_symlink[$testno]: ino_tgt=$ino_tgt ino_lnk=$ino_lnk"
	if [ "$ino_tgt" = "" ]; then
		error ino_tgt is empty
	fi
	if [ "$ino_lnk" = "" ]; then
		error ino_lnk is empty
	fi
	if [ "$ino_lnk_follow" = "" ]; then
		error ino_lnk_follow is empty
	fi
	if [ "$ino_tgt" != "$ino_lnk_follow" ]; then
		error ino_tgt is different to ino_lnk_follow
	fi
}

clean_test() {
	#clean_file_structure_mds4
	echo > /dev/null
}

trap 'clean_test; exit $exit_trap' $trap_sigs

# setup conditions
setup_file_structure_mds4

# test symlink of directory
check_symlink 1 $mds4$A_d  $toM2_l$toM3_l$toM4_l$A_d
# exit
clean_test
exit $exit_pass

