# Part 1 data definition
Summary: Grid Datafarm
Name: gfarm
Version: 0.1
Release: 1
Source: gfarm-0.1.tar.gz
#Patch: 
Copyright: AIST inernal use only
Group: Local
Packager: Tohru Sotoyama <sotoyama@sra.co.jp>
BuildRoot: %{_tmppath}/%{name}-%{version}-buildroot

%package doc
Summary: document for gfarm
Group: Documentation

%package client
Summary: clients for gfarm
Group: Applications/Internet

%package server
Summary: servers for gfarm
Group: Applications/Internet

%package fsnode
Summary: gfsd for gfarm
Group: System Environment/Daemons

%package gfmd
Summary: gfmd for gfarm
Group: System Environment/Daemons

%package devel
Summary: development library for gfarm
Group: Development/Libraries


%description
gfarm - Grid datafarm 

%description doc
doc for gfarm

%description client
clients for gfarm

%description server
servers for gfarm

%description fsnode
fsnode for gfarm

%description gfmd
gfmd for gfarm

%description devel
development library for gfarm

%changelog
* Wed Nov 27 2002 Tohru Sotoyama <sotoyama@sra.co.jp>
- first release for version 0.1

# Part 2 script
%prep
rm -rf ${RPM_BUILD_ROOT}
mkdir -p $RPM_BUILD_ROOT

%setup
#%patch -p1

%build
./configure --prefix=/usr --with-openldap=/usr
make

%install
make prefix=${RPM_BUILD_ROOT}/usr \
	default_mandir=${RPM_BUILD_ROOT}/usr/share/man \
	example_bindir=${RPM_BUILD_ROOT}/usr/bin install 
mkdir -p ${RPM_BUILD_ROOT}/etc/rc.d/init.d
cp -p package/redhat/gfmd package/redhat/gfsd package/redhat/gfarm-slapd \
	${RPM_BUILD_ROOT}/etc/rc.d/init.d

%clean
rm -rf ${RPM_BUILD_ROOT}

%post fsnode
/sbin/chkconfig --add gfsd

%post gfmd
/sbin/chkconfig --add gfmd
/sbin/chkconfig --add gfarm-slapd

%preun fsnode
if [ "$1" = 0 ]
then
	/sbin/service gfsd stop > /dev/null 2>&1 || :
	/sbin/chkconfig --del gfsd
fi

%preun gfmd
if [ "$1" = 0 ]
then
	/sbin/service gfmd stop > /dev/null 2>&1 || :
	/sbin/service gfarm-slpad stop > /dev/null 2>&1 || :
	/sbin/chkconfig --del gfmd
	/sbin/chkconfig --del gfarm-slapd
fi

