#!/bin/sh

gfarm2fs_hook=$1
gfarm_src_path="/home/${GFDOCKER_PRIMARY_USER}/gfarm"
HOST_SHARE_DIR=/mnt
gfarm2fs_hook="${HOST_SHARE_DIR}/hook/gfarm2fs.hook"
mount_point="${HOME}/mnt"
install_path="${mount_point}/install"
exit_status=1

case $# in
2)
  tree_parallel=$1
  make_parallel=$2
  ;;
*)
  echo >&2 "Usage: `basename $0` <num_of_src_trees> <num_of_parallel_make>"
  exit 2
  ;;
esac

restore()
{
  fusermount -u "${mount_point}"
  rm -rf "${mount_point}"
  exit "${exit_status}"
}

trap 'restore' 0 1 2 15

mkdir -p "${mount_point}"

${gfarm2fs_hook} gfarm2fs "${mount_point}"

for i in $(seq 1 $tree_parallel); do
  (
     src="${mount_point}/src.${i}"
     mkdir -p "${src}"
     cd "$src"
     tar cf - -C "${gfarm_src_path}" . | tar pxf -
     ./configure --prefix="${install_path}"
     make -j"${make_parallel}"
     make install
  ) &
done 
wait

for i in $(seq 1 $tree_parallel); do
  (
     src="${mount_point}/src.${i}"
     rm -rf "${src}"
  ) &
done 
rm -rf "${install_path}" &
wait

exit_status=0
