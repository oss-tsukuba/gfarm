#! /bin/sh

# e.g.
mdhosts="$(gfmdhost)"
IFS='
'
for h in $mdhosts; do
  gfservice stop-gfmd "$h" || exit 1
done
for h in $mdhosts; do
  gfservice start-gfmd "$h" || exit 1
done
exit 0
