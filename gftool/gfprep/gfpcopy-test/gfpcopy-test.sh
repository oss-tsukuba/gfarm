#!/usr/bin/env bash
# $Id$

# Test parameters (modify if necessary)
#PARALLELS="1 4 8 16 32 64"
PARALLELS="1 4 8 16 32"
# FILE_SIZE and NUM_FILES must be in pairs.
FILE_SIZE=(1024 104857600)
NUM_FILES=(1024 32)
# Number of Replicas
NUM_REPLICA=2

# Definitions (These should not be modified)
DIR_PATTERN="/gfpcopy-test.${HOSTNAME}.$$"
DIR_PATTERN2="/gfpcopy-test.2.${HOSTNAME}.$$"
TMP_FILE="/tmp/gfpcopy-test.sh.${HOSTNAME}.$$"

OPT_DH=
OPT_DH_VAL=
OPT_U=

function test_clean() {
    local dir
    remove_local_files > /dev/null 2>&1
    remove_gfarm_files > /dev/null 2>&1
    dir="${LOCAL_DIR}${DIR_PATTERN2}"
    rm -rf "${dir}" > /dev/null 2>&1
    rm -f ${TMP_FILE}
}

function sig_handler() {
    test_clean
    exit 0
}

function print_parallels() {
    if [[ $1 -eq 1 ]]; then
	printf "   %3d parallel  " $1
    else
	printf "   %3d parallels " $1
    fi
    echo -n ${PHASE}" "$1" " >> ${TMP_FILE}
}

function print_result() {
    local num
    num=`echo "scale=2; $1" | bc`
    printf "%12.02f MB/s (%.02f sec)\n" ${num} $2
    echo $1" "$2 >> ${TMP_FILE}
}

function create_local_files() {
    local i tmp_dir src
    tmp_dir="${LOCAL_DIR}${DIR_PATTERN}"
    mkdir -p "$tmp_dir"
    for i in `seq $1` ; do
	src="${tmp_dir}/test.${i}.dat"
	dd if=/dev/zero of="${src}" \
           bs=1024 count=$(($2 / 1024)) > /dev/null 2>&1
	if [[ $? -ne 0 ]]; then
	    echo "cannot create a test file! ("${src}")"
	    return 1
	fi
    done
}

function remove_local_files() {
    local tmp_dir
    tmp_dir="${LOCAL_DIR}${DIR_PATTERN}"
    rm -rf "$tmp_dir"
}

function exec_gfpcopy() {
    local j p src dst
    use_opt=$1
    j=$2
    p=$3
    src=$4
    dst=$5
    if [ $use_opt -eq 1 -a -n "$OPT_DH" ]; then
        gfpcopy $OPT_DH "$OPT_DH_VAL" $OPT_U -j $j $p "$src" "$dst"
    else
        gfpcopy $OPT_U -j $j $p "$src" "$dst"
    fi
    if [ $? -ne 0 ]; then
        echo gfpcopy failed 1>&2
        test_clean
        exit 1
    fi
}

function create_gfarm_files() {
    local src_dir dst_dir
    src_dir="file://${LOCAL_DIR}${DIR_PATTERN}"
    dst_dir="${GFARM_DIR}"

    exec_gfpcopy 1 4 "" "${src_dir}" "${dst_dir}"
}

function remove_gfarm_files() {
    local tmp_dir
    tmp_dir="${GFARM_DIR}${DIR_PATTERN}"
    gfrm -rf "$tmp_dir"
}

function do_gfpcopy_togfarm() {
    local src_dir dst_dir tmp_str val time
    src_dir="file://${LOCAL_DIR}${DIR_PATTERN}"
    dst_dir="${GFARM_DIR}${DIR_PATTERN}"

    print_parallels $1

    tmp_str=`exec_gfpcopy 1 $1 -p "${src_dir}" "${dst_dir}"`
    val=`echo ${tmp_str}  | cut -d ' ' -f 8`
    time=`echo ${tmp_str}  | cut -d ' ' -f 11`
    print_result $val $time
    gfrm -rf "$dst_dir"
}

function do_gfpcopy_fromgfarm() {
    local src_dir dst_dir tmp_str val time
    src_dir="${GFARM_DIR}${DIR_PATTERN}"
    dst_dir="${LOCAL_DIR}${DIR_PATTERN2}"

    print_parallels $1

    tmp_str=`exec_gfpcopy 0 $1 -p "${src_dir}" "file://${dst_dir}"`
    val=`echo ${tmp_str}  | cut -d ' ' -f 8`
    time=`echo ${tmp_str}  | cut -d ' ' -f 11`
    print_result $val $time
    rm -rf "${dst_dir}"
}

