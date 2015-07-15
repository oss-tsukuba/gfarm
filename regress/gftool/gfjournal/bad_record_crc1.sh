#! /bin/sh

. ./regress.conf
exec $testbase/test_gfjournal.sh $testbase/bad_record_crc1.gmj
