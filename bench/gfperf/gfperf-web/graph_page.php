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
$key = $_GET['key'];
?>
<html>
<head>
<meta http-equiv="content-type" content="text/html; charset=utf8">
<title>Result Graph</title>
<style>
	div.menu {text-align: right; padding-right: 10px; float: right;}
</style>
</head>
<body>
<div class="menu">
<a href="config_view.php?year=<?php echo $year ?>&month=<?php echo $month ?>&date=<?php echo $date ?>&key=<?php echo urlencode($key) ?>">config</a>
</div>
<a href="index.php?year=<?php echo $year; ?>&month=<?php echo $month; ?>">Top</a>/<a href="view_result.php?year=<?php echo $year; ?>&month=<?php echo $month; ?>&date=<?php echo $date; ?>">Results</a>/Graph<br>
<img src="graph_draw.php?key=<?php echo urlencode($key) ?>&date=<?php echo $date ?>">
</body>
</html>
