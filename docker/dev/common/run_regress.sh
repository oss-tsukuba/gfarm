#!/bin/sh

GFARM_SRCDIR=$1
LOGFILE=$2
export GFARM_TEST_CKSUM_MISMATCH=$3
parallel=$4

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

for i in $(seq 1 $parallel); do
    make check &
done
wait
result=$?
cp -p log $LOGFILE
exit $result