# Part 3  file list
%files doc
/usr/share/man/man1/digest.1.gz
/usr/share/man/man1/gfexport.1.gz
/usr/share/man/man1/gfgrep.1.gz
/usr/share/man/man1/gfimport_fixed.1.gz
/usr/share/man/man1/gfimport_text.1.gz
/usr/share/man/man1/gfkey.1.gz
/usr/share/man/man1/gfls.1.gz
/usr/share/man/man1/gfmpirun.1.gz
/usr/share/man/man1/gfps.1.gz
/usr/share/man/man1/gfrcmd.1.gz
/usr/share/man/man1/gfreg.1.gz
/usr/share/man/man1/gfrep.1.gz
/usr/share/man/man1/gfrm.1.gz
/usr/share/man/man1/gfrsh.1.gz
/usr/share/man/man1/gfrun.1.gz
/usr/share/man/man1/gfssh.1.gz
/usr/share/man/man1/gfwc.1.gz
/usr/share/man/man1/gfwhere.1.gz
/usr/share/man/man3/gfarm_initialize.3.gz
/usr/share/man/man3/gfarm_strings_free_deeply.3.gz
/usr/share/man/man3/gfarm_terminate.3.gz
/usr/share/man/man3/gfarm_url_hosts_schedule.3.gz
/usr/share/man/man3/gfs_chmod.3.gz
/usr/share/man/man3/gfs_chown.3.gz
/usr/share/man/man3/gfs_closedir.3.gz
/usr/share/man/man3/gfs_mkdir.3.gz
/usr/share/man/man3/gfs_opendir.3.gz
/usr/share/man/man3/gfs_pio_close.3.gz
/usr/share/man/man3/gfs_pio_create.3.gz
/usr/share/man/man3/gfs_pio_eof.3.gz
/usr/share/man/man3/gfs_pio_error.3.gz
/usr/share/man/man3/gfs_pio_flush.3.gz
/usr/share/man/man3/gfs_pio_getc.3.gz
/usr/share/man/man3/gfs_pio_getline.3.gz
/usr/share/man/man3/gfs_pio_open.3.gz
/usr/share/man/man3/gfs_pio_putc.3.gz
/usr/share/man/man3/gfs_pio_putline.3.gz
/usr/share/man/man3/gfs_pio_puts.3.gz
/usr/share/man/man3/gfs_pio_read.3.gz
/usr/share/man/man3/gfs_pio_seek.3.gz
/usr/share/man/man3/gfs_pio_set_local.3.gz
/usr/share/man/man3/gfs_pio_set_view_index.3.gz
/usr/share/man/man3/gfs_pio_set_view_local.3.gz
/usr/share/man/man3/gfs_pio_ungetc.3.gz
/usr/share/man/man3/gfs_pio_write.3.gz
/usr/share/man/man3/gfs_readdir.3.gz
/usr/share/man/man3/gfs_rename.3.gz
/usr/share/man/man3/gfs_rmdir.3.gz
/usr/share/man/man3/gfs_stat.3.gz
/usr/share/man/man3/gfs_stat_free.3.gz
/usr/share/man/man3/gfs_unlink.3.gz
/usr/share/man/man8/gfmd.8.gz
/usr/share/man/man8/gfsd.8.gz
/usr/share/man/ja/man1/digest.1.gz
/usr/share/man/ja/man1/gfexport.1.gz
/usr/share/man/ja/man1/gfgrep.1.gz
/usr/share/man/ja/man1/gfimport_fixed.1.gz
/usr/share/man/ja/man1/gfimport_text.1.gz
/usr/share/man/ja/man1/gfkey.1.gz
/usr/share/man/ja/man1/gfls.1.gz
/usr/share/man/ja/man1/gfmkdir.1.gz
/usr/share/man/ja/man1/gfmpirun.1.gz
/usr/share/man/ja/man1/gfps.1.gz
/usr/share/man/ja/man1/gfrcmd.1.gz
/usr/share/man/ja/man1/gfreg.1.gz
/usr/share/man/ja/man1/gfrep.1.gz
/usr/share/man/ja/man1/gfrm.1.gz
/usr/share/man/ja/man1/gfrmdir.1.gz
/usr/share/man/ja/man1/gfrsh.1.gz
/usr/share/man/ja/man1/gfrun.1.gz
/usr/share/man/ja/man1/gfssh.1.gz
/usr/share/man/ja/man1/gfwc.1.gz
/usr/share/man/ja/man1/gfwhere.1.gz
/usr/share/man/ja/man3/GFARM_STRINGLIST_ELEM.3.gz
/usr/share/man/ja/man3/GFARM_STRINGLIST_STRARRAY.3.gz
/usr/share/man/ja/man3/gfarm_hostlist_read.3.gz
/usr/share/man/ja/man3/gfarm_import_fragment_config_read.3.gz
/usr/share/man/ja/man3/gfarm_import_fragment_size_alloc.3.gz
/usr/share/man/ja/man3/gfarm_initialize.3.gz
/usr/share/man/ja/man3/gfarm_stringlist_add.3.gz
/usr/share/man/ja/man3/gfarm_stringlist_cat.3.gz
/usr/share/man/ja/man3/gfarm_stringlist_elem.3.gz
/usr/share/man/ja/man3/gfarm_stringlist_free.3.gz
/usr/share/man/ja/man3/gfarm_stringlist_free_deeply.3.gz
/usr/share/man/ja/man3/gfarm_stringlist_init.3.gz
/usr/share/man/ja/man3/gfarm_stringlist_length.3.gz
/usr/share/man/ja/man3/gfarm_strings_free_deeply.3.gz
/usr/share/man/ja/man3/gfarm_terminate.3.gz
/usr/share/man/ja/man3/gfarm_url_fragments_replicate.3.gz
/usr/share/man/ja/man3/gfarm_url_hosts_schedule.3.gz
/usr/share/man/ja/man3/gfarm_url_program_deliver.3.gz
/usr/share/man/ja/man3/gfarm_url_program_register.3.gz
/usr/share/man/ja/man3/gfarm_url_section_replicate_from_to.3.gz
/usr/share/man/ja/man3/gfarm_url_section_replicate_to.3.gz
/usr/share/man/ja/man3/gfs_chmod.3.gz
/usr/share/man/ja/man3/gfs_chown.3.gz
/usr/share/man/ja/man3/gfs_closedir.3.gz
/usr/share/man/ja/man3/gfs_mkdir.3.gz
/usr/share/man/ja/man3/gfs_opendir.3.gz
/usr/share/man/ja/man3/gfs_pio_close.3.gz
/usr/share/man/ja/man3/gfs_pio_create.3.gz
/usr/share/man/ja/man3/gfs_pio_eof.3.gz
/usr/share/man/ja/man3/gfs_rmdir.3.gz
/usr/share/man/ja/man3/gfs_pio_error.3.gz
/usr/share/man/ja/man3/gfs_pio_flush.3.gz
/usr/share/man/ja/man3/gfs_pio_getc.3.gz
/usr/share/man/ja/man3/gfs_pio_getline.3.gz
/usr/share/man/ja/man3/gfs_pio_open.3.gz
/usr/share/man/ja/man3/gfs_pio_putc.3.gz
/usr/share/man/ja/man3/gfs_pio_putline.3.gz
/usr/share/man/ja/man3/gfs_pio_puts.3.gz
/usr/share/man/ja/man3/gfs_pio_read.3.gz
/usr/share/man/ja/man3/gfs_pio_seek.3.gz
/usr/share/man/ja/man3/gfs_pio_set_local.3.gz
/usr/share/man/ja/man3/gfs_pio_set_view_index.3.gz
/usr/share/man/ja/man3/gfs_pio_set_view_local.3.gz
/usr/share/man/ja/man3/gfs_pio_ungetc.3.gz
/usr/share/man/ja/man3/gfs_pio_write.3.gz
/usr/share/man/ja/man3/gfs_readdir.3.gz
/usr/share/man/ja/man3/gfs_rename.3.gz
/usr/share/man/ja/man3/gfs_stat.3.gz
/usr/share/man/ja/man3/gfs_stat_free.3.gz
/usr/share/man/ja/man3/gfs_unlink.3.gz
/usr/share/man/ja/man8/gfmd.8.gz
/usr/share/man/ja/man8/gfsd.8.gz

