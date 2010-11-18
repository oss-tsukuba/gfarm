#!/bin/sh

. ./regress.conf

while read input expected
do
	path=`expr "$input" : 'x\(.*\)'`
	out=`gfpath -d "$path"`
	if [ x"$out" != x"$expected" ]; then
		echo >&2 "`basename $0`: url_dir '$path': '$expected' is expected, but '$out' is returned"
		exit $exit_fail
	fi
done <<'EOF'
xgfarm://hoge:9999		gfarm://hoge:9999
xgfarm://hoge:9999/		gfarm://hoge:9999
xgfarm://hoge:9999/a		gfarm://hoge:9999
xgfarm://hoge:9999/a/		gfarm://hoge:9999
xgfarm://hoge:9999/a/b		gfarm://hoge:9999/a
xgfarm://hoge:9999/a//		gfarm://hoge:9999
xgfarm://hoge:9999/a//b		gfarm://hoge:9999/a
xgfarm://hoge:9999//		gfarm://hoge:9999
xgfarm://hoge:9999//a		gfarm://hoge:9999
xgfarm://hoge:9999//a/		gfarm://hoge:9999
xgfarm://hoge:9999//a/b		gfarm://hoge:9999//a
xgfarm://hoge:9999//a//		gfarm://hoge:9999
xgfarm://hoge:9999//a//b	gfarm://hoge:9999//a
xgfarm://hoge			gfarm://hoge
xgfarm://hoge/			gfarm://hoge
xgfarm://hoge/a			gfarm://hoge
xgfarm://hoge/a/		gfarm://hoge
xgfarm://hoge/a/b		gfarm://hoge/a
xgfarm://hoge/a//		gfarm://hoge
xgfarm://hoge/a//b		gfarm://hoge/a
xgfarm://hoge//			gfarm://hoge
xgfarm://hoge//a		gfarm://hoge
xgfarm://hoge//a/		gfarm://hoge
xgfarm://hoge//a/b		gfarm://hoge//a
xgfarm://hoge//a//		gfarm://hoge
xgfarm://hoge//a//b		gfarm://hoge//a
xgfarm://:9999			gfarm://:9999
xgfarm://:9999/			gfarm://:9999
xgfarm://:9999/a		gfarm://:9999
xgfarm://:9999/a/		gfarm://:9999
xgfarm://:9999/a/b		gfarm://:9999/a
xgfarm://:9999/a//		gfarm://:9999
xgfarm://:9999/a//b		gfarm://:9999/a
xgfarm://:9999//		gfarm://:9999
xgfarm://:9999//a		gfarm://:9999
xgfarm://:9999//a/		gfarm://:9999
xgfarm://:9999//a/b		gfarm://:9999//a
xgfarm://:9999//a//		gfarm://:9999
xgfarm://:9999//a//b		gfarm://:9999//a
xgfarm://			gfarm://
xgfarm:///			gfarm://
xgfarm:///a			gfarm://
xgfarm:///a/			gfarm://
xgfarm:///a/b			gfarm:///a
xgfarm:///a//			gfarm://
xgfarm:///a//b			gfarm:///a
xgfarm:////			gfarm://
xgfarm:/			gfarm:/
xgfarm:/a			gfarm:/
xgfarm:/a/			gfarm:/
xgfarm:/a/b			gfarm:/a
xgfarm:/a//			gfarm:/
xgfarm:/a//b			gfarm:/a
xgfarm:				gfarm:
xgfarm:hoge			gfarm:
xgfarm:hoge/			gfarm:
xgfarm:hoge/a			gfarm:hoge
xgfarm:hoge/a/			gfarm:hoge
xgfarm:hoge/a/b			gfarm:hoge/a
xgfarm:hoge/a//			gfarm:hoge
xgfarm:hoge/a//b		gfarm:hoge/a
xgfarm:hoge//			gfarm:
xgfarm:hoge//a			gfarm:hoge
xgfarm:hoge//a/			gfarm:hoge
xgfarm:hoge//a/b		gfarm:hoge//a
xgfarm:hoge//a//		gfarm:hoge
xgfarm:hoge//a//b		gfarm:hoge//a
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
