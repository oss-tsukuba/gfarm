<?php
require_once('config.php');

date_default_timezone_set(TIMEZONE);

if (!file_exists(CONFIG_DB)) {
	echo 'Can not find config DB '.CONFIG_DB;
	die(1);
}

if ($_SERVER['REQUEST_METHOD'] == 'GET') {
	$year = $_GET['year'];
	$month = $_GET['month'];
	if (!empty($_GET['date'])) {
		$date = $_GET['date'];
	}
	if (!empty($_GET['key'])) {
		$key = $_GET['key'];
	}
	try {
	$db = new PDO("sqlite:".CONFIG_DB);
	$sql = "select val from config where key='number_of_points_in_graph';";
	$result = $db->query($sql);
	$row = $result->fetch(PDO::FETCH_NUM);
	$npoints = $row[0];
	unset($result);
	$sql = "select val from config where key='warning_limit';";
	$result = $db->query($sql);
	$row = $result->fetch(PDO::FETCH_NUM);
	$warnlimit = $row[0];
	unset($result);
	unset($db);
	} catch (Exception $e) {
		echo 'Can not read config DB '.CONFIG_DB;
		echo "\n<pre>\n" . $e . "\n</pre>\n";
		die(1);
	}
}
if ($_SERVER['REQUEST_METHOD'] == 'POST') {
	$year = $_POST['year'];
	$month = $_POST['month'];
	if (!empty($_POST['date'])) {
		$date = $_POST['date'];
	}
	if (!empty($_POST['key'])) {
		$key = $_POST['key'];
	}
	$npoints = $_POST['npoints'];
	$warnlimit = $_POST['warnlimit'];
	try {
	$db = new PDO("sqlite:".CONFIG_DB);
	$stmt = $db->prepare("update config set val=:val ".
			      "where key='number_of_points_in_graph';");
	$stmt->bindParam(':val', $npoints, PDO::PARAM_INT);
	$stmt->execute();
	unset($stmt);
	$stmt = $db->prepare("update config set val=:val ".
			     "where key='warning_limit';");
	$stmt->bindParam(':val', $warnlimit, PDO::PARAM_INT);
	$stmt->execute();
	unset($stmt);
	unset($db);
	} catch (Exception $e) {
		echo 'Can not write confi DB '.CONFIG_DB;
		echo "\n<pre>\n" . $e . "\n</pre>\n";
		die(1);
	}
	if (empty($key) && empty($date)) {
		header("Location: index.php?year=".$year."&month=".$month);
		exit(0);
	} else if (empty($key)) {
		header("Location: view_result.php?year=".$year."&month=".$month."&date=".$date);
		exit(0);
	} else {
		header("Location: graph_page.php?year=".$year."&month=".$month."&date=".$date."&key=".urlencode($key));
		exit(0);
	}
}
?>
<html>
<head>
<meta http-equiv="content-type" content="text/html; charset=utf8">
<title>Config</title>
<style>
	td.title { text-align: right; }
</style>
</head>
<body>
	<?php if( empty($key) && empty($date)) { ?>    
<a href="index.php?year=<?php echo $year ?>&month=<?php echo $month ?>">Top</a>/Config
	 <?php } else if (empty($key)) { ?>
<a href="index.php?year=<?php echo $year; ?>&month=<?php echo $month; ?>">Top</a>/<a href="view_result.php?year=<?php echo $year; ?>&month=<?php echo $month; ?>&date=<?php echo $date; ?>">Results</a>/Config<br>
	 <?php } else { ?>
<a href="index.php?year=<?php echo $year; ?>&month=<?php echo $month; ?>">Top</a>/<a href="view_result.php?year=<?php echo $year; ?>&month=<?php echo $month; ?>&date=<?php echo $date; ?>">Results</a>/<a href="graph_page.php?year=<?php echo $year; ?>&month=<?php echo $month; ?>&date=<?php echo $date; ?>&key=<?php echo urlencode($key) ?>">Graph</a>/Config<br>
	 <?php } ?>
<h1>Config</h1>
<form method="POST" action="config_view.php">
<table>
<tr>
<td>number of points in graph :
<input type="text" name="npoints" value="<?php echo $npoints ?>"></td>
</tr><tr>
<td>warning limit : average &plusmn; <input type="text" name="warnlimit" value="<?php echo $warnlimit ?>"> &times; stddev</td>
</tr>
</table>
<input type="submit"> <input type="reset">
<input type="hidden" name="year" value="<?php echo $year ?>">
<input type="hidden" name="month" value="<?php echo $month ?>">
	 <?php if( !empty($key)) { ?>    
<input type="hidden" name="key" value="<?php echo $key ?>">
	 <?php } ?>
	 <?php if( !empty($date)) { ?>    
<input type="hidden" name="date" value="<?php echo $date ?>">
	 <?php } ?>
</form>

</body>
</html>
