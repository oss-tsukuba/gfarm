			Gfarm kernel module test scripts

o Setup
	- gfarm (Linux kernel module) is installed
			% ./confiure --enable-linuxkernel
			% make
			% su
			# make install
	- gfmd, backend DB (PostgreSQL/OpenLDAP) are running
	- Gfarm FS is empty (any files, directories are not created)

	- fix initparams.sh for some arguments (mountpoint etc)
		* gfarm, gfarm2 account must be setup correctly
		* These accounts must be in gfarmadm group
			(both of /etc/password and gfgroup)

o build file I/O test programs

	% cd gfarm_v2.6/regress
	% make -C linux/kernel/src

o Run directory and file tests

	- These test scripts must be run by root to use mount/sudo etc.

	% su
	# cd gfarm_v2.6/regress

	# sh -x ./linux/kernel/dironlytest.sh
	# sh -x ./linux/kernel/filetest.sh


o Run simple benchmark test

	- start gfmd, gfsd
	- mount GfarmFS

	- Write benchmark
	% linux/kernel/src/benchmark /mnt/gfarm benchfile 100

	- Read benchmark
	% linux/kernel/src/benchmark /mnt/gfarm benchfile 100 read

	This program write/read/mmap 100[MB] data to /mnt/gfarm/benchfile.
	You can see elapsed time and I/O bandwidth[MB/sec].
	To clear all caches, you must umount&mount before read test.

	You can use benchmark.sh also (after setting some parameters).
