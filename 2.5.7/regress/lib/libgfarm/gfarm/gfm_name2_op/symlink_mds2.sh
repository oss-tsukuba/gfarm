#!/bin/sh

. ./regress.conf
. ${regress}/lib/libgfarm/gfarm/gfm_inode_or_name_op_test/\
gfm_inode_or_name_op_test.conf

# environment:
#     GFARM_TEST_MDS2=<host>:<port>

check_env_mds2

check_symlink() {
	testno=$1
	tgt1=$2
	tgt2=$3
	echo "check_symlink[$testno]: tgt1=$tgt1 tgt2=$tgt2"
	tgt2_parent=`dirname $tgt2`
	ino_tgt1=`$inode_op_nf $tgt1`
	check_ok
	ino_tgt2_parent=`$inode_op $tgt2_parent`
	check_ok
	$name2_op $tgt1 $tgt2 > $localtmp
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
	    "ino_tgt2_parent=$ino_tgt2_parent"\
	    "ino_tgt2_parent_test=$ino_tgt2_parent_test"
	if [ "$ino_tgt1" = "" ]; then
		error ino_tgt1 is empty
	fi
	if [ "$ino_tgt1_test" = "" ]; then
		error ino_tgt1_test is empty
	fi
	if [ "$ino_tgt2_parent" = "" ]; then
		error ino_tgt2_parent is empty
	fi
	if [ "$ino_tgt2_parent_test" = "" ]; then
		error ino_tgt2_parent_test is empty
	fi
	if [ "$ino_tgt1_test" != "$ino_tgt1" ]; then
		error ino_tgt1_test is different to ino_tgt1
	fi
	if [ "$ino_tgt2_parent_test" != "$ino_tgt2_parent" ]; then
		error ino_tgt2_parent_test is different to ino_tgt2_parent
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

clean_test() {
	rm -f $localtmp 2>/dev/null
	clean_file_structure_mds2
}

trap 'clean_test; exit $exit_trap' $trap_sigs

# setup conditions
setup_file_structure_mds2

## test symlink of directory/file
# mds1, mds1
check_symlink 1 $A_d                $toV_l
# mds1, mds1
check_symlink 2 $toV_l              $A_d
# mds1->mds2, mds2
check_symlink 3 $toU_l/u1           $mds2$v1_f
# mds1->mds2, mds1->mds2
check_symlink 4 $toU_l/u1           $toV_l/v1
# mds2->mds1->mds2, mds1->mds2
check_symlink 5 $mds2$toA_l/toU/u1  $toV_l/v1
# mds2->mds1->mds2, mds2->mds1->mds2
check_symlink 6 $mds2$toA_l/toU/u1  $mds2$toA_l/toU/u1
# mds2->mds1->mds2, mds2->mds1->mds1->mds2
check_symlink 7 $mds2$toA_l/toU/u1  $mds2$toA_l/toR1/A/toU/u1
check_symlink 8 $toR1_l/A/toR1/A/toR1/A/toR1/A/toU/u1 \
	$mds2$toR2_l/U/toR2/U/toR2/U/toR2/U
check_symlink 9 $mds2$toR2_l/U/toR2/U/toR2/U/toR2/U \
	$toR1_l/A/toR1/A/toR1/A/toR1/A/toU/u1
check_symlink_loop 1 $mds2$toLL_l/x $A_d
check_symlink_loop 1 $A_d $mds2$toLL_l/x
# exit
clean_test
exit $exit_pass

