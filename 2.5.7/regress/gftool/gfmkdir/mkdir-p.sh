#!/bin/sh

. ./regress.conf

trap 'gfrm -rf $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp; then
	gfreg $data/0byte $gftmp/f
	gfln -s does-not-exist $gftmp/s
	gfmkdir $gftmp/d
	if gfmkdir -p $gftmp/d/a/b &&
	   [ x"`gfls -d $gftmp/d/a/b`" = x"$gftmp/d/a/b" ] &&
	   gfmkdir -p $gftmp/n/a/b &&
	   [ x"`gfls -d $gftmp/n/a/b`" = x"$gftmp/n/a/b" ]
	then
		if gfmkdir -p $gftmp/f/a/b; then
			:
		elif gfmkdir -p $gftmp/s/a/b; then
			:
		else
			exit_code=$exit_pass
		fi
	fi
fi

gfrm -rf $gftmp
exit $exit_code
