#!/bin/sh

. ./regress.conf

allout=$(gfdf -a | tail -1)
read allblocks allused allavail allrest <<< $allout

out=$(gfdf -R | tail -1)
read blocks used avail rest <<< $out

if [ $allblocks = $blocks ] &&
   [ $allused = $used ] && [ $allavail = $avail ]; then
	exit_code=$exit_pass
else
	echo gfdf -a: $allout
	echo gfdf -R: $out
fi

exit $exit_code
