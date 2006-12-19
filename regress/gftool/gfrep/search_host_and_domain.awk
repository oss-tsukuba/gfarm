# search a pair of host and domain to which the host does not belong

{
    pos = index($0, ".")
    host[pos == 0 ? "" : substr($0, pos + 1)] = $0
}

END {
    for (domain in host) { 
	for (i in host) {
	    if (domain != "" && match(host[i], domain) == 0) {
		printf "%s %s\n", host[i], domain
		exit
	    }
	}
    }
    # Although an adeqatet pair is not found,
    # we test gfrep with a host and domain to which the host belongs.
    printf "%s %s\n", host[i], domain
}
