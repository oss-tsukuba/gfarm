#!/bin/sh

. ./regress.conf

user=tmpusr-`hostname`-$$
user2=tmpusr2-`hostname`-$$
real=tmprealname
home=/home/$user
home2=/home/$user2
gsi_dn=''
auth_id1='auth_id1'
auth_id2='auth_id2'

if $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

trap 'gfuser -d "$user" 2>/dev/null; gfuser -d "$user2" 2>/dev/null; exit $exit_trap' $trap_sigs

function regist_or_update() {
    if gfuser -A "$1" "$2" "$3"; then
	if gfuser -L "$1" |
		awk -F '[:\t]' 'BEGIN { status = 1 }
		  $2 == "'"$2"'" && $3 == "'"$3"'" { status = 0 }
		  END { exit status }'
        then
	    return 0
	else
	    return -1
        fi
    fi
}

function update_fail() {
    if gfuser -A "$1" "$2" "$3" 2>$localtmp; then
	return 1
    else
	if grep ': already exists' $localtmp >/dev/null; then
	    return 0
	fi
    fi
    return 1
}

function delete() {
    if gfuser -A "$1" "$2" ""; then
	if gfuser -L "$1" |
		awk -F '[:\t]' 'BEGIN { status = 0 }
		  $2 == "'"$2"'" { status = 1 }
		  END { exit status }'
        then
	    return 0
        fi
    fi
    return 1
}


if gfuser -c "$user" "$real" "$home" "$gsi_dn"; gfuser -c "$user2" "$real" "$home2" "$gsi_dn";
then
    
    while :
    do
	# regist
        regist_or_update "$user" SASL "$auth_id1"
	if [ $? -ne 0 ]; then
	    exit_code=$exit_fail
	    break $exit_code
        fi

        regist_or_update "$user" Kerberos "$auth_id1"
	if [ $? -ne 0 ]; then
	    exit_code=$exit_fail
	    break $exit_code
        fi

	# update
        regist_or_update "$user" SASL "$auth_id2"
	if [ $? -ne 0 ]; then
	    exit_code=$exit_fail
	    break $exit_code
        fi

        regist_or_update "$user" Kerberos "$auth_id2"	
	if [ $? -ne 0 ]; then
	    exit_code=$exit_fail
	    break $exit_code
        fi

	# duplicate
	update_fail "$user2" SASL "$auth_id2"
	if [ $? -ne 0 ]; then
	    exit_code=$exit_fail
	    break $exit_code
        fi

	update_fail "$user2" Kerberos "$auth_id2"
	if [ $? -ne 0 ]; then
	    exit_code=$exit_fail
	    break $exit_code
        fi

	# delete
	delete "$user" SASL
	if [ $? -ne 0 ]; then
	    exit_code=$exit_fail
	    break $exit_code
        fi

	delete "$user" Kerberos	
	if [ $? -ne 0 ]; then
	    exit_code=$exit_fail
	    break $exit_code
        fi

	exit_code=$exit_pass
	break
    done
    gfuser -d "$user"
    gfuser -d "$user2"    
fi

exit $exit_code
