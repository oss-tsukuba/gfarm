			gfxattr, gffindxmlattr test scripts

o Setup
	- gfarm, gfarm2fs, fuse are installed
	- "sudo modprobe fuse" executed
	- setup visudo to run "sudo umount" by test running user
	- gfmd, backend DB (PostgreSQL/OpenLDAP) are running
	- Gfarm FS is empty (any files, directories are not created)

o At XML attr supported gfmd and PostgreSQL environment

	cd gfarm/regress
	sh ./gftool/gfxattr/gfxattr-xml-enabled.sh

o At XML attr NOT supported gfmd and PostgreSQL/OpenLDAP environment

	cd gfarm/regress
	sh ./gftool/gfxattr/gfxattr-xml-disabled.sh
	

If test succeeded, you will see
	*** gfxattr test passed! ***

If failed, test exits at failed command.
run ./gftool/gfxattr/gfxattr-fini.sh
for finalize (remove some files etc). 


o gffindxmlattr-large-test.sh
	cd gfarm/regress/gfxattr
	sh ./gffindxmlattr-large-test.sh
