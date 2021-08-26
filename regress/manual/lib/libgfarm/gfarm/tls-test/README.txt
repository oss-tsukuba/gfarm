How to test:

% make
% sudo make env
% make run-test
% make run-test-interactive
	... requires user interaction and must login with a tty/pty
		 for passphrase input.

Running with valgrind:

Use following environmental variables:
	__RUN_VALGRIND__
	An enviromnment variable to determine run valgrind or not

If ./suppressed_funcs exists valgrind uses it as the supression file.

