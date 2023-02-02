#!/bin/sh

. ./regress.conf

trap 'gfrm -rf $gftmp; rm -rf $localtmp; exit $exit_trap' $trap_sigs

gfmkdir $gftmp
mkdir $localtmp

# avoid "UnicodeEncodeError: 'ascii' codec can't encode character"
# on Python 3.6.x
LANG=en_US.UTF-8
export LANG

if gfptar -q --test \
	  --test-workdir-local=$localtmp \
	  --test-workdir-gfarm=gfarm:$gftmp; then
    exit_code=$exit_pass
fi

gfrm -rf $gftmp
rm -rf $localtmp
exit $exit_code
