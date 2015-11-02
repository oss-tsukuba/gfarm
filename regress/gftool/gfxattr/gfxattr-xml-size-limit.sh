#!/bin/sh

. ./regress.conf

xmlattr_size_limit=786432

if $regress/bin/is_xmlattr_supported; then
	:
else
	exit $exit_unsupported
fi

sh -x $testbase/gfxattr-size-limit.sh $xmlattr_size_limit -x
