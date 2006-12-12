#!/bin/sh

. ./regress.conf

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $pair_file; gfrm $gftmp; exit $exit_trap' $trap_sigs

pair_file=$localtop/RT_gfrep-D_host_and_domain.$$

# search a pair of host and domain that the host is not in the domain
if gfhost | awk '
    {
	pos = index($0, ".")
	host[pos == 0 ? "" : substr($0, pos + 1)] = $0
    }
    END {
	for (domain in host) { 
	    for (i in host) {
		if (domain != "" && match(host[i], domain) == 0) {
		    printf "%s %s FOUND\n", host[i], domain >"'$pair_file'"
		    exit 0
		}
	    }
	}
	printf "%s %s NOT_FOUND\n", host[i], domain >"'$pair_file'"
	exit 0
    }
' && read shost domain is_found <$pair_file
then
    if gfreg -h $shost $datafile $gftmp &&
	gfrep -D $domain $gftmp &&      
	gfwhere $gftmp | awk 'NR > 1 {
	    if ("'$is_found'" == "NOT_FOUND" && NF == 2 && $2 == "'$shost'" ||
		"'$is_found'" == "FOUND" && NF == 3 &&
		    ($2 == "'$shost'" && match($3, "'$domain'") ||
		     $3 == "'$shost'" && match($2, "'$domain'")))
		exit 0
	    else
		exit 1
	}'
    then	
	exit_code=$exit_pass
    fi	
fi

rm  $pair_file
gfrm $gftmp
exit $exit_code
