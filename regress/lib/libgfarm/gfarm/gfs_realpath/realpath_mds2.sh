#!/bin/sh

. ./regress.conf
. ${regress}/lib/libgfarm/gfarm/gfm_inode_or_name_op_test/\
gfm_inode_or_name_op_test.conf

# environment:
#     GFARM_TEST_MDS2=<host>:<port>

check_env_mds2

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

clean_test() {
	rm -f $localtmp 2>/dev/null
	clean_file_structure_mds2
}

trap 'clean_test; exit $exit_trap' $trap_sigs

# setup conditions
setup_file_structure_mds2

# check realpath
check_realpath  1 $mds2$R_d/U         $R_d/A/toU
check_realpath  2 $mds2$R_d/U         $mds1$R_d/A/toU
check_realpath  3 $mds2$R_d/U/u1      $R_d/A/tou1
check_realpath  4 $mds1$R_d/A         $R_d/toV/toA
check_realpath  5 $mds1$R_d/A         $R_d/A/toU/toR2/V/toA
check_realpath  6 $mds2$R_d/U/u1      $R_d/A/toU/../U/toR2/V/toA/tou1

# exit
clean_test
exit $exit_pass

