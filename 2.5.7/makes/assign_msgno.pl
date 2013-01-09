#!/usr/bin/perl -w
#
# catgets() mesasge number assignment tool
#
use File::Temp qw/ :mktemp /;
$gfarmtopdir=$ENV{"GFARM_TOPDIR"};
$number_file="$gfarmtopdir/makes/.msgno";
$msgno_base=1000000;
$msgno_used=0;
$msgno_orig=0;

$header_file="$gfarmtopdir/include/gfarm/gfarm_msg_enums.h";

sub assign_msgno {
	my($file, $current) = @_;
	my($count) = 0;

	open(IN, $file);
	($OUT, $tmpfile) = mkstemp("/tmp/assign_msgno.XXXXXX");

	while ($line = <IN>) {
		$msg_str = sprintf("GFARM_MSG_%07d", $current + $count + 1);
		if ($line =~ s/GFARM_MSG_UNFIXED/$msg_str/) {
			$count++;
		}
		print $OUT $line;
	}
	close(IN);
	close(OUT);

	if ($count != 0) {
		system("mv $tmpfile $file");
		printf("replaced %d messages in file: %s\n", $count, $file);
	} else {
		unlink($tmpfile);
	}
	return $current + $count;
}

sub append_header {
	my($from, $to) = @_;
	open(HDROUT, ">>$header_file");
	for ($i = $from; $i <= $to; $i++) {
		$hdr_str = sprintf("#define GFARM_MSG_%07d\t%07d\n", $i, $i);
		print HDROUT $hdr_str;
	}
	close(HDROUT);
}

# main
if (-e $number_file) {
	$msgno_used=`cat $number_file`;
} else {
	print "message number file initialized, because $number_file not found\n";
	$msgno_used=$msgno_base;
}

$msgno_orig=$msgno_used;

foreach $f (@ARGV) {
    chomp($f);
    $msgno_used = assign_msgno($f, $msgno_used);
}

if ($msgno_used != $msgno_orig) {
	append_header($msgno_orig + 1, $msgno_used);

	open(OUT, ">$number_file");
	printf OUT $msgno_used;
	close(OUT);

	printf("max msgno is now %d.\n", $msgno_used);
	printf("don't forget to commit $number_file file.\n");
}
