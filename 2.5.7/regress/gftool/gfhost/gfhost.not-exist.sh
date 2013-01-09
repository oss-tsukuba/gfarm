#!/bin/sh

. ./regress.conf

if gfhost does-not-exist; then
	:
else
	exit_code=$exit_pass
fi

exit $exit_code
