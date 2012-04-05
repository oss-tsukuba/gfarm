			Gfarm kernel module test scripts

o Setup
	- gfarm (Linux kernel module) is installed
			% ./confiure --enable-linuxkernel
			% make
			% su
			# make install
	- gfmd, backend DB (PostgreSQL/OpenLDAP) are running
	- Gfarm FS is empty (any files, directories are not created)

	- fix dironlytest-init.sh for some arguments (mountpoint etc)
		* gfarm, gfarm2 account must be setup correctly
		* These accounts must be in gfarmadm group
			(both of /etc/password and gfgroup)

o Run directory only test

	% su
	# cd gfarm_v2/regress
	# sh -x ./linux/kernel/dironlytest.sh
