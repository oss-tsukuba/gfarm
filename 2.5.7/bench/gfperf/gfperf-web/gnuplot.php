<?php
require_once('config.php');
date_default_timezone_set(TIMEZONE);

if (!defined('GNUPLOT')) {
	define('GNUPLOT', '/usr/bin/gnuplot');
}

class ProcessIO {
	private $stdin, $stdout, $stderr;

	public function __construct($pipes) {
		$this->stdin  =& $pipes[0];
		$this->stdout =& $pipes[1];
		$this->stderr =& $pipes[2];
	}

	public function write($data) {
		fwrite($this->stdin, $data);
		// fwrite(STDERR, $arg); // for debug
	}

	public function get_stdout_data() {
		if (!is_null($this->stdin)) {
			fclose($this->stdin);
			$this->stdin = NULL;
		}
		if (is_null($this->stdout)) {
			return NULL;
		}
		return stream_get_contents($this->stdout);
	}

	public function get_stderr_data() {
		if (!is_null($this->stdin)) {
			fclose($this->stdin);
			$this->stdin = NULL;
		}
		if (is_null($this->stderr)) {
			return NULL;
		}
		return stream_get_contents($this->stderr);
	}

	public function close() {
		if (!is_null($this->stdout)) {
			fclose($this->stdout);
			$this->stdout = NULL;
		}
		if (!is_null($this->stderr)) {
			fclose($this->stderr);
			$this->stderr = NULL;
		}
	}
}

class ProcessOpen {
	private $io, $proc;

	public function __construct($path) {
		$this->proc = proc_open($path, array(0 => array('pipe', 'r'),
											 1 => array('pipe', 'w'),
											 2 => array('pipe', 'w')),
								$pipes);
		if (!is_resource($this->proc)) {
			trigger_error("proc_open() error", E_USER_ERROR);
		}
		$this->io = new ProcessIO($pipes);
	}

	public function write($str) {
		$this->io->write($str);
	}

	public function puts($str) {
		$this->io->write($str . "\n");
	}

	public function get_result() {
		return $this->io->get_stdout_data();
	}

	public function get_error() {
		return $this->io->get_stderr_data();
	}

	public function close() {
		$this->io->close();
		proc_close($this->proc);
	}
}

class GNUPlot {
	private $gplot, $data, $xlabel, $ylabel, $xlabels, $title, $ymax;

	// default
	private $size_x = 800;
	private $size_y = 600;
	private $grid   = false;
	private $style  = "linespoints";

	public function __construct($data) {
		$this->gplot = new ProcessOpen(GNUPLOT);
		$this->data = $data;
		$this->check_data();
	}

	public function data() {
		return $this->data;
	}

	public function title() {
		return $this->title;
	}

	public function set_ymax($max) {
		$this->ymax = $max;
	}

	public function set_title($title) {
		$this->title = $title;
	}

	public function set_size($x, $y) {
		if(!is_int($x) || !is_int($y)) {
			trigger_error("set_size() must be integer", E_USER_WARNING);
		} else {
			$this->size_x = $x;
			$this->size_y = $y;
		}
	}

	public function size_x() {
		return $this->size_x;
	}

	public function size_y() {
		return $this->size_y;
	}

	public function set_xlabel($xlabel) {
		$this->xlabel = $xlabel;
	}

	public function xlabel() {
		return $this->xlabel;
	}

	public function set_ylabel($ylabel) {
		$this->ylabel = $ylabel;
	}

	public function ylabel() {
		return $this->ylabel;
	}

	public function set_xlabels($xlabels) {
		$this->xlabels = $xlabels;
	}

	public function xlabels() {
		return $this->xlabels;
	}

	public function set_grid($grid) {
		if (!is_bool($grid)) {
			trigger_error("grid must be boolean", E_USER_WARNING);
		}
		else $this->grid = $grid;
	}

	public function is_grid() {
		return $this->grid;
	}

	public function set_style($style) {
		$this->style = $style;
	}

	public function style() {
		return $this->style;
	}

	private function check_data() {
		if (count($this->data()) < 1) {
			trigger_error("no data", E_USER_WARNING);
		} else {
			foreach ($this->data() as $titile => $data) {
				foreach ($data as $time => $value) {
					if (!is_numeric($time)) {
						trigger_error(
							"time format error: time must be numeric",
							E_USER_ERROR);
					}
					if (!is_numeric($value)) {
						trigger_error(
							"data format error: data must be numeric",
							E_USER_ERROR);
					}
				}
			}
		}
	}

	private function init_common() {
		if (!is_null($this->title())) {
			$this->gplot->puts("set title \"" . $this->title() . "\"");
		}
		$this->gplot->puts("set yrange[0:]");
		$this->gplot->puts("set key left top");
		$this->gplot->puts("set xdata time");
		$this->gplot->puts("set timefmt \"%s\"");
		//$this->gplot->puts("set xtics rotate");
		$this->gplot->puts("set format x \"%m-%d\\n%H:%M\"" );
		$this->gplot->puts("set term png size " .
								 $this->size_x() . "," . $this->size_y());
		if ($this->is_grid()) {
			$this->gplot->puts("set grid");
		}
		if (!is_null($this->xlabel())) {
			$this->gplot->puts("set xlabel \"" . $this->xlabel() ."\"");
		}
		if (!is_null($this->ylabel())) {
			$this->gplot->puts("set ylabel \"" . $this->ylabel() ."\"");
		}
		$this->gplot->puts("set offsets 0, 0, 0, 0");
	}

	private function init_xlabels() {
		if (is_null($this->xlabels())) {
			return;
		}
		$this->gplot->write("set xtics (");
		$n = 0;
		$n_xlabels = count($this->xlabels());
		foreach ($this->xlabels() as $d) {
			$this->gplot->write(sprintf("'%s' %d", $d, ++$n) .
								($n != $n_xlabels ? ", " : ""));
		}
		$this->gplot->puts(")");
	}

	private function get_timezone_offset() {
		$iTime = time();
		$arr = localtime($iTime);
		$arr[5] += 1900;
		$arr[4]++;
		$iTztime = gmmktime($arr[2], $arr[1], $arr[0], $arr[4],
				    $arr[3], $arr[5], $arr[8]);
		return $iTztime-$iTime;
	}

	private function plot() {
		$max = 0;
		$min = -1;
		foreach ($this->data() as $title => $data) {
			foreach ($data as $time => $value) {
				if ($time > $max) {
					$max = $time;
				}
				if ($min == -1 || $time < $min) {
					$min = $time;
				}
			}
		}
		$this->gplot->write("plot ");
		$n = count($this->data());
		foreach ($this->data() as $title => $data) {
			$this->gplot->write("'-' using 1:2 title '" . $title .
									        "' with " .
									  $this->style() . " lt " .
									        $n .
									  " lw 1 pt 13 ps 1");
			if ($n-- > 1) {
				$this->gplot->write(", ");
			}
		}
		$this->gplot->puts("");
		$off = $this->get_timezone_offset();
		foreach ($this->data() as $title => $data) {
			foreach ($data as $time => $value) {
				$this->gplot->puts(
					sprintf("%d %f", ($time + $off), $value));
			}
			$this->gplot->puts("e");
		}
	}

	public function stroke() {
		$this->init_common();
		$this->init_xlabels();
		$this->plot();
		header("Content-type: image/png");
		echo $this->gplot->get_result();
		//fputs(STDERR, $this->gplot->get_error());
		$this->gplot->close();
    }
}
?>
