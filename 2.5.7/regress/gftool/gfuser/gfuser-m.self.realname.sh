#!/bin/sh

# test for SourceForge Ticket #423:
# if user's realname and/or home directory is changed,
# but GSI DN is not changed by gfuser command,
# GSI authentication stops to work against the user

. ./regress.conf

if $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

lock_prefix="XXX.LOCK.XXX-"
user=`gfwhoami`

while
	# waiting until lock is free
	while
		OIFS=$IFS
		IFS=:
		set `gfuser -l "$user"`
		IFS="$OIFS"
		real=$2
		home=$3
		gsi_dn=$4

		case "$real" in
		"${lock_prefix}"*) true;;
		*) false;;
		esac
	do
		sleep 1
	done

	# waiting until lock is aquired by me

	trap '' $trap_sigs # beginning of critical section

	real2="${lock_prefix}tmprealname-${real}-`hostname`-$$"

	if gfuser -m "$user" "$real2" "$home" "$gsi_dn"; then :;
	else exit $exit_fail; fi

	case `gfuser -l "$user" | awk -F: '{print $2}'` in
	"${real2}") false;;
	*) true;;
	esac
do
	trap - $trap_sigs # end of critical section

	sleep 1
done

# clean-up procedure of the critical section
trap 'gfuser -m "$user" "$real" "$home" "$gsi_dn"; exit $exit_code' 0 $trap_sigs
if gfuser -l "$user" | awk -F: 'BEGIN { status = 1 }
	$1 == "'"$user"'" && $2 == "'"$real2"'" &&
	$3 == "'"$home"'" && $4 == "'"$gsi_dn"'" { status = 0 }
	END { exit status }'
then
	exit_code=$exit_pass
fi
