#!/bin/sh

. ./regress.conf

if $regress/bin/am_I_gfarmadm; then
   :
else
    exit $exit_unsupported
fi
if gfmdhost 2>&1 | grep 'operation not permitted' >/dev/null; then
   # metadata replication is not enabled
   exit $exit_unsupported
fi

tmphost=TMP-`hostname`-$$

trap 'gfmdhost -d $tmphost 2>/dev/null; exit $exit_trap' $trap_sigs
trap 'gfmdhost -d $tmphost 2>/dev/null; exit $exit_code' 0

if gfmdhost -c -t s -C FOR-REGRESS -p 60000 $tmphost >/dev/null &&
   gfmdhost -l |
	grep "^- slave  async s FOR-REGRESS  $tmphost 60000"'$' >/dev/null &&
   gfmdhost -m -t c -C XXX-REGRESS -p 50000 $tmphost >/dev/null &&
   gfmdhost -l |
	grep "^- slave  async c XXX-REGRESS  $tmphost 50000"'$' >/dev/null &&
   gfmdhost -d $tmphost
then
   if gfmdhost | grep "^$tmphost"'$' >/dev/null; then
      :
   else
      exit_code=$exit_pass
   fi
fi
