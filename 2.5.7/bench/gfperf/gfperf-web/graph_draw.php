<?php
require_once('config.php');
require_once("gnuplot.php");

date_default_timezone_set(TIMEZONE);

$key = $_GET['key'];
$date = $_GET['date'];
if (!is_readable(CONFIG_DB)) {
	echo 'Can not read config DB '.CONFIG_DB;
	die(1);
}
try {
$db = new PDO("sqlite:".CONFIG_DB);
$sql = "select val from config where key='number_of_points_in_graph';";
$result = $db->query($sql);
$row = $result->fetch(PDO::FETCH_NUM);
$npoints = $row[0];
unset($result);
unset($db);
} catch (Exception $e) {
	echo 'Can not read config DB '.CONFIG_DB;
	echo "\n<pre>\n" . $e . "\n</pre>\n";
	die(1);
}

if (!is_readable(DATABASE)) {
	echo 'Can not read data DB '.DATABASE;
	die(1);
}
try {
$db = new PDO("sqlite:".DATABASE);
$stmt = $db->prepare("select date,val,unit from data where key=:key ".
		     "and date<=:date ".
		     "order by date desc limit :npoints ;");
$stmt->bindParam(':key', $key, PDO::PARAM_STR);
$stmt->bindParam(':npoints', $npoints, PDO::PARAM_INT);
$stmt->bindParam(':date', $date, PDO::PARAM_INT);
$result = $stmt->execute();
$values = array();
$val_max = 0;
while ($row = $stmt->fetch(PDO::FETCH_NUM)) {
	$time = $row[0];
	if ($row[2] == 'usec') {
		$val = 1000000 / $row[1];
		$y_unit = 'ops';
	} else {
		$val = $row[1];
		$y_unit = $row[2];
	}
	$values[$time] = $val;
	if ($val_max < $val) {
		$val_max = $val;
	}
}
unset($result);
unset($stmt);
unset($db);
} catch (Exception $e) {
	echo 'Can not read data DB '.DATABASE;
	echo "\n<pre>\n" . $e . "\n</pre>\n";
	die(1);
}

$title = $key;
ksort($values);
//------------------------------------------------------------------
$data = array("Result" => $values);

$gp = new GNUPlot($data);
$gp->set_title($title);
$gp->set_xlabel("time");
$gp->set_ylabel($y_unit);
$gp->set_ymax($val_max);
$gp->set_size(800, 600);
$gp->set_grid(true);
$gp->set_style("linespoints");
$gp->stroke();
?>
