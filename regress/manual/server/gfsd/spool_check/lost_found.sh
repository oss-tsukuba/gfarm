#!/bin/sh

BASE_DIR=`dirname $0`
TEST_CONFIG=$BASE_DIR/config.sh
TEST_LOST_FOUND=$BASE_DIR/newfile_on_spool.pl
LOSTF_DIR=/lost+found

if [ ! -r ./regress.conf ]; then
    echo >&2 "change the Gfarm_source/regress directory"
    exit 6
fi
. ./regress.conf

if [ ! -r $TEST_CONFIG ]; then
    echo >&2 "need $TEST_CONFIG"
    exit $exit_unsupported
fi
. $TEST_CONFIG

if [ -z "$GFSD_HOST_NAME" ]; then
    echo >&2 "need GFSD_HOST_NAME in $TEST_CONFIG"
    exit $exit_unsupported
fi
if [ -z "$GFSD_START_SCRIPT" -o ! -f $GFSD_START_SCRIPT ]; then
    echo >&2 "need GFSD_START_SCRIPT in $TEST_CONFIG"
    exit $exit_unsupported
fi
if [ -z "$GFSD_SPOOL_DIR" -o ! -d $GFSD_SPOOL_DIR ]; then
    echo >&2 "need GFSD_SPOOL_DIR in $TEST_CONFIG"
    exit $exit_unsupported
fi
if [ -z "$GFSD_CONFIG_FILE" -o ! -f $GFSD_CONFIG_FILE ]; then
    echo >&2 "need GFSD_CONFIG_FILE in $TEST_CONFIG"
    exit $exit_unsupported
fi
if [ -z "$USE_SUDO" ]; then
    echo >&2 "need USE_SUDO in $TEST_CONFIG"
    exit $exit_unsupported
fi
if [ $USE_SUDO != "true" -a $USE_SUDO != "false" ]; then
    echo >&2 "need USE_SUDO in $TEST_CONFIG"
    exit $exit_unsupported
fi
SUDO=
SUDO_GFARMFS=
if $USE_SUDO; then
    SUDO=sudo
    SUDO_GFARMFS="sudo -u _gfarmfs"
fi

egrep '^spool_check_level\s+' $GFSD_CONFIG_FILE
  if [ $? -eq 0 ]; then
    egrep '^spool_check_level\s+lost_found' $GFSD_CONFIG_FILE
    if [ $? -ne 0 ]; then
      echo >&2 "need 'spool_check_level lost_found' in $GFSD_CONFIG_FILE"
      exit $exit_unsupported
  fi
fi

GUSER=`gfwhoami`
if [ $? -ne 0 ]; then
    echo >&2 "gfwhoami failed"
    exit $exit_unsupported
fi

gfgroup -l gfarmroot | egrep " ${GUSER} | ${GUSER}\$" > /dev/null
if [ $? -ne 0 ]; then
    echo >&2 "you must be a member of gfarmroot"
    exit $exit_unsupported
fi

if gfmkdir $gftmp &&
    gfncopy -s 1 $gftmp; then  ### disable automatic replication
    :
else
    gfrmdir $gftmp > /dev/null 2>&1
    exit $exit_fail
fi

TEST_2_GFILE=${gftmp}/test2
TEST_3_GFILE=${gftmp}/test3
TEST_4_GFILE=${gftmp}/test4

clean_all() {
    rm -f $localtmp
    gfrm -f $TEST_2_GFILE
    gfrm -f $TEST_3_GFILE
    gfrm -f $TEST_4_GFILE
    gfrmdir $gftmp
}

trap 'clean_all; exit $exit_trap' $trap_sigs

### TEST_1: register unreferred file
TEST_1_INUMGEN=`$SUDO_GFARMFS $TEST_LOST_FOUND $GFSD_SPOOL_DIR`
if [ $? -ne 0 ]; then
    echo >&2 "cannot create unreferred file"
    clean_all
    exit $exit_fail
fi

### prepare files for TEST 2-4
dd if=/dev/zero of=$localtmp bs=1024 count=1
if [ $? -ne 0 ]; then
    echo >&2 "cannot create localtmp file"
    clean_all
    exit $exit_fail
fi

