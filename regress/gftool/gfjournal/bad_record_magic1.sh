#! /bin/sh

. ./regress.conf
exec $testbase/test_gfjournal.sh $testbase/bad_record_magic1.gmj
