#!/bin/sh

. ./regress.conf
. ${regress}/lib/libgfarm/gfarm/gfm_inode_or_name_op_test/\
gfm_inode_or_name_op_test.conf

# environment:
#     GFARM_TEST_MDS2=<host>:<port>

if [ "$GFARM_TEST_MDS2" = "" ]; then
	echo GFARM_TEST_MDS2 is not set
	exit $exit_unsupported
fi

check_symlink() {
	testno=$1
	is_same_mds=$2
	tgt=$3
	lnk=$4
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
	if $is_same_mds && [ "$ino_tgt" = "$ino_lnk" ]; then
		error ino_tgt is equal to ino_lnk
	fi
	if [ "$ino_tgt" != "$ino_lnk_follow" ]; then
		error ino_tgt is different to ino_lnk_follow
	fi
}

clean_test() {
	rm -f $localtmp 2>/dev/null
	clean_file_structure_mds2
}

trap 'clean_test; exit $exit_trap' $trap_sigs

# setup conditions
setup_file_structure_mds2

# test symlink of directory
check_symlink 1 false $mds2$U_d  $mds1$toU_l
# test symlink of file
check_symlink 2 false $mds2$u1_f $mds1$tou1_l
# test more complicated symlink of file
check_symlink 3 true  $mds2$u1_f $mds1$toV_l/toA/tou1
# test symlink which is not url
check_symlink 4 true  $mds2$toR2_l/U/u1 $mds1$toV_l/toA/tou1
# exit
clean_test
exit $exit_pass

