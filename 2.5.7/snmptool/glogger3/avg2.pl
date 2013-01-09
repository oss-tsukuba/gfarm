#! /usr/bin/perl

# for glogger3 (#define PRINTTIME)
# for rdtsc count

$MAX = 90000000;  # clock

$enablediv = 1;  # 1:on

# global

$start;    
$late;

# for REQUEST
$request;
$rcount = 1;
$rmin = $MAX;
$ravg;
$rmax = 0;
$rdis; # dispersion "bunsan"

# for WAIT
$sleepwait;
$scount = 1;
$smin = $MAX;
$savg;
$smax = 0;
$sdis;  

# for RECIEVE
$receive;
$gcount = 1;
$gmin = $MAX;
$gavg;
$gmax = 0;
$gdis;  

# for WRITE
$write;
$wcount = 1;
$wmin = $MAX;
$wavg;
$wmax = 0;
$wdis;

# for FLUSH
$flush;
$fcount = 1;
$fmin = $MAX;
$favg;
$fmax = 0;
$fdis;

$v = 1;

$count = 1;
while(<STDIN>){
    $now = $_;
    if($now =~ /START/){ # START
	if($v = 1) {print $now};
	$request = "";
	$sleepwait = "";
	$receive = "";
	$write = "";
	$timeout = 0;
    }
    elsif($now =~ /REQUEST/){ # start
	if($v = 1) {print $now};
	&setclock(*now, *start);
    }
    elsif($now =~ /WAIT/){    # REQUEST <-> WAIT
	if($v = 1) {print $now};
	$request = $now;
    }
    elsif($now =~ /RECEIVE/){ # WAIT <-> RECEIVE
	if($v = 1) {print $now};
	$sleepwait = $now;
    }
    elsif($now =~ /CANCEL/){  # cancel
	print $now;
	$timeout = 1;
    } 
    elsif($now =~ /TIMEOUT/){ # timeout
	print "TIMEOUT\n";
	$timeout = 1;
    }
    elsif($now =~ /WRITE/){   # RECEIVE <-> WRITE
	if($v = 1) {print $now};
	$receive = $now;
    }
    elsif($now =~ /FLUSH/){   # WRITE <-> FLUSH
	if($v = 1) {print $now};
	$write = $now;
    }
    elsif($now =~ /END/ && $timeout == 0){  # FLUSH <-> END
	if($v = 1) {print $now};
	$flush = $now;

	print "REQUEST:";
	&main(*count, *rmin, *ravg, *rmax, *rdis, $request);
	print "WAIT   :";
	&main(*count, *smin, *savg, *smax, *sdis, $sleepwait);
	print "RECEIVE:";
	&main(*count, *gmin, *gavg, *gmax, *gdis, $receive);
	print "WRITE  :";
	&main(*count, *wmin, *wavg, *wmax, *wdis, $write);
	print "FLUSH  :";
	&main(*count, *fmin, *favg, *fmax, *fdis, $flush);
	#&flush('STDOUT')
	$count++;
    }
    else {
	#print "else\n";
    }
}

#########################################################

sub setclock {
    local(*now, *clock) = @_;

    @line = split(/ /, $now);
#    @line2 = split(/ /, $line[2]);
    $clock = $line[2];
}

#########################################################

sub main {
    local(*count, *min, *avg, *max, *dis, $now) = @_;

    &setclock(*now, *late);
    $tmp = $late - $start;
    &main2(*count, *min, *avg, *max, *dis, $tmp);
}

sub main2 {
    local(*count, *min, *average, *max, *dispersion, $tmp) = @_;

    if($tmp > $max){
	$max = $tmp;
    }
    if($tmp < $min){
	$min = $tmp;
    }
    
    #$average = ($average*($count-1)+$tmp)/$count;
    $average = (($count-1)/$count)*$average + $tmp/$count;
    $avg = int($average);
    
    $dispersion =
	($dispersion*($count-1)+($tmp-$average)*($tmp-$average))/$count;
    $dis = int($dispersion);

    if($enablediv == 1){    
        $dev = sqrt($dis);
	$percent = int(100*$dev/$avg);
	printf("\#%d now=%4d min=%4d avg=%4d max=%4d dev=%3d %2d%%\n",
	       $count, $tmp, $min, $avg, $max, $dev, $percent);
    } else {
	printf("\#%d now=%4d min=%4d avg=%4d max=%4d\n",
	       $count, $tmp, $min, $avg, $max);
    }
}