create_gfile() {
    TEST_NUM=$1
    TEST_GFILE=$2

    gfreg -h $GFSD_HOST_NAME $localtmp $TEST_GFILE > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo >&2 "TEST_${TEST_NUM}: gfreg failed"
        clean_all
        exit $exit_fail
    fi
    gfspoolpath $TEST_GFILE
    if [ $? -ne 0 ]; then
        echo >&2 "TEST_${TEST_NUM}: gfspoolpath failed"
        clean_all
        exit $exit_fail
    fi
}

### TEST_2: different size
TEST_2_SPOOL=`create_gfile 2 $TEST_2_GFILE`
TEST_2_INUMGEN=`echo $TEST_2_SPOOL | tr -d '[/data]'`
$SUDO_GFARMFS sh -c "echo >> ${GFSD_SPOOL_DIR}/${TEST_2_SPOOL}"
if [ $? -ne 0 ]; then
    echo >&2 "TEST_2: cannot append data to file"
    clean_all
    exit $exit_fail
fi

### TEST_3: size 0
create_gfile 3 $TEST_3_GFILE
TEST_3_SPOOL=`create_gfile 3 $TEST_3_GFILE`
TEST_3_INUMGEN=`echo $TEST_3_SPOOL | tr -d '[/data]'`
$SUDO_GFARMFS sh -c "printf '' > ${GFSD_SPOOL_DIR}/${TEST_3_SPOOL}"
if [ $? -ne 0 ]; then
    echo >&2 "TEST_3: cannot truncate file"
    clean_all
    exit $exit_fail
fi

### TEST_4: unlink file
TEST_4_SPOOL=`create_gfile 4 $TEST_4_GFILE`
TEST_4_INUMGEN=`echo $TEST_4_SPOOL | tr -d '[/data]'`
$SUDO_GFARMFS rm -f ${GFSD_SPOOL_DIR}/${TEST_4_SPOOL}
if [ $? -ne 0 ]; then
    echo >&2 "TEST_4: cannot unlink file"
    clean_all
    exit $exit_fail
fi

### gfsd restart
$SUDO $GFSD_START_SCRIPT restart
while :; do
    gfsched | grep $GFSD_HOST_NAME
    if [ $? -eq 0 ]; then
        break;
    fi
    echo >&2 "wait gfsd startup"
    sleep 1
done

### check
exit_code=$exit_pass

check_lost_found() {
    TEST_NUM=$1
    TEST_INUMGEN=$2
    EXIST=$3

    LOSTF_FILE="${LOSTF_DIR}/${TEST_INUMGEN}-${GFSD_HOST_NAME}"
    gfstat $LOSTF_FILE > /dev/null
    if [ $? -ne 0 ]; then
        if [ $EXIST -eq 1 ]; then
            echo >&2 "TEST_${TEST_NUM} NG: file was not moved in lost+found"
            clean_all
            exit_code=$exit_fail
        else
            echo "TEST_${TEST_NUM} OK: file was moved in lost+found"
        fi
    else
        if [ $EXIST -eq 0 ]; then
            echo >&2 "TEST_${TEST_NUM} NG: file exists in lost+found"
            clean_all
            exit_code=$exit_fail
        else
            echo "TEST_${TEST_NUM} OK: file does not exist in lost+found"
        fi
        gfrm -f $LOSTF_FILE
    fi
}

check_removed_metadata() {
    TEST_NUM=$1
    TEST_GFILE=$2

    gfwhere $TEST_GFILE
    if [ $? -eq 0 ]; then
        echo >&2 "TEST_${TEST_NUM} NG: metadata is not removed"
        clean_all
        exit_code=$exit_fail
    else
        echo "TEST_${TEST_NUM} OK: metadata is removed"
    fi
}

check_lost_found 1 $TEST_1_INUMGEN 1
check_lost_found 2 $TEST_2_INUMGEN 1
check_lost_found 3 $TEST_3_INUMGEN 0
check_lost_found 4 $TEST_4_INUMGEN 0

check_removed_metadata 3 $TEST_3_GFILE
check_removed_metadata 4 $TEST_4_GFILE

clean_all
[ $exit_code -eq $exit_pass ] && echo ALL success
exit $exit_code
