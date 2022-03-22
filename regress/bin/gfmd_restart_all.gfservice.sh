#! /bin/sh

# e.g.
mdhosts="$(egrep gfmd[0-9]*\= ~/.gfservice | cut -d = -f 1)"
IFS='
'
for h in $mdhosts; do
  gfservice stop-gfmd "$h" || exit 1
done
for h in $mdhosts; do
  gfservice start-gfmd "$h" || exit 1
done
exit 0
