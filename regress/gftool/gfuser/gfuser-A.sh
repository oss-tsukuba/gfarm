#!/bin/sh

. ./regress.conf

user=tmpusr-`hostname`-$$
user2=tmpusr2-`hostname`-$$
real=tmprealname
home=/home/$user
home2=/home/$user2
gsi_dn=''
auth_id1=tmp_auth_user_id1-`hostname`-$$
auth_id2=tmp_auth_user_id2-`hostname`-$$

if $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

trap 'gfuser -d "$user" 2>/dev/null; gfuser -d "$user2" 2>/dev/null; exit $exit_trap' $trap_sigs

register_or_update() {
    if gfuser -A "$1" "$2" "$3"; then
	if gfuser -L "$1" |
		awk -F ':' 'BEGIN { status = 1 }
		  $0 ~ /^	/ && $1 == "'"	$2"'" && $2 == "'"$3"'" { status = 0 }
		  END { exit status }'
        then
	    return $exit_success
	else
	    return $exit_fail
        fi
    else
	return $exit_fail
    fi
}

prohibit_duplication() {
    if gfuser -A "$1" "$2" "$3" 2>$localtmp; then
	return $exit_fail
    else
	if grep ': already exists' $localtmp >/dev/null; then
	    return $exit_success
	fi
    fi
    return $exit_fail
}

unregister() {
    gfuser -A "$1" "$2" "" &&
    gfuser -L "$1" |
        awk -F ':' 'BEGIN { status = 0 }
		  $0 ~ /^	/ && $1 == "	'"$2"'" { status = 1 }
		  END { exit status }'
}

do_tests() {
  auth_method=$1

  register_or_update "$user" $auth_method "$auth_id1" && # register
  register_or_update "$user" $auth_method "$auth_id2" && # update
  prohibit_duplication "$user2" $auth_method "$auth_id2" &&
  unregister "$user" $auth_method
}

failure_of_clean_up=false
if gfuser -c "$user" "$real" "$home" "$gsi_dn"; then
    if gfuser -c "$user2" "$real" "$home2" "$gsi_dn"; then
	if
	    do_tests SASL && do_tests Kerberos
	then
	    exit_code=$exit_pass
	fi
	if ! gfuser -d "$user2"; then
	    failure_of_clean_up=true
	fi
    fi
    if ! gfuser -d "$user"; then
	failure_of_clean_up=true
    fi
fi

if $failure_of_clean_up; then
  exit_code=$exit_fail
fi

exit $exit_code
