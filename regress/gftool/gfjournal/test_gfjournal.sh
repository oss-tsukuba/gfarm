#!/bin/sh

. ./regress.conf

journal_file="$1"
out_file=`echo $journal_file | sed -e 's/\.gmj$/.out/'`
out_m_file=`echo $journal_file | sed -e 's/\.gmj$/-m.out/'`

trap 'rm -f $localtmp; exit $exit_trap' $trap_sigs

#
# Test 'gfjournal'.
#
nerror=0
if gfjournal $journal_file > $localtmp && \
    diff -b $localtmp $out_file > /dev/null 2>&1; then
	:
else
	nerror=`expr $nerror + 1`
fi

#
# Test 'gfjournal -m'.
#
if gfjournal -m $journal_file > $localtmp && \
   diff -b $localtmp $out_m_file > /dev/null 2>&1; then
	:
else
	nerror=`expr $nerror + 1`
fi

rm -f $localtmp
[ $nerror -eq 0 ] && exit_code=$exit_pass

exit $exit_code
