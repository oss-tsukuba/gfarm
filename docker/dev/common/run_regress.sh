#!/bin/sh

GFARM_SRCDIR=$1
LOGFILE=$2
run_as_gfarmroot=$3
export GFARM_TEST_CKSUM_MISMATCH=$4
parallel=$5

cd $GFARM_SRCDIR/regress
if ! [ -e bin/gfmd_restart_all ]; then
    ln -s gfmd_restart_all.gfservice.sh bin/gfmd_restart_all
fi

export LANG=C

### XXX TODO
# export GFARM_TEST_MDS2=gfmd1:601/tmp/docker-dev-mds2
# export GFARM_TEST_MDS3=gfmd1:601/tmp/docker-dev-mds3
# export GFARM_TEST_MDS4=gfmd1:601/tmp/docker-dev-mds4
# gfmkdir -p gfarm://$GFARM_TEST_MDS2
# gfmkdir -p gfarm://$GFARM_TEST_MDS3
# gfmkdir -p gfarm://$GFARM_TEST_MDS4

if bin/am_I_gfarmroot; then
    was_gfarmroot=true
else
    was_gfarmroot=false
fi
need_to_fix_gfarmroot=false
me=`gfwhoami`

trap_sigs='0 1 2 15'
restore() {
    if $need_to_fix_gfarmroot; then
        if $was_gfarmroot; then
            gfgroup -a -m gfarmroot $me
        else
            gfgroup -r -m gfarmroot $me
        fi
    fi
}
trap 'restore' $trap_sigs

if [ $run_as_gfarmroot != $was_gfarmroot ]; then
    need_to_fix_gfarmroot=true
    if $run_as_gfarmroot; then
        gfgroup -a -m gfarmroot $me
    else
        gfgroup -r -m gfarmroot $me
    fi
fi

for i in $(seq 1 $parallel); do
    make check &
done
wait
result=$?
cp -p log $LOGFILE
exit $result
