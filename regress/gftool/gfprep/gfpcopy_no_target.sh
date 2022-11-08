#!/bin/sh

. ./regress.conf

GFPREP_DIR=`dirname $0`
. ${GFPREP_DIR}/setup_gfprep.sh

setup_test

if mkdir $local_dir1 &&
   gfmkdir $gf_dir2; then
  :
else
    echo mkdir failed: $local_dir1
    clean_test
    exit $exit_fail
fi

DST_HOST="unknown.example.com"

CONFIG_FILE=$local_dir1/gfarm2rc

cat <<EOF > "$CONFIG_FILE" || exit $exit_fail
write_target_domain ${DST_HOST}
#log_level debug
EOF
if [ -n "$GFARM_CONFIG_FILE" ]; then
    conf_file="$GFARM_CONFIG_FILE"
else
    conf_file=$HOME/.gfarm2rc
fi
if [ -r "$conf_file" ]; then
    echo "include $conf_file" >> "$CONFIG_FILE" || exit $exit_fail
fi

test_copy() {
  SIZE=$1
  filename=COPYFILE
  OPT="-b 65536 -f -d"
  lfile=$local_dir1/$filename
  gfile1=$gf_dir1/$filename
  gfile2=$gf_dir2/$filename
  if dd if=/dev/urandom of=$lfile bs=$SIZE count=1 > /dev/null; then
    :
  else
    echo dd failed
    clean_test
    exit $exit_fail
  fi

  # copyable even if there is no available node in write_target_domain.

  ### use -D option
  if $GFPCOPY $OPT -D ${DST_HOST} file:$lfile gfarm:$gf_dir1; then
    :
  else
    echo gfpcopy failed [local to gfarm, -D]
    clean_test
    exit $exit_fail
  fi
  if $GFPCOPY $OPT -D ${DST_HOST} gfarm:$gfile1 gfarm:$gf_dir2; then
    :
  else
    echo gfpcopy failed [gfarm to gfarm, -D]
    clean_test
    exit $exit_fail
  fi

  gfrm $gfile1
  gfrm $gfile2

  ### use write_target_domain in gfarm configuration
  if GFARM_CONFIG_FILE="$CONFIG_FILE" \
     $GFPCOPY $OPT file:$lfile gfarm:$gf_dir1; then
    :
  else
    echo gfpcopy failed [local to gfarm, write_target_domain]
    clean_test
    exit $exit_fail
  fi
  if GFARM_CONFIG_FILE="$CONFIG_FILE" \
     $GFPCOPY $OPT gfarm:$gfile1 gfarm:$gf_dir2; then
    :
  else
    echo gfpcopy failed [gfarm to gfarm, write_target_domain]
    clean_test
    exit $exit_fail
  fi
}

test_copy 1

clean_test
exit $exit_pass
