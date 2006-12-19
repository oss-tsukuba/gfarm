# check 

BEGIN {
    shost = ARGV[1]  
    domain = ARGV[2]
    ARGV[1] = "-"
}

NR > 1 {
    if (match(shost, domain)) {
	# In this case, gfrep must not replicate gfarm file,
        # because the source host belongs to the domain,
	# and the file fragment only remains on the source host,
	# therefore output from gfwhere likes
	#
	# gfarm:afile
	# 0: foo.example.com
	if (NF == 2 && $2 == shost)
	    exit 0	
    } else {
	# In this case, gfrep have to replicate gfarm file,
	# output from gfwhere likes
	#
	# gfarm:afile
	# 0: foo.one.com bar.other.org
	if (NF == 3 &&
	    ($2 == shost && match($3, domain) ||
	     match($2, domain) && $3 == shost))
	    exit 0
    }
    exit 1  
}
