#!/bin/sh

. ./regress.conf

$regress/bin/am_I_gfarm_super_adm
if test $? -ne 0; then
    exit $exit_unsupported
fi

dir=$gftmp

pat00="g0:1, g1:1, g0:2, g2:1, g0:3, g3:1"

pat="g0:1, g1:1, g0:2, g2:1, g0:3"
g0s=g0:6
g1s=g1:1
g2s=g2:1

setup() {
    gfmkdir ${dir}
    gfncopy -r ${dir}

    for g in g0 g1 g2 g3
    do
	gfhost -c -a dummy-arch -p 12345 dummy-$g
	gfhostgroup -s dummy-$g $g
    done
}

cleanup() {
    gfrmdir ${dir}

    for g in g0 g1 g2 g3
    do
	gfhost -d dummy-$g
    done
}

trap 'cleanup; exit $exit_trap' $trap_sigs

setup

gfncopy -M -S "${pat00}" ${dir}
if [ $? -ne 1 ]; then
    cleanup
    exit $exit_fail
fi

gfncopy -C -S "${pat00}" ${dir}
if [ $? -ne 0 ]; then
    cleanup
    exit $exit_fail
fi

gfncopy -M -S "${pat}" ${dir}
if [ $? -ne 0 ]; then
    cleanup
    exit $exit_fail
fi

pat1=`gfncopy ${dir} | sed -e 's:,: :g'`
if [ $? -ne 0 -o "X${pat1}" = "X" ]; then
    cleanup
    exit $exit_fail
fi

gotcha=0
for i in ${pat1}; do
    if [ \
	"X${i}" = "X${g0s}" -o \
	"X${i}" = "X${g1s}" -o \
	"X${i}" = "X${g2s}" ]; then
	gotcha=`expr ${gotcha} + 1`
    fi
done

if [ ${gotcha} -ne 3 ]; then
    cleanup
    exit $exit_fail
fi

gfncopy -r ${dir}
st=$?

cleanup

if [ ${st} -eq 0 ]; then
    exit $exit_pass
else
    exit $exit_fail
fi