function do_gfprep() {
    local tmp_dir tmp_str val time
    tmp_dir="${GFARM_DIR}${DIR_PATTERN}"

    [ `gfsched -w | wc -l` -ge  ${NUM_REPLICA} ] || return 1

    print_parallels $1

    tmp_str=`gfprep -p -N ${NUM_REPLICA} -j $1 "${tmp_dir}"`
    val=`echo ${tmp_str}  | cut -d ' ' -f 8`
    time=`echo ${tmp_str}  | cut -d ' ' -f 11`
    print_result $val $time
}

# default value
: ${LOCAL_DIR:=$PWD}
: ${GFARM_DIR:=/tmp}

DEFAULT_LOCAL_DIR="$LOCAL_DIR"
DEFAULT_GFARM_DIR="$GFARM_DIR"

function usage() {
    echo "usage: $0 [options]"
    echo "options:"
    echo "  -l <local dir> (default: $DEFAULT_LOCAL_DIR)"
    echo "  -g <gfarm dir> (default: $DEFAULT_GFARM_DIR)"
    echo "  -D <domain>    create files on the domain"
    echo "  -H <hostlist>  create files on the hosts in hostlist"
    echo "  -U             disable checking disk_avail before copying"
    exit 1
}

function conflict_check() {
    if [ -n "$OPT_DH" ]; then
        echo -${1} option conflicts with $OPT_DH 1>&2
        usage
    fi
}

# main
while getopts hl:g:D:H:U opt; do
    case $opt in
        l)
            LOCAL_DIR="$OPTARG"
            ;;
        g)
            GFARM_DIR="$OPTARG"
            ;;
        D)
            conflict_check $opt
            OPT_DH=-D;
            OPT_DH_VAL="$OPTARG"
            ;;
        H)
            conflict_check $opt
            OPT_DH=-H;
            OPT_DH_VAL="$OPTARG"
            if [ ! -f "$OPT_DH_VAL" ]; then
                echo ${OPT_DH_VAL}: not a file
                exit 1
            fi
            ;;
        U)
            OPT_U=-U
            ;;
        h | ?)
            usage
            ;;
    esac
done


if [[ ! -d "$LOCAL_DIR" ]]; then
    echo "${LOCAL_DIR}" is not a directory.
    exit 1
fi

st=`gfstat "${GFARM_DIR}" 2>&1`
if [[ $? -ne 0 ]]; then
    echo $st 1>&2
    exit 1
fi
tmp=`echo -n $st | grep "Filetype: directory"`
if [[ X"${tmp}" == X ]]; then
    echo "${GFARM_DIR} is not a directory in the Gfarm file system."
    exit 1
fi

if [[ ! ${GFARM_DIR} =~ ^gfarm:* ]]; then
    case X"${GFARM_DIR}" in
	X/*)
	    GFARM_DIR="gfarm://${GFARM_DIR}" ;;
	*)
	    GFARM_DIR="gfarm:///${GFARM_DIR}" ;;
    esac
fi

trap sig_handler 1 2 15

if [[ ${#FILE_SIZE[@]} -le ${#NUM_FILES[@]} ]]; then
    len=${#FILE_SIZE[@]}
else
    len=${#NUM_FILES[@]}
fi

PHASE=0
for (( i = 0; i<$len; i++ )) do
    size=${FILE_SIZE[${i}]}
    num=${NUM_FILES[${i}]}

    echo "File size: "${size}"   Number of files: "${num}
    create_local_files $num $size
    echo -e "  Copy test from local file system ($LOCAL_DIR) to Gfarm ($GFARM_DIR)"
    for par in $PARALLELS; do do_gfpcopy_togfarm ${par}; done
    create_gfarm_files $num $size
    let $((PHASE++))
    echo -e "  Copy test from Gfarm ($GFARM_DIR) to local file system ($LOCAL_DIR)"
    for par in $PARALLELS; do do_gfpcopy_fromgfarm ${par}; done
    remove_gfarm_files
    let $((PHASE++))
#    echo -e "  Replicate Files on gfarm"
#    for par in $PARALLELS; do
#	create_gfarm_files $num $size
#	do_gfprep ${par}
#	remove_gfarm_files
#    done
    remove_local_files
done

if [ -f ${TMP_FILE} ]; then
awk -f -  <<"__END__" ${TMP_FILE}
{
	if ($4!="" && (res[$1]=="" || res[$1] > $4)) {
		res[$1]=$4
		para[$1]=$2
	}
}
END {
	sum=0
	count=0
	for ( i in para ) {
		count++
		sum = sum + para[i]
	}
	avr = sum / count
	print "preferred \"client_parallel_copy\" in gfarm2.conf is "int(avr+0.5)
}
__END__
rm -f ${TMP_FILE}
fi