%files client
/usr/bin/gfarm
/usr/bin/ns_put
/usr/bin/ns_get
/usr/bin/ns_stat
/usr/bin/ns_lstat
/usr/bin/ns_rename
/usr/bin/ns_unlink
/usr/bin/ns_unlink_dir
/usr/bin/ns_mkdir
/usr/bin/ns_symlink
/usr/bin/ns_readlink
/usr/bin/ns_readdir

%files server
/usr/bin/gfrsh
/usr/bin/gfarmd
/usr/bin/digest
/usr/bin/gfexport
/usr/bin/gfhost
/usr/bin/gfkey
/usr/bin/gfls
/usr/bin/gfrcmd
/usr/bin/gfrep
/usr/bin/gfrm
/usr/bin/gfsched
/usr/bin/gfmpirun
/usr/bin/gfps
/usr/bin/gfrun
/usr/bin/gfssh
/usr/bin/gfrshl
/usr/bin/gfsshl
/usr/bin/gfimport_fixed
/usr/bin/gfimport_text
/usr/bin/gfgrep
/usr/bin/gfwhere

%files fsnode
/usr/bin/gfsd
/usr/bin/gfreg
/etc/rc.d/init.d/gfsd

#%config /etc/gfarm.conf

%files gfmd
/usr/bin/gfmd
/etc/rc.d/init.d/gfmd
/etc/rc.d/init.d/gfarm-slapd

%files devel
/usr/include/gfarm/comm.h
/usr/include/gfarm/con.h
/usr/include/gfarm/debug.h
/usr/include/gfarm/gfarm.h
/usr/include/gfarm/gfarm_config.h
/usr/include/gfarm/gfarm_error.h
/usr/include/gfarm/gfarm_metadb.h
/usr/include/gfarm/gfarm_misc.h
/usr/include/gfarm/gflib.h
/usr/include/gfarm/gfs.h
/usr/include/gfarm/gfs_hook.h
/usr/include/gfarm/ns.h
/usr/include/gfarm/nsclnt.h
/usr/include/gfarm/nscom.h
/usr/include/gfarm/nsexec.h
/usr/include/gfarm/soc-lxdr.h
/usr/include/gfarm/soc.h
/usr/include/gfarm/type.h
/usr/lib/libns.a
/usr/lib/libnsexec.a
/usr/lib/gfs_hook.o
/usr/lib/gfs_hook_debug.o
/usr/lib/libgfarm.a
