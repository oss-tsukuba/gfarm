#!/bin/sh

. ./regress.conf

tmp1=$localtop/gfu1.$$
tmp2=$localtop/gfu2.$$

trap 'gfrm $gftmp; rm -f $tmp1 $tmp2; exit $exit_trap' $trap_sigs

# XXX there is possible race condition here, if another process concurrently
# creates or delete a file during this test.

if	gfusage >$tmp1 &&
	gfreg $data/0byte $data/1byte $gftmp &&
   	gfusage >$tmp2 &&
	awk '
	$1 == "'"`gfwhoami`"'" && FILENAME == "'"$tmp1"'" {
		tmp1ok = 1; sz1 = $2; nf1= $3; ns1 = $4
	}
	$1 == "'"`gfwhoami`"'" && FILENAME == "'"$tmp2"'" {
		tmp2ok = 1; sz2 = $2; nf2= $3; ns2 = $4
	}
	END {
		if (tmp1ok && tmp2ok && sz2 - sz1 == 1 && nf2 - nf1 == 1 && ns2 - ns1 == 2)
			exit(0)
		exit(1)
	}
	' $tmp1 $tmp2
then
	exit_code=$exit_pass
fi

if [ x"$REGRESS_AGENT" = x"only" ]; then
	case $exit_code in 
	$exit_pass)	exit_code=$exit_xpass;;
	$exit_fail)	exit_code=$exit_unsupported;;
	esac
fi

gfrm $gftmp
rm -f $tmp1 $tmp2
exit $exit_code
