#!/bin/sh

. ./regress.conf
. ${regress}/lib/libgfarm/gfarm/gfm_inode_or_name_op_test/\
gfm_inode_or_name_op_test.conf

# environment:
#     GFARM_TEST_MDS2=<host>:<port>

check_env_mds2

check_symlink() {
	testno=$1
	is_same_mds=$2
	tgt=$3
	lnk=$4
	bn_tgt=`basename $tgt`
	bn_lnk=`basename $lnk`
	echo "check_symlink[$testno]: tgt=$tgt"

	ino_tgt=`$inode_op $tgt`
	check_ok
	ino_lnk=`$inode_op_nf $lnk`
	check_ok
	ino_lnk_follow=`$inode_op $lnk`
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
	    "ino_tgt=$ino_tgt"
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
	if [ "$ino_lnk_follow" = "" ]; then
		error ino_lnk_follow is empty
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
	if [ "$ino_tgt" != "$ino_lnk_follow" ]; then
		error ino_tgt is different to ino_lnk_follow
	fi
	if $is_same_mds && [ "$ino_lnk" = "$ino_tgt" ]; then
		error ino_lnk is equal to ino_tgt
	fi
	if [ "$bn_tgt" != "$bn_tgt_test" ]; then
		error bn_tgt is different to bn_tgt_test
	fi
	if [ "$bn_lnk" != "$bn_lnk_test" ]; then
		error bn_lnk is different to bn_lnk_test
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
# exit
clean_test
exit $exit_pass

