#!/usr/bin/env bash

# Test parameters (modify if necessary)
#PARALLELS="1 4 8 16 32 64"
PARALLELS="1 4 8 16 32"
# FILE_SIZE and NUM_FILES must be in pairs.
FILE_SIZE=(1024 104857600)
NUM_FILES=(1024 32)
# Definitions for
NUM_REPLICA=2

# Definitions (These shuold not be modified)
DIR_PATTERN="/gfarm-parallel-copy-test.${HOSTNAME}.$$"
DIR_PATTERN2="/gfarm-parallel-copy-test.2.${HOSTNAME}.$$"

function print_parallels() {
    if [[ $1 -eq 1 ]]; then
	printf "   %3d parallel  " $1
    else
	printf "   %3d parallels " $1
    fi
}

function print_result() {
    printf "%12.0f Bytes/sec\n" $1
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
	    echo "can not make a file! ("${src}")"
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
    local src_dir dst_dir tmp_str
    src_dir="file://"${LOCAL_DIR}${DIR_PATTERN}
    dst_dir=${GFARM_DIR}${DIR_PATTERN}

    print_parallels $1

    tmp_str=`gfpcopy -p -j $1 ${src_dir} ${dst_dir} | grep "total_throughput:" | cut -d ' ' -f 2`
    print_result $tmp_str
    gfrm -rf $dst_dir
}

function do_gfpcopy_fromgfarm() {
    local src_dir dst_dir tmp_str
    src_dir=${GFARM_DIR}${DIR_PATTERN}

    dst_dir=${LOCAL_DIR}${DIR_PATTERN2}

    print_parallels $1

    tmp_str=`gfpcopy -p -j $1 ${src_dir} "file://"${dst_dir} | grep "total_throughput:" | cut -d ' ' -f 2`
    print_result $tmp_str
    rm -rf ${dst_dir}
}

function do_gfprep() {
    local tmp_dir tmp_str
    tmp_dir=${GFARM_DIR}${DIR_PATTERN}

    [ `gfsched -w | wc -l` -ge  ${NUM_REPLICA} ] || return 1

    print_parallels $1

    tmp_str=`gfprep -p -N ${NUM_REPLICA} -j $1 ${tmp_dir} | grep "total_throughput:" | cut -d ' ' -f 2`
    print_result $tmp_str
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

if [[ X${LOCAL_DIR} == X ]]; then
    usage
fi

if [[ X${GFARM_DIR} == X ]]; then
    usage
fi

if [[ ! -d $LOCAL_DIR ]]; then
    echo ${LOCAL_DIR} is not a directory.
    exit 1
fi

tmp=`gfstat ${GFARM_DIR} 2> /dev/null | grep "Filetype: directory"`

if [[ X${tmp} == X ]]; then
    echo ${GFARM_DIR} is not a directory.
    exit 1
fi

if [[ ! ${GFARM_DIR} =~ "^gfarm:///*" ]]; then
    GFARM_DIR="gfarm://"${GFARM_DIR}
fi

if [[ ${#FILE_SIZE[@]} -le ${#NUM_FILES[@]} ]]; then
    len=${#FILE_SIZE[@]}
else
    len=${#NUM_FILES[@]}
fi

for (( i = 0; i<$len; i++ )) do
    size=${FILE_SIZE[${i}]}
    num=${NUM_FILES[${i}]}

    echo "File size: "${size}"   Number of files: "${num}
    create_local_files $num $size
    echo -e "  Copy Files to gfarm"
    for par in $PARALLELS; do do_gfpcopy_togfarm ${par}; done
    create_gfarm_files $num $size
    echo -e "  Copy Files from gfarm"
    for par in $PARALLELS; do do_gfpcopy_fromgfarm ${par}; done
    remove_gfarm_files
    echo -e "  Replicate Files on gfarm"
    for par in $PARALLELS; do
	create_gfarm_files $num $size
	do_gfprep ${par}
	remove_gfarm_files
    done
    remove_local_files
done
