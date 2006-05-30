#!/bin/sh

. regress.conf

if gfls -d / >$tmp1 && cmp -s $tmp1 $scriptbase/root.out; then
	status=0
fi

