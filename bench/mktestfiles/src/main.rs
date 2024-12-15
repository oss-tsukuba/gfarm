use docopt::Docopt;

use rand::distributions::Alphanumeric;
use rand::{thread_rng, Rng};
use rayon::prelude::*;
use std::fs::create_dir;
use std::fs::remove_dir_all;
use std::fs::File;
use std::io::prelude::*;
use std::io::stdin;
use std::io::stdout;
use std::path::Path;
use std::process;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::thread::sleep;
use std::time::Duration;
use std::time::Instant;

const USAGE: &'static str = "
Usage:
    mktestfiles [options]

Options:
    -o, --output-dir=DIR        Output directory [default: ./testdir]
    -n, --num-files=NUM         Number of files per directory [default: 10]
    -N, --num-directories=NUM   Number of directories for files [default: 10]
    -l, --filename-length=LEN   Length of the test file names (max: 255)
                                [default: 255]
    -s, --min-size=SIZE         Minimum file size in bytes [default: 0]
    -S, --max-size=SIZE         Maximum file size in bytes [default: 0]
                                (Random size when -S is greater than -s)
    -O, --overwrite             Remove existing output directory
    -f, --force                 No prompt before overwrite (-Of)
    --disable-random            Use fixed data and fixed filename
    -p, --parallel              Run in parallel (usually slow in local fs)
    --sleep=SEC                 Sleep time to debug -p [default: 0]
    -q, --quiet                 Quiet messages
    -v, --verbose               Verbose messages
    -d, --debug                 Debug messages
    -h, --help                  Show this message
    --version                   Print version
";

fn confirm(message: &str) -> bool {
    print!("{} (y/n) ", message);
    stdout().flush().unwrap();
    let mut input = String::new();
    stdin().read_line(&mut input).expect("Cannot read stdin");
    input = input.trim().to_lowercase();
    input == "y"
}

fn msg_debug(message: &str) {
    eprintln!("DEBUG: {}", message);
}

fn msg_info(message: &str) {
    eprintln!("INFO: {}", message);
}

fn msg_normal(message: &str) {
    eprintln!("{}", message);
}

fn msg_progress(message: &str) {
    eprint!("{}\r", message);
}

fn msg_noop(_message: &str) {}

