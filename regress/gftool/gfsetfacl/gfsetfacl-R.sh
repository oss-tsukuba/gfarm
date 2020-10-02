#! /bin/sh

. ./regress.conf

clean_test() {
    gfrm -f $gftmp/a/file
    gfrm -f $gftmp/a/sym
    gfrmdir $gftmp/a/b/c
    gfrmdir $gftmp/a/b
    gfrmdir $gftmp/a
    gfrmdir $gftmp/DIR
    gfrmdir $gftmp
}

trap 'clean_test; exit $exit_trap' $trap_sigs

### setup
if gfmkdir -p $gftmp/a/b/c &&
    gfmkdir -p $gftmp/DIR &&
    gfln -s $gftmp/DIR $gftmp/a/sym &&
    gfreg $data/1byte $gftmp/a/file; then
    :
else
    exit $exit_fail
fi

error() {
    msg=$1
    echo "error: $msg"
    clean_test
    exit $exit_code
}

### #1: gfsetfacl without -R: follow symlinks.
gfsetfacl -m d:o:r $gftmp/a/sym || error "#1-1"
val=$(gfgetfacl $gftmp/DIR | grep "default:other:")
[ "$val" = "default:other::r--" ] || { echo $val; error "#1-2"; }

### #2: gfgetfacl without -R: follow symlinks.
val=$(gfgetfacl $gftmp/a/sym | grep "default:other:")
[ "$val" = "default:other::r--" ] || { echo $val; error "#2-1"; }
gfsetfacl -b $gftmp/a/sym || error "#2-2"
val=$(gfgetfacl $gftmp/DIR | grep "default:other:")
[ "$val" = "" ] || { echo $val; error "#2-3"; }

### #3: gfsetfacl with -R: ignore symlinks and ignore default ACL for files.
gfsetfacl -R -m d:o:rw $gftmp/a || error "#3"

### #4: gfgetfacl with -R: ignore symlinks.
val=$(gfgetfacl -R $gftmp/a | grep default:other:)
val2="default:other::rw-
default:other::rw-
default:other::rw-"
[ "$val" = "$val2" ] || { echo $val; error "#4-1"; }
val=$(gfgetfacl $gftmp/DIR | grep "default:other:")
[ "$val" = "" ] || { echo $val; error "#4-2"; }

clean_test
exit $exit_pass
