#!/bin/sh

. ./regress.conf

allout=$(gfdf -a | tail -1)
read allblocks allused allavail allrest <<EOF
$allout
EOF

out=$(gfdf | tail -1)
read blocks used avail rest <<EOF
$out
EOF

allout2=$(gfdf -a | tail -1)
read allblocks2 allused2 allavail2 allrest2 <<EOF
$allout2
EOF

# this test may fail due to the timing issue
exit_code=$exit_xfail

if [ $allblocks = $blocks ] &&
   [ $allused = $used ] && [ $allavail = $avail ]; then
	exit_code=$exit_pass
elif [ $allblocks2 = $blocks ] &&
   [ $allused2 = $used ] && [ $allavail2 = $avail ]; then
	exit_code=$exit_pass
else
	echo gfdf -a: $allout
	echo gfdf -R: $out
	echo gfdf -a: $allout2
fi

exit $exit_code
