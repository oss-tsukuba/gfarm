<?php
require_once('config.php');

function red_black_compare($a, $b)
{
	if ($a[3] == 'red') {
		$ta = 'a'.$a[0];
	} else {
		$ta = 'b'.$a[0];
	}
	if ($b[3] == 'red') {
		$tb = 'a'.$b[0];
	} else {
		$tb = 'b'.$b[0];
	}
	return $ta > $tb;
}

date_default_timezone_set(TIMEZONE);
$date = $_GET['date'];
if (!is_numeric($date)) {
	$date = 0;
}
$year = $_GET['year'];
if (!is_numeric($year)) {
	$year = Date("Y");
}
$month = $_GET['month'];
if (!is_numeric($month)) {
	$month = Date("M");
}
if (!is_readable(CONFIG_DB)) {
	echo 'Can not read config DB '.CONFIG_DB;
	die(1);
}
try {
$db = new PDO("sqlite:".CONFIG_DB);
$sql = "select val from config where key='warning_limit';";
$result = $db->query($sql);
$row = $result->fetch(PDO::FETCH_NUM);
$warnlimit = $row[0];
unset($result);
unset($db);
} catch (PDOException $e) {
	echo 'Can not read config DB '.CONFIG_DB;
	die(1);
}

if (!is_readable(DATABASE)) {
	echo 'Can not read data DB '.DATABASE;
	die(1);
}
try {
$db = new PDO("sqlite:".DATABASE);
$st = array();
$stmt = $db->prepare("select key,avr,stddev from statistics where date=:date;");
$stmt->bindParam(':date', $date, PDO::PARAM_INT);
$result = $stmt->execute();
while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
	$st[$row['key']] = array();
	$st[$row['key']]['avr'] = $row['avr'];
	$st[$row['key']]['high'] = $row['avr'] + $warnlimit * $row['stddev'];
	$st[$row['key']]['low'] = $row['avr'] - $warnlimit * $row['stddev'];
	$st[$row['key']]['stddev'] = $row['stddev'];
}
unset($result);
unset($stmt);
$stmt = $db->prepare("select key,val,unit from data where date=:date");
$result = $stmt->execute(array(':date' => $date));
$data = array();
while ($row = $stmt->fetch(PDO::FETCH_NUM)) {
	if ($row[1] > $st[$row[0]]['high'] || $row[1] < $st[$row[0]]['low']) {
		$color = "red";
	} else {
		$color = "black";
	}
	$data[] = array($row[0],$row[1],$row[2],$color,
			$st[$row[0]]['avr'], $row[2],
			$st[$row[0]]['stddev'], $row[2]);
}
unset($result);
unset($stmt);
unset($db);
} catch (PDOException $e) {
	echo 'Can not read data DB '.DATABASE;
	die(1);
}

usort($data, 'red_black_compare');
$data2 = array();
foreach($data as $d) {
	if ($d[2] == 'usec') {
		$d[1] = 1000000 / $d[1];
		$d[4] = 1000000 / $d[4];
		$d[6] = 1000000 / $d[6];
		$d[2] = 'ops';
		$d[5] = 'ops';
		$d[7] = 'ops';
	}
	$data2[]=$d;
}
$data = $data2;
$data2 = array();
foreach($data as $d) {
	if ($d[1] > 1000*1000*1000*1000) {
		$d[1] = sprintf("%.02f", $d[1]/(1000*1000*1000*1000));
		$d[2] = 'T'.$d[2];
	} else if ($d[1] > 1000*1000*1000) {
		$d[1] = sprintf("%.02f", $d[1]/(1000*1000*1000));
		$d[2] = 'G'.$d[2];
	} else if ($d[1] > 1000*1000) {
		$d[1] = sprintf("%.02f", $d[1]/(1000*1000));
		$d[2] = 'M'.$d[2];
	} else if ($d[1] > 1000) {
		$d[1] = sprintf("%.02f", $d[1]/(1000));
		$d[2] = 'K'.$d[2];
	} else {
		$d[1] = sprintf("%.02f", $d[1]);
	}
	if ($d[4] > 1000*1000*1000*1000) {
		$d[4] = sprintf("%.02f", $d[4]/(1000*1000*1000*1000));
		$d[5] = 'T'.$d[5];
	} else if ($d[4] > 1000*1000*1000) {
		$d[4] = sprintf("%.02f", $d[4]/(1000*1000*1000));
		$d[5] = 'G'.$d[5];
	} else if ($d[4] > 1000*1000) {
		$d[4] = sprintf("%.02f", $d[4]/(1000*1000));
		$d[5] = 'M'.$d[5];
	} else if ($d[4] > 1000) {
		$d[4] = sprintf("%.02f", $d[4]/(1000));
		$d[5] = 'K'.$d[5];
	} else {
		$d[4] = sprintf("%.02f", $d[4]);
	}
	if ($d[6] > 1000*1000*1000*1000) {
		$d[6] = sprintf("%.02f", $d[6]/(1000*1000*1000*1000));
		$d[7] = 'T'.$d[7];
	} else if ($d[6] > 1000*1000*1000) {
		$d[6] = sprintf("%.02f", $d[6]/(1000*1000*1000));
		$d[7] = 'G'.$d[7];
	} else if ($d[6] > 1000*1000) {
		$d[6] = sprintf("%.02f", $d[6]/(1000*1000));
		$d[7] = 'M'.$d[7];
	} else if ($d[6] > 1000) {
		$d[6] = sprintf("%.02f", $d[6]/(1000));
		$d[7] = 'K'.$d[7];
	} else {
		$d[6] = sprintf("%.02f", $d[6]);
	}
	$data2[] = $d;
}
$data = $data2;
?>
<html>
<head>
<meta http-equiv="content-type" content="text/html; charset=utf8">
<title>Test Results</title>
<style>
div.menu {text-align: right; padding-right: 10px; float: right;}
td.value {text-align: right; padding-right: 1em; }
td.unit  {text-align: left; padding-right: 1em; }
tr.gray {background-color: whitesmoke; }
</style>
</head>
<body>
<div class="menu">
<a href="config_view.php?year=<?php echo $year ?>&month=<?php echo $month ?>&date=<?php echo $date ?>">config</a>
</div>
<a href="index.php?year=<?php echo $year; ?>&month=<?php echo $month; ?>">Top</a>/Results<br>
	<?php
        $pt = strftime('%Y/%m/%d %H:%M:%S',$date);
	?>
	<h1>Test Results(<?php echo $pt;?>)</t1>
	<table>
	<tr><th>key</th><th>value</th><th>unit</th><th>average</th><th>unit</th><th>stddev</th><th>unit</th><tr>
	<?php
	$i=0;
	foreach ($data as $d)
{
	if ($i % 2 == 1) {
		echo "<tr class=\"gray\"><td>";
	} else {
		echo "<tr><td>";
	}
	$opt = "year=".$year."&month=".$month."&date=".$date."&key=".urlencode($d[0]);
	echo "<a href=\"graph_page.php?".$opt."\">".$d[0]."</a>";
	echo "</td><td class=\"value\">";
	echo "<font color=\"".$d[3]."\">";
	echo $d[1];
	echo "</font>";
	echo "</td><td class=\"unit\">";
	echo $d[2];
	echo "</td><td class=\"value\">";
	echo $d[4];
	echo "</td><td class=\"unit\">";
	echo $d[5];
	echo "</td><td class=\"value\">";
	echo $d[6];
	echo "</td><td class=\"unit\">";
	echo $d[7];
	echo "</td></tr>\n";
	$i += 1;
}
	?>
	</table>
</body>
</html>
