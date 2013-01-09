#!/bin/sh

. ./regress.conf

if $testbin/test_gfxattr -s -n gfarm.ncopy /.not/.exist /.not/.exist </dev/null
then
  :
else
  exit_code=$exit_pass
fi
exit $exit_code