fn main() {
    let args = Docopt::new(USAGE)
        .and_then(|dopt| dopt.parse())
        .unwrap_or_else(|e| e.exit());

    let opt_version = args.get_bool("--version");
    if opt_version {
        println!("{} {}", env!("CARGO_PKG_NAME"), env!("CARGO_PKG_VERSION"));
        process::exit(0);
    }

    let opt_debug = args.get_bool("--debug");
    let mut opt_verbose = args.get_bool("--verbose");
    let mut opt_quiet = args.get_bool("--quiet");

    let debug: fn(&str);
    let info: fn(&str);
    let print_msg: fn(&str);
    let progress: fn(&str);

    if opt_debug {
        opt_verbose = true;
        debug = msg_debug;
    } else {
        debug = msg_noop;
    }
    if opt_verbose {
        opt_quiet = false;
        info = msg_info;
    } else {
        info = msg_noop;
    }
    if opt_quiet {
        print_msg = msg_noop;
        progress = msg_noop;
    } else {
        print_msg = msg_normal;
        progress = msg_progress;
    }

    debug(&format!("{:?}", args));

    let num_files: usize = args.get_str("--num-files").parse().unwrap();
    let num_directories: usize = args.get_str("--num-directories").parse().unwrap();
    let min_size: usize = args.get_str("--min-size").parse().unwrap();
    let mut max_size: usize = args.get_str("--max-size").parse().unwrap();
    if min_size > max_size {
        max_size = min_size;
    }
    let overwrite = args.get_bool("--overwrite");
    let force = args.get_bool("--force");
    let disable_random = args.get_bool("--disable-random");
    let mut file_name_length: usize = args.get_str("--filename-length").parse().unwrap();
    let outdir_path = Path::new(args.get_str("--output-dir"));
    let parallel = args.get_bool("--parallel");
    let sleep_sec: f64 = args.get_str("--sleep").parse().unwrap();

    let file_digits = ((num_files - 1) as f64).log10() as usize + 1;
    let dir_digits = ((num_directories - 1) as f64).log10() as usize + 1;
    debug(&format!(
        "file_digits={}, dir_digits={}",
        file_digits, dir_digits
    ));
    if file_digits + 1 > file_name_length {
        file_name_length = file_digits + 1;
    }
    // 0001_<rand name>
    let rand_name_len;
    if file_name_length > file_digits + 1 {
        rand_name_len = file_name_length - file_digits - 1;
    } else {
        rand_name_len = 0;
    }

    let message = format!(
        "Are you sure you want to delete the directory {}?",
        outdir_path.display()
    );
    if overwrite {
        if force || confirm(&message) {
            if outdir_path.exists() {
                println!("removing directory: {}", outdir_path.display());
                remove_dir_all(outdir_path).expect(&format!(
                    "Failed to remove existing directory: {}",
                    outdir_path.display()
                ));
            }
        }
    }
    create_dir(&outdir_path).expect(&format!(
        "Failed to create directory: {}",
        outdir_path.display()
    ));

    if !parallel {
        rayon::ThreadPoolBuilder::new()
            .num_threads(1)
            .build_global()
            .unwrap();
    }

    let start_time = Instant::now();
    let total_num = num_directories * num_files;

    let count_atomic = AtomicUsize::new(0);
    let size_atomic = AtomicUsize::new(0);

    for i in 0..num_directories {
        let dir_padded_len = format!("{:0>width$}", i, width = dir_digits);
        let dir_path = format!("{}/{}", outdir_path.display(), dir_padded_len);
        create_dir(&dir_path).expect(&format!("Failed to create directory: {}", dir_path));

        (0..num_files).into_par_iter().for_each(|j| {
            let file_padded_len = format!("{:0>width$}", j, width = file_digits);
            let file_name: String;
            if disable_random {
                file_name = "abcdefghij".chars().cycle().take(rand_name_len).collect();
            } else {
                file_name = thread_rng()
                    .sample_iter(&Alphanumeric)
                    .take(rand_name_len)
                    .map(char::from)
                    .collect();
            }
            let file_path = format!("{}/{}_{}", dir_path, file_padded_len, file_name);
            let file_size = thread_rng().gen_range(min_size..=max_size);

            size_atomic.fetch_add(file_size, Ordering::SeqCst);

            let mut contents;
            if disable_random {
                contents = "0123456789"
                    .chars()
                    .cycle()
                    .take(file_size)
                    .map(|c| c as u8)
                    .collect();
            } else {
                contents = vec![0u8; file_size];
                thread_rng().fill(&mut contents[..]);
            }
            let mut file = File::create(&file_path).expect("Failed to create file");
            file.write_all(&contents).expect("Failed to write to file");

            if opt_verbose {
                info(&format!("Create ({} bytes): {}", file_size, file_path));
            } else {
                let old_count = count_atomic.fetch_add(1, Ordering::SeqCst);
                let count = old_count + 1;
                // let count = count_atomic.load(Ordering::SeqCst);

                if count % 10000 == 0 {
                    let percent: usize = 100 * count / total_num;
                    let elapsed = Instant::now().duration_since(start_time);
                    let sec = elapsed.as_secs_f64();
                    let average = count as f64 / sec;
                    progress(&format!(
                        "Progress: {}% {}/{}file {:.1}s {:.1}file/s\r",
                        percent, count, total_num, sec, average,
                    ));
                }
            }

            if sleep_sec > 0.0 {
                let millis = (sleep_sec * 1000.0) as u64;
                sleep(Duration::from_millis(millis));
            }
        });
    }

    if !opt_verbose {
        progress("\n");
    }
    let elapsed = Instant::now().duration_since(start_time);
    let sec = elapsed.as_secs_f64();
    let average = total_num as f64 / sec;
    let average_time_per_file = sec / total_num as f64;
    let total_size = size_atomic.load(Ordering::SeqCst);
    let average_io = total_size as f64 / sec;
    let average_io_mb = average_io / 1000000.0;

    print_msg(&format!(
        "Created files: {}dir * {} = {}files",
        num_directories, num_files, total_num,
    ));
    print_msg(&format!("Total time: {:.6}s", sec,));
    print_msg(&format!("Total size: {}B", total_size,));
    print_msg(&format!(
        "Creation speed: {:.3}file/s {:.6}s/file",
        average, average_time_per_file,
    ));
    print_msg(&format!(
        "I/O speed: {:.0}B/s {:.0}MB/s",
        average_io, average_io_mb,
    ));
}
