#!/bin/sh

. ./regress.conf

if $testbin/gfxattr -l /.not/.exist /.not/.exist
then
  :
else
  exit_code=$exit_pass
fi
exit $exit_code
