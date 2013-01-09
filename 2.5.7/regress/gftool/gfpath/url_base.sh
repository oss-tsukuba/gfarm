#!/bin/sh

. ./regress.conf

while read input expected
do
	path=`expr "$input" : 'x\(.*\)'`
	base=`expr "$expected" : 'x\(.*\)'`
	out=`gfpath -b "$path"`
	if [ x"$out" != x"$base" ]; then
		echo >&2 "`basename $0`: url_base '$path': '$base' is expected, but '$out' is returned"
		exit $exit_fail
	fi
done <<'EOF'
xgfarm://hoge:9999		x
xgfarm://hoge:9999/		x
xgfarm://hoge:9999/a		xa
xgfarm://hoge:9999/a/		x
xgfarm://hoge:9999/a/b		xb
xgfarm://hoge:9999/a//		x
xgfarm://hoge:9999/a//b		xb
xgfarm://hoge:9999//		x
xgfarm://hoge:9999//a		xa
xgfarm://hoge:9999//a/		x
xgfarm://hoge:9999//a/b		xb
xgfarm://hoge:9999//a//		x
xgfarm://hoge:9999//a//b	xb
xgfarm://hoge			x
xgfarm://hoge/			x
xgfarm://hoge/a			xa
xgfarm://hoge/a/		x
xgfarm://hoge/a/b		xb
xgfarm://hoge/a//		x
xgfarm://hoge/a//b		xb
xgfarm://hoge//			x
xgfarm://hoge//a		xa
xgfarm://hoge//a/		x
xgfarm://hoge//a/b		xb
xgfarm://hoge//a//		x
xgfarm://hoge//a//b		xb
xgfarm://:9999			x
xgfarm://:9999/			x
xgfarm://:9999/a		xa
xgfarm://:9999/a/		x
xgfarm://:9999/a/b		xb
xgfarm://:9999/a//		x
xgfarm://:9999/a//b		xb
xgfarm://:9999//		x
xgfarm://:9999//a		xa
xgfarm://:9999//a/		x
xgfarm://:9999//a/b		xb
xgfarm://:9999//a//		x
xgfarm://:9999//a//b		xb
xgfarm://			x
xgfarm:///			x
xgfarm:///a			xa
xgfarm:///a/			x
xgfarm:///a/b			xb
xgfarm:///a//			x
xgfarm:///a//b			xb
xgfarm:////			x
xgfarm:/			x
xgfarm:/a			xa
xgfarm:/a/			x
xgfarm:/a/b			xb
xgfarm:/a//			x
xgfarm:/a//b			xb
xgfarm:				x
xgfarm:hoge			xhoge
xgfarm:hoge/			x
xgfarm:hoge/a			xa
xgfarm:hoge/a/			x
xgfarm:hoge/a/b			xb
xgfarm:hoge/a//			x
xgfarm:hoge/a//b		xb
xgfarm:hoge//			x
xgfarm:hoge//a			xa
xgfarm:hoge//a/			x
xgfarm:hoge//a/b		xb
xgfarm:hoge//a//		x
xgfarm:hoge//a//b		xb
x/				x
x/a				xa
x/a/				x
x/a/b				xb
x//				x
x//a				xa
x//a//				x
x//a//b				xb
x				x
xa				xa
xa/				x
xa/b				xb
xa//				x
xa//b				xb
EOF

exit $exit_pass
