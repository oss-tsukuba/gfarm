<?php
require_once('config.php');

date_default_timezone_set(TIMEZONE);

if (file_exists(CONFIG_DB) == false) {
	try {
	$db = new PDO("sqlite:".CONFIG_DB);
	$sql = "CREATE TABLE config(key string primary key, val string);";
	$db->query($sql);
	$sql = "insert into config(key,val) values('number_of_points_in_graph','90');";
	$db->query($sql);
	$sql = "insert into config(key,val) values('warning_limit','3');";
	$db->query($sql);
	unset($db);
	} catch (Exception $e) {
		echo 'Can not create config DB '.CONFIG_DB;
		echo "\n<pre>\n" . $e . "\n</pre>\n";
		die(1);
	}
}

function get_last_time_of_the_month($year, $month)
{
	$month++;
	if ($month > 12) {
		$year++;
		$month=1;
	}
	$t = mktime(0,0,0,$month,1,$year);
	return $t - 1;
}

if (empty($_GET['year'])) {
	$year = Date("Y");
} else {
	$year = $_GET['year'];
	if (!is_numeric($year)) {
		$year = Date("Y");
	}
}
if (empty($_GET['month'])) {
	$month = Date("m");
} else {
	$month = $_GET['month'];
	if (!is_numeric($month)) {
		$month = Date("m");
	}
}

$s = mktime(0,0,0,$month,1,$year);
$e = get_last_time_of_the_month($year, $month);

if (!is_readable(DATABASE)) {
	echo 'Can not read data DB '.DATABASE;
	die(1);
}

try {
$db = new PDO('sqlite:'.DATABASE);
$sql="select date from error_msg where date >= $s and date <= $e group by date order by date desc;";
$result = $db->query($sql);
$err_dates=array();
while ($row = $result->fetch(PDO::FETCH_NUM)) {
	$err_dates[$row[0]]=0;
}
unset($result);
$sql="select date from data where date >= $s and date <= $e group by date order by date desc;";
$result = $db->query($sql);
$dates=array();
while ($row = $result->fetch(PDO::FETCH_NUM)) {
	$dates[$row[0]]=0;
}
unset($result);
$sql="select date, end_date from execute_time where date >= $s and date <= $e;";
$result = $db->query($sql);
$end_dates=array();
while ($row = $result->fetch(PDO::FETCH_NUM)) {
	$end_dates[$row[0]]=$row[1];
}
unset($result);
$sql="select date,count from error where date >= $s and date <= $e order by date desc;";
$result = $db->query($sql);
while ($row = $result->fetch(PDO::FETCH_NUM)) {
	$dates[$row[0]]=$row[1];
}
unset($result);
unset($db);
} catch (Exception $e) {
	echo 'Can not read data DB '.DATABASE;
	echo "\n<pre>\n" . $e . "\n</pre>\n";
	die(1);
}
krsort($dates);
?>
<html>
<head>
<meta http-equiv="content-type" content="text/html; charset=utf8">
<title>gfperf</title>
<style>
div.menu {text-align: right; padding-right: 10px; float: right;}
td {padding-right: 5px; padding-left: 5px;}
</style>
</head>
<body>
<div class="menu">
<a href="config_view.php?year=<?php echo $year ?>&month=<?php echo $month ?>">config</a>
</div>
	<h1>Gfperf Top Page</h1>
	<table><tr><td>
	<form method="get" action "#">
	year:<input type="text" name="year" value="<?php echo $year ?>" size=4>
	<select name="month" onChange="this.form.submit();">
	<option value="01" <?php if ($month == "01") echo "selected" ?>/>Jan.
	<option value="02" <?php if ($month == "02") echo "selected" ?>/>Feb.
	<option value="03" <?php if ($month == "03") echo "selected" ?>/>Mar.
	<option value="04" <?php if ($month == "04") echo "selected" ?>/>Apr.
	<option value="05" <?php if ($month == "05") echo "selected" ?>/>May
	<option value="06" <?php if ($month == "06") echo "selected" ?>/>Jun.
	<option value="07" <?php if ($month == "07") echo "selected" ?>/>Jul.
	<option value="08" <?php if ($month == "08") echo "selected" ?>/>Aug.
	<option value="09" <?php if ($month == "09") echo "selected" ?>/>Sep.
	<option value="10" <?php if ($month == "10") echo "selected" ?>/>Oct.
	<option value="11" <?php if ($month == "11") echo "selected" ?>/>Nov.
	<option value="12" <?php if ($month == "12") echo "selected" ?>/>Dec.
	</select>
<!--	<input type=submit value="print">   -->
	</form>
        </td><td>
	<form method="get" action "#">
	<input type=submit value="this month">
	</form>
        </td></tr></table>
	<table>
	<?php
	foreach ($dates as $d => $c)
{
	$pt = strftime('%Y/%m/%d %H:%M:%S',$d); 
	echo "<tr><td>";
	echo "<a href=\"view_result.php?year=".$year."&month=".$month."&date=$d\">".$pt."</a>";
	if (isset($end_dates[$d])) {
		$et = strftime('%Y/%m/%d %H:%M:%S',$end_dates[$d]);
		echo " - ".$et;
	}
	echo "</td>";
	if ($c > 0) {
		if (isset($err_dates[$d])) {
			$mes = "<a href=\"view_error.php?year=".$year."&month=".$month."&date=$d\">Error Occurred!</a>";
		} else {
			$mes = "Error Occurred!";
		}
		echo "<td><font color=\"red\">".$mes."</font></td>";
	} else {
		echo "<td></td>";
	}
	echo"</tr>";
	echo "\n";
}
	?>
	</table>
</body>
</html>
