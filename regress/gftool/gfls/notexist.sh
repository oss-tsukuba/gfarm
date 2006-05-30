#!/bin/sh

. regress.conf

gfls /notexist 2>$tmp1

if [ $? = 1 ] && cmp -s $tmp1 $scriptbase/notexist.out; then
	status=0
fi
