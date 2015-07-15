#! /bin/sh

. ./regress.conf
exec $testbase/test_gfjournal.sh $testbase/no_record.gmj
