PROG=./gfs_pio_failover_test
TMPF=/tmp/gfs_pio_failover_test.file
GF_TMPF=$TMPF
GF_TMPF_SLNK=/tmp/gfs_pio_failover_test.slnk
GF_TMPD=/tmp/gfs_pio_failover_test.dir

get_gfsd() {
	_tmpf=/tmp/gfs_pio_failover_test.tmp
	gfhost -M > $_tmpf
	while true; do
		read _DMY _DMY GFSD0 GFSD0_PORT _DMY
		read _DMY _DMY GFSD1 GFSD1_PORT _DMY
		break
	done < $_tmpf
	rm $_tmpf
	if [ "$GFSD0" = "" ]; then
		echo "SCRIPT ERROR: failed to get gfsd."
		exit 1
	fi

	./wait_for_gfsd.sh $GFSD0
	./wait_for_gfsd.sh $GFSD1
}

