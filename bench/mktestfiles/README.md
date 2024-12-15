# mktestfiles

Create a large number of files for testing or benchmarking purposes

## How to install

- Install Rust
- `make clean`
- `make test`
- `make install`
  - ~/.cargo/bin/mktestfiles is installed
- `make clean`
- `mktestfiles -h`

## Example

```bash
$ mktestfiles -o dir -n 100 -N 10

Total time (10dir * 100 = 1000files): 0.021214061s
Average: 47138.54645746517file/s 0.000021214061s/file
$ mktestfiles -o dir -n 100 -N 10
thread 'main' panicked at src/main.rs:128:29:
Failed to create directory: dir: Os { code: 17, kind: AlreadyExists, message: "File exists" }
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace
$ rm -rf dir
$ mktestfiles -o dir -n 100 -N 10

Total time (10dir * 100 = 1000files): 0.020954549s
Average: 47722.33465869392file/s 0.000020954548999999998s/file
```

## MEMO: Install Rust

- `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`
  - SEE ALSO: https://rustup.rs/
- `source "$HOME/.cargo/env"`
- `cargo version`

## MEMO: cargo new

- `cargo new mktestfiles`
- `cd mktestfiles`
- `ls -l Cargo.toml src/main.rs`
