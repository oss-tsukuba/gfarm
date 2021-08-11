#!/bin/sh

set -eu

. ./regress.conf

GFPREP_DIR=`dirname $0`
. ${GFPREP_DIR}/setup_gfprep.sh

get_type() {
  NAME="$1"
  echo `expr "$NAME" : "[l|g]-\(file\|symlink\|dir\)"`
}

get_isgfarm() {
  NAME="$1"
  if [ `expr "$NAME" : "\([l|g]\)-.*"` = g ]; then
    echo true
  else
    echo false
  fi
}

get_workdir() {
  TARGET="$1"
  IS_GFARM="$2"
  NAME="$3"

  DIR=
  if [ $TARGET = SRC ]; then
    if [ $IS_GFARM = true ]; then
      DIR=$gf_dir1
    else
      DIR=$local_dir1
    fi
  else
    if [ $IS_GFARM = true ]; then
      DIR=$gf_dir2
    else
      DIR=$local_dir2
    fi
  fi
  echo $DIR
}

create_dir() {
  IS_GFARM="$1"
  NAME="$2"

  MKDIR=
  if [ $IS_GFARM = true ]; then
    MKDIR=gfmkdir
  else
    MKDIR=mkdir
  fi

  if $MKDIR -p $NAME; then
    :
  else
      echo "$MKDIR failed: $NAME"
      clean_test
      exit $exit_fail
  fi
}

remove_dir() {
  IS_GFARM="$1"
  NAME="$2"

  RM=
  if [ $IS_GFARM = true ]; then
    RM=gfrm
  else
    RM=rm
  fi

  if $RM -rf $NAME; then
    :
  else
      echo "$RM -rf failed: $NAME"
      clean_test
      exit $exit_fail
  fi
}


create_file() {
  IS_GFARM="$1"
  NAME="$2"

  if [ $IS_GFARM = true ]; then
    gfreg $data/1byte $NAME
  else
    echo test > $NAME
  fi
  if [ $? -ne 0 ]; then
      echo "create_file (IS_GFARM=$IS_GFARM) failed: $NAME"
      clean_test
      exit $exit_fail
  fi
}

create_symlink() {
  IS_GFARM="$1"
  NAME="$2"

  if [ $IS_GFARM = true ]; then
    gfln -s ABCDEFG $NAME
  else
    ln -s ABCDEFG $NAME
  fi
  if [ $? -ne 0 ]; then
    echo "create_symlink (IS_GFARM=$IS_GFARM) failed: $NAME"
    clean_test
    exit $exit_fail
  fi
}

create_entry() {
  IS_GFARM="$1"
  TYPE="$2"
  NAME="$3"

  if [ $TYPE = file ]; then
    create_file "$IS_GFARM" "$NAME"
  elif [ $TYPE = symlink ]; then
    create_symlink "$IS_GFARM" "$NAME"
  elif [ $TYPE = dir ]; then
    create_dir "$IS_GFARM" "$NAME"
  fi
}

expect_type() {
  IS_GFARM="$1"
  TYPE="$2"
  NAME="$3"

  OPT=
  if [ $TYPE = file ]; then
    OPT=-s
  elif [ $TYPE = symlink ]; then
    OPT=-h
  elif [ $TYPE = dir ]; then
    OPT=-d
  fi
  TEST_CMD=
  if [ $IS_GFARM = true ]; then
    TEST_CMD=gftest
  else
    TEST_CMD=test
  fi
  if $TEST_CMD $OPT $NAME; then
    :
  else
    echo "unexpected type (IS_GFARM=$IS_GFARM): $NAME"
    if [ $IS_GFARM = true ]; then
      gfstat $NAME
    else
      stat $NAME
    fi
    clean_test
    exit $exit_fail
  fi
}

get_scheme() {
  IS_GFARM="$1"

  if [ $IS_GFARM = true ]; then
    echo "gfarm:"
  else
    echo "file:"
  fi
}

test_overwrite() {
  SRC="$1"
  DST="$2"

  SRC_ISGFARM=`get_isgfarm "$SRC"`
  DST_ISGFARM=`get_isgfarm "$DST"`
  SRC_TYPE=`get_type "$SRC"`
  DST_TYPE=`get_type "$DST"`
  SRCDIR=`get_workdir SRC "$SRC_ISGFARM" "$SRC"`
  DSTDIR_PARENT=`get_workdir DST "$DST_ISGFARM" "$DST"`
  DSTDIR=${DSTDIR_PARENT}/`basename "$SRCDIR"`

  create_dir "$SRC_ISGFARM" "$SRCDIR"
  create_dir "$DST_ISGFARM" "$DSTDIR"

  TESTFILENAME="__${SRC}__${DST}__"
  create_entry "$SRC_ISGFARM" "$SRC_TYPE" "${SRCDIR}/${TESTFILENAME}"
  create_entry "$DST_ISGFARM" "$DST_TYPE" "${DSTDIR}/${TESTFILENAME}"

  SRC_PREFIX=`get_scheme "$SRC_ISGFARM"`
  DST_PREFIX=`get_scheme "$DST_ISGFARM"`
  if $GFPCOPY "${SRC_PREFIX}${SRCDIR}" "${DST_PREFIX}${DSTDIR_PARENT}"; then
    :
  else
    echo "gfpcopy failed"
    clean_test
    exit $exit_fail
  fi

  expect_type "$DST_ISGFARM" "$SRC_TYPE" "${DSTDIR}/${TESTFILENAME}"

  remove_dir "$SRC_ISGFARM" "$SRCDIR"
  remove_dir "$DST_ISGFARM" "$DSTDIR"
}

test_overwrite l-file g-file
test_overwrite g-file l-file
test_overwrite l-file g-dir
test_overwrite g-file l-dir
test_overwrite l-file g-symlink
test_overwrite g-file l-symlink

test_overwrite l-dir g-file
test_overwrite g-dir l-file
test_overwrite l-dir g-dir
test_overwrite g-dir l-dir
test_overwrite l-dir g-symlink
test_overwrite g-dir l-symlink

test_overwrite l-symlink g-file
test_overwrite g-symlink l-file
test_overwrite l-symlink g-dir
test_overwrite g-symlink l-dir
test_overwrite l-symlink g-symlink
test_overwrite g-symlink l-symlink

clean_test
exit $exit_pass
