#!/bin/sh

. ./regress.conf

if $regress/bin/am_I_gfarmroot; then
  exit $exit_unsupported
fi

$testbase/file_trunc_not_writable.sh -r
