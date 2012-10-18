<?php
require_once('config.php');

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

try {
$db = new PDO('sqlite:'.DATABASE);
$err = array();
$stmt = $db->prepare("select command,message from error_msg where date=:date;");
$stmt->bindParam(':date', $date, PDO::PARAM_INT);
$result = $stmt->execute();
while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
	$tmp = array();
	$tmp['command'] = $row['command'];
	$tmp['message'] = $row['message'];
	$err[] = $tmp;
}
unset($result);
unset($stmt);

unset($db);
} catch (Exception $e) {
	echo 'Can not read data DB '.DATABASE;
	echo "\n<pre>\n" . $e . "\n</pre>\n";
	die(1);
}
?>
<html>
<head>
<meta http-equiv="content-type" content="text/html; charset=utf8">
<title>error messages</title>
<style type="text/css">
div.command {background-color: whitesmoke; margin: 3px;}
div.message {background-color: lightyellow; margin: 3px;}

td.group {
margin: 10px;
          border: 1px solid black;
}
</style>
</head>
<body>
<a href="index.php?year=<?php echo $year; ?>&month=<?php echo $month; ?>">Top</a>/Errors<br>
<?php
 $pt = strftime('%Y/%m/%d %H:%M:%S',$date);
?>
<h1>Error Messages (<?php echo $pt; ?>)</h1>
<table>
<?php
foreach($err as $e) {
	echo "<tr><td class=\"group\">";
	echo "<div class=\"command\">";
	echo $e['command'];
	echo "</div>";
	echo "<div class=\"message\"><pre>";
	echo $e['message'];
	echo "</pre></div>";
	echo "</td></tr>\n";
}
?>
</table>
</body>
</html>
