#! /usr/bin/perl

# for glogger3 (#define PRINTTIME)
# for microsecond

$MAX = 2000000;  # 2 second

# global

# saving time   0:hour, 1:minute, 2:second, 3:microsec
@start;    
@late;
$clockstart;

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
	&settime(*now, *start);
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

sub settime {
    local(*now, *t) = @_;

    @line = split(/ /, $now);
    @timestr = split(/\./, $line[0]);
    @hms = split(/:/, $timestr[0]);  # hour min sec
    
    # hour
    $t[0] = int($hms[0]);
    
    # minute
    $t[1] = int($hms[1]);
    
    # second
    $t[2] = int($hms[2]);
    
    # micro second
    $t[3] = int($timestr[1]);
}

#########################################################

sub main {
    local(*count, *min, *avg, *max, *dis, $now) = @_;

    &settime(*now, *late);

    $hour = $late[0] - $start[0];
    $minu = $late[1] - $start[1];
    $sec  = $late[2] - $start[2];
    $ms   = $late[3] - $start[3];
    
    if($ms < 0){
	$ms = $ms + 1000000;
	$sec = $sec - 1;
    }
    if($sec < 0){
	$sec = $sec + 60;
	$minu = $minu - 1;
    }
    if($minu < 0){
	$minu = $minu + 60;
	$hour = $hour - 1;
    }

    #print "$hour $minu $sec $ms ";

    # $tmp < 60second
    $tmp = $ms + $sec*1000000;

    #print "******\n";

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

    $dev = sqrt($dis);
    $percent = int(100*$dev/$avg);
 
    #print "\#$count now=$tmp min=$min avg=$avg max=$max dev=$dev $percent%\n";
    printf("\#%d now=%4d min=%4d avg=%4d max=%4d dev=%3d %2d%%\n",
	   $count, $tmp, $min, $avg, $max, $dev, $percent);
    #$count++;
}
