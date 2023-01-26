#!/bin/sh

. ./regress.conf

if which flake8; then
    :
else
    exit $exit_unsupported
fi

gfptar_path=`which gfptar`

if flake8 $gfptar_path; then
    exit_code=$exit_pass
fi

exit $exit_code
