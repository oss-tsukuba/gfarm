#!/bin/sh

. ./regress.conf

while read input expected
do
	path=`expr "$input" : 'x\(.*\)'`
	out=`gfpath -D "$path"`
	if [ x"$out" != x"$expected" ]; then
		echo >&2 "`basename $0`: url_dir '$path': '$expected' is expected, but '$out' is returned"
		exit $exit_fail
	fi
done <<'EOF'
x/				/
x/a				/
x/a/				/
x/a/b				/a
x//				/
x//a				/
x//a//				/
x//a//b				//a
x				.
xa				.
xa/				.
xa/b				a
xa//				.
xa//b				a
EOF

exit $exit_pass
