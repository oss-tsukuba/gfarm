#! /bin/sh

. ./regress.conf
exec $testbase/test_gfjournal.sh $testbase/one_transaction.gmj
