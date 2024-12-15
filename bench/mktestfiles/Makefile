test: build clean-testdir
	./target/release/mktestfiles
	./target/release/mktestfiles -Of
	$(MAKE) clean-testdir

test-many: build clean-testdir
	./target/release/mktestfiles --num-files 1000 --num-directories 1000 > /dev/null
	$(MAKE) clean-testdir

test-many-para: build clean-testdir
	./target/release/mktestfiles --num-files 1000 --num-directories 1000 --parallel > /dev/null
	$(MAKE) clean-testdir

build: fix
	cargo build --release

fix: fmt
	cargo fix --allow-dirty --allow-staged

fmt:
	cargo fmt

install:
	cargo install --locked --path .

install-root:
	sudo cargo install --locked --path . --root /usr/local

update:
	cargo update

clean: clean-testdir clean-target

clean-testdir:
	rm -rf ./testdir

clean-target:
	rm -rf ./target
