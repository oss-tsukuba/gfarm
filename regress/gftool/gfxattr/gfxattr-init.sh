. gftool/gfxattr/gfxattr.conf
xattrtmp=$data/xattrtest
attrfile=$xattrtmp/xattr
attrfile2=$xattrtmp/xattr2
getfile=$xattrtmp/xattr.get
nameslist=$xattrtmp/xattr.names
getnames=$xattrtmp/xattr.names.get
statfile=$xattrtmp/xattr.stat
getstat=$xattrtmp/xattr.stat.get

subdir="dir1"
subdir2="dir2"
subsubdir="$subdir/dir11"
fusemnt="/tmp/xattrroot"
fileX="file1"
attrname="user.attr"
attrname2="user.attr2"

wait_for_command_output() {
	path=$1
	expect=$2
	shift 2
	[ "${1:-}" = "--" ] || return 2
	shift
	timeout=$WAIT_TIMEOUT
	interval=$WAIT_INTERVAL
	count=0
	while [ $count -lt $timeout ]; do
		"$@" > "$path"
		if cmp -s "$expect" "$path"; then
			return 0
		fi
		sleep "$interval"
		count=$((count + 1))
	done
	"$@" > "$path"
	cmp -s "$expect" "$path"
}

wait_for_gfstat_change() {
	path=$1
	expect=$2
	timeout=${3:-$WAIT_TIMEOUT}
	interval=${4:-$WAIT_INTERVAL}
	count=0
	while [ $count -lt $timeout ]; do
		gfstat / > "$path"
		if ! cmp -s "$expect" "$path"; then
			return 0
		fi
		sleep "$interval"
		count=$((count + 1))
	done
	gfstat / > "$path"
	! cmp -s "$expect" "$path"
}

wait_for_gfstat_same() {
	path=$1
	expect=$2
	timeout=${3:-$WAIT_TIMEOUT}
	interval=${4:-$WAIT_INTERVAL}
	count=0
	while [ $count -lt $timeout ]; do
		gfstat / > "$path"
		if cmp -s "$expect" "$path"; then
			return 0
		fi
		sleep "$interval"
		count=$((count + 1))
	done
	gfstat / > "$path"
	cmp -s "$expect" "$path"
}
