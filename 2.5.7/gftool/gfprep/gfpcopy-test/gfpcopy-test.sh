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

function sig_handler() {
    local dir
    remove_local_files > /dev/null 2>&1
    remove_gfarm_files > /dev/null 2>&1
    dir=${LOCAL_DIR}${DIR_PATTERN2}
    rm -rf ${dir} > /dev/null 2>&1
    rm ${TMP_FILE}
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
    num=`echo "scale=2; $1 / 1000000" | bc`
    printf "%12.02f MB/s (%.02f sec)\n" ${num} $2
    echo $1" "$2 >> ${TMP_FILE}
}

function create_local_files() {
    local i tmp_dir src
    tmp_dir=${LOCAL_DIR}${DIR_PATTERN}
    mkdir -p $tmp_dir
    for i in `seq $1` ; do
	src=${tmp_dir}"/test."${i}.dat
	dd if=/dev/zero of=${src} \
           bs=1024 count=$(($2 / 1024)) > /dev/null 2>&1
	if [[ $? -ne 0 ]]; then
	    echo "cannot create a test file! ("${src}")"
	    return 1
	fi
    done
}

function remove_local_files() {
    local tmp_dir
    tmp_dir=${LOCAL_DIR}${DIR_PATTERN}
    rm -rf $tmp_dir
}

function create_gfarm_files() {
    local src_dir dst_dir
    src_dir="file://"${LOCAL_DIR}${DIR_PATTERN}
    dst_dir=${GFARM_DIR}

    gfpcopy ${src_dir} ${dst_dir}
}

function remove_gfarm_files() {
    local tmp_dir
    tmp_dir=${GFARM_DIR}${DIR_PATTERN}
    gfrm -rf $tmp_dir
}

function do_gfpcopy_togfarm() {
    local src_dir dst_dir tmp_str val time
    src_dir="file://"${LOCAL_DIR}${DIR_PATTERN}
    dst_dir=${GFARM_DIR}${DIR_PATTERN}

    print_parallels $1

    tmp_str=`gfpcopy -p -j $1 ${src_dir} ${dst_dir}`
    val=`echo ${tmp_str}  | cut -d ' ' -f 8`
    time=`echo ${tmp_str}  | cut -d ' ' -f 11`
    print_result $val $time
    gfrm -rf $dst_dir
}

function do_gfpcopy_fromgfarm() {
    local src_dir dst_dir tmp_str val time
    src_dir=${GFARM_DIR}${DIR_PATTERN}
    dst_dir=${LOCAL_DIR}${DIR_PATTERN2}

    print_parallels $1

    tmp_str=`gfpcopy -p -j $1 ${src_dir} "file://"${dst_dir}`
    val=`echo ${tmp_str}  | cut -d ' ' -f 8`
    time=`echo ${tmp_str}  | cut -d ' ' -f 11`
    print_result $val $time
    rm -rf ${dst_dir}
}

function do_gfprep() {
    local tmp_dir tmp_str val time
    tmp_dir=${GFARM_DIR}${DIR_PATTERN}

    [ `gfsched -w | wc -l` -ge  ${NUM_REPLICA} ] || return 1

    print_parallels $1

    tmp_str=`gfprep -p -N ${NUM_REPLICA} -j $1 ${tmp_dir}`
    val=`echo ${tmp_str}  | cut -d ' ' -f 8`
    time=`echo ${tmp_str}  | cut -d ' ' -f 11`
    print_result $val $time
}

function usage() {
    echo "usage : $0 -l <local directory> -g <gfarm directory>"
    exit 1
}

# main

OPT=`getopt "hl:g:" $@` || usage $@
set -- $OPT

while [[ -- != "$1" ]]; do
    case $1 in
	-h)
	    usage
	    ;;
	-l)
	    LOCAL_DIR=$2; shift
	    ;;
	-g)
	    GFARM_DIR=$2; shift
	    ;;
    esac
    shift 
done

# default value
: ${LOCAL_DIR:=$PWD}
: ${GFARM_DIR:=/tmp}

if [[ ! -d $LOCAL_DIR ]]; then
    echo ${LOCAL_DIR} is not a directory.
    exit 1
fi

tmp=`gfstat ${GFARM_DIR} 2> /dev/null | grep "Filetype: directory"`

if [[ X${tmp} == X ]]; then
    echo ${GFARM_DIR} is not a directory in the Gfarm file system.
    exit 1
fi

if [[ ! ${GFARM_DIR} =~ "^gfarm:*" ]]; then
    case X"${GFARM_DIR}" in
	X/*)
	    GFARM_DIR="gfarm://"${GFARM_DIR} ;;
	*)
	    GFARM_DIR="gfarm:///"${GFARM_DIR} ;;
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
rm ${TMP_FILE}
fi
