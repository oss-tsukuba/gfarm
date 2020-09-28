#!/bin/sh

test_for_gfarmroot=true
base="$(dirname "$0")"
. "${base}/effective_perm.common.sh"

if $regress/bin/am_I_gfarmadm; then :; else
    exit $exit_unsupported
fi
if $regress/bin/am_I_gfarmroot; then :; else
    exit $exit_unsupported
fi

### group
gfroot_enable
gfchown $tmpuser1:$tmpgroup1 $testdir $testfile || error "gfchown"
gfgroup -a -m $tmpgroup1 $user || error "gfgroup -a -m"
test_ep "000" "---"
test_ep "070" "rwx"
test_ep "040" "r--"
test_ep "020" "-w-"
test_ep "010" "--x"
gfgroup -r -m $tmpgroup1 $user

### other
gfroot_enable
gfchown $tmpuser1:$tmpgroup2 $testdir $testfile || error "gfchown"
test_ep "000" "---"
test_ep "007" "rwx"
test_ep "004" "r--"
test_ep "002" "-w-"
test_ep "001" "--x"

### ACL user
gfroot_enable
gfchown $tmpuser1:$tmpgroup2 $testdir $testfile || error "gfchown"
gfchmod 000 $testdir $testfile || error "gfchmod"
test_ep_acl "u:$user:---" "---"
test_ep_acl "u:$user:rwx" "rwx"
test_ep_acl "u:$user:r--" "r--"
test_ep_acl "u:$user:-w-" "-w-"
test_ep_acl "u:$user:--x" "--x"
gfroot_enable
gfsetfacl -b $testdir $testfile || error "gfsetfacl -b"
gfroot_disable

### ACL group
gfroot_enable
gfchown $tmpuser1:$tmpgroup2 $testdir $testfile || error "gfchown"
gfgroup -a -m $tmpgroup1 $user || error "gfgroup"
gfchmod 000 $testdir $testfile || error "gfchmod"
test_ep_acl "g:$tmpgroup1:---" "---"
test_ep_acl "g:$tmpgroup1:rwx" "rwx"
test_ep_acl "g:$tmpgroup1:r--" "r--"
test_ep_acl "g:$tmpgroup1:-w-" "-w-"
test_ep_acl "g:$tmpgroup1:--x" "--x"
gfroot_enable
gfsetfacl -b $testdir $testfile || error "gfsetfacl -b"
gfroot_disable

exit_code=$exit_pass

cleanup
exit $exit_code
