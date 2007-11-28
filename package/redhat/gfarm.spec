# Part 1 data definition
%define pkg	gfarm
%define ver	2.0.0
%define rel	1

# a hook to make RPM version number different from %{ver}
%define pkgver	%{ver}

%define prefix		%{_prefix}
%define lib_prefix	%{_libdir}
%define man_prefix	%{_mandir}
%define share_prefix	%{prefix}/share/%{pkg}
%define doc_prefix	%{prefix}/share/doc/%{name}-%{ver}
%define html_prefix	%{doc_prefix}/html
%define etc_prefix	/etc
%define rc_prefix	%{etc_prefix}/rc.d/init.d
%define profile_prefix	%{etc_prefix}/profile.d
%define sysconfdir	%{etc_prefix}

# whether "ns" is included in this release or not.
%define have_ns	0

%define gfarm_v2_not_yet 0

#
# check && enable/disable MPI
#
# do the followings to make gfwc
#   # env MPI_PREFIX=/usr/local/mpich rpmbuild -bb gfarm.spec

%define	mpi_prefix	%(echo "${MPI_PREFIX}")

%define mpi	%(test -x %{mpi_prefix}/bin/mpicc && echo 1 || echo 0)

#
# check && enable/disable Globus
#
# do the followings to build gfarm-gsi-*.rpm:
#   # env GLOBUS_PREFIX=/usr/grid GLOBUS_FLAVOR=gcc32 rpmbuild -bb gfarm.spec
# or
#   # env GLOBUS_PREFIX=/usr/grid GLOBUS_FLAVOR=gcc32 \
#	GFARM_CONFIGURE_OPTION=--with-globus-static rpmbuild -bb gfarm.spec

%define	globus_prefix	%(echo "${GLOBUS_PREFIX}")
%define	globus_flavor	%(echo "${GLOBUS_FLAVOR}")

%define globus %(test -n "${GLOBUS_PREFIX}" -a -n "${GLOBUS_FLAVOR}" -a -r %{globus_prefix}/include/%{globus_flavor}/gssapi.h && echo 1 || echo 0)

%if %{globus}
%define package_name	%{pkg}-gsi
%else
%define package_name	%{pkg}
%endif

Summary: Gfarm File System 2 
Name: %{package_name}
Version: %pkgver
Release: %{rel}%{?dist}
Source: %{pkg}-%{ver}.tar.gz
#Patch: %{pkg}.patch
#%Patch0: gfarm-1.2-patch1.diff
#%Patch1: gfarm-1.2-patch2.diff
#%Patch2: gfarm-1.2-patch3.diff
#%Patch3: gfarm-1.2-patch4.diff
Group: Applications/Internet
License: BSD
Vendor: National Institute of Advanced Industrial Science and Technology (AIST) and Osamu Tatebe
URL: http://datafarm.apgrid.org/
Packager: Osamu Tatebe <tatebe@cs.tsukuba.ac.jp>
BuildRoot: %{_tmppath}/%{name}-%{version}-buildroot

%package doc
Summary: Document for Gfarm file system
Group: Documentation
# always provide "gfarm-doc" as a virtual package.
%if %{globus}
Provides: %{pkg}-doc = %{ver}-%{rel}
%endif

%package libs
Summary: Runtime libraries for Gfarm file system
Group: System Environment/Libraries
# always provide "gfarm-libs" as a virtual package.
%if %{globus}
Provides: %{pkg}-libs = %{ver}-%{rel}
%endif
BuildRequires: openssl-devel

%package client
Summary: Clients for Gfarm file system
Group: Applications/Internet
# always provide "gfarm-client" as a virtual package.
%if %{globus}
Provides: %{pkg}-client = %{ver}-%{rel}
%endif
Requires: %{package_name}-libs = %{ver}

%if %{gfarm_v2_not_yet}
%package gfptool
Summary: Parallel tools for Gfarm file system
Group: Applications/Internet
# always provide "gfarm-gfptool" as a virtual package.
%if %{globus}
Provides: %{pkg}-gfptool = %{ver}-%{rel}
%endif
Requires: %{package_name}-libs = %{ver}, %{package_name}-client = %{ver}
%endif

%package fsnode
Summary: File system daemon for Gfarm file system
Group: System Environment/Daemons
# always provide "gfarm-fsnode" as a virtual package.
%if %{globus}
Provides: %{pkg}-fsnode = %{ver}-%{rel}
%endif
Requires: %{package_name}-libs = %{ver}, %{package_name}-client = %{ver}

%package server
Summary: Metadata server for Gfarm file system
Group: System Environment/Daemons
# always provide "gfarm-server" as a virtual package.
%if %{globus}
Provides: %{pkg}-server = %{ver}-%{rel}
%endif
Requires: %{package_name}-libs = %{ver}

%package devel
Summary: Development header files and libraries for Gfarm file system
Group: Development/Libraries
%if %{globus}
Provides: %{pkg}-devel = %{ver}-%{rel}
%endif

%if %{gfarm_v2_not_yet}
%package gfront
Summary: File system browser for Gfarm file system
Group: Applications/Internet
# always provide "gfarm-gfront" as a virtual package.
%if %{globus}
Provides: %{pkg}-gfront = %{ver}-%{rel}
%endif
%endif

%description
The Gfarm filesystem is a distributed filesystem consisting of the
local storage of commodity PCs.  PCs in a local area network, compute
nodes in a single cluster, multiple clusters in wide area, comprise a
large-scale, high-performance shared network filesystem.  The Gfarm
filesystem solves performance and reliability problems in NFS and AFS
by means of multiple file replicas. It not only prevents performance
degradation due to access concentration, but also supports fault
tolerance and disaster recovery.

%description doc
Documentation for Gfarm file system

%description libs
Runtime libraries for Gfarm file system

%description client
Clients for Gfarm file system

%if %{gfarm_v2_not_yet}
%description gfptool
parallel tools installed under gfarm:/bin
%endif

%description fsnode
File system daemon for Gfarm file system

%description server
Metadata server for Gfarm file system

%if %{gfarm_v2_not_yet}
%description agent
Metadata cache server for Gfarm file system
%endif

%description devel
Development header files and libraries for Gfarm file system

%if %{gfarm_v2_not_yet}
%description gfront
file system browser for gfarm
%endif

%changelog
* Fri Nov 28 2007 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.0.0-1
- first release of Gfarm file system version 2

* Tue Aug  8 2006 SODA Noriyuki <soda@sra.co.jp>
- restart gfsd, gfmd and gfarm_agent at update, if they are already running.

* Fri Apr 24 2003 Tohru Sotoyama <sotoyama@sra.co.jp>
- first public release for version 1.0b1

* Wed Nov 27 2002 Tohru Sotoyama <sotoyama@sra.co.jp>
- first release for version 0.1

# Part 2 script
%prep
rm -rf ${RPM_BUILD_ROOT}
mkdir -p $RPM_BUILD_ROOT

%setup -n %{pkg}-%{ver}
#%patch -p1
#%patch0 -p1
#%patch1 -p1
#%patch2 -p1
#%patch3 -p1

%build
./configure --prefix=%{prefix} \
	--libdir=%{lib_prefix} \
	--sysconfdir=%{sysconfdir} \
	--with-postgresql=/usr \
	--with-openldap=/usr \
	--with-openssl=/usr \
	`test "%{globus}" -ne 0 && echo	\
		--with-globus=%{globus_prefix} \
		--with-globus-flavor=%{globus_flavor}` \
	`test "%{mpi}" -ne 0 && echo --with-mpi=%{mpi_prefix}` \
	${GFARM_CONFIGURE_OPTION}

make

%install
make DESTDIR=${RPM_BUILD_ROOT} \
	default_docdir=%{doc_prefix} \
	default_mandir=%{man_prefix} install
%if %{gfarm_v2_not_yet}
mkdir -p ${RPM_BUILD_ROOT}%{profile_prefix}
cp -p package/redhat/gfarm.{csh,sh} ${RPM_BUILD_ROOT}%{profile_prefix}
chmod +x ${RPM_BUILD_ROOT}%{profile_prefix}/*
%endif

%clean
rm -rf ${RPM_BUILD_ROOT}

%post libs -p /sbin/ldconfig

%postun libs -p /sbin/ldconfig

%post fsnode
if [ ! -f /etc/gfarm2.conf ]; then
	echo "copy /etc/gfarm2.conf from a metadata server and"
	echo run %{prefix}/bin/config-gfsd
fi

%post server
if [ ! -f /etc/gfmd.conf ]; then
	echo run %{prefix}/bin/config-gfarm to configure Gfarm file system
fi

%preun fsnode
if [ "$1" = 0 ]
then
	# XXX This doesn't deal with /etc/init.d/gfsd-IP_ADDRESS.
	if [ -f /etc/init.d/gfsd ]; then
		/sbin/service gfsd stop > /dev/null 2>&1 || :
		/sbin/chkconfig --del gfsd > /dev/null 2>&1 || :
	fi
fi

%pre fsnode
useradd -c "Gfarm gfsd" _gfarmfs >/dev/null 2>&1 || :

%pre server
useradd -c "Gfarm gfsd" _gfarmfs >/dev/null 2>&1 || :

%preun server
if [ "$1" = 0 ]
then
	if [ -f /etc/init.d/gfmd ]; then
		/sbin/service gfmd stop > /dev/null 2>&1 || :
		/sbin/chkconfig --del gfmd > /dev/null 2>&1 || :
	fi
	if [ -f /etc/init.d/gfarm-slapd ]; then
		/sbin/service gfarm-slapd stop > /dev/null 2>&1 || :
		/sbin/chkconfig --del gfarm-slapd > /dev/null 2>&1 || :
	fi
	if [ -f /etc/init.d/gfarm-pgsql ]; then
		/sbin/service gfarm-pgsql stop > /dev/null 2>&1 || :
		/sbin/chkconfig --del gfarm-pgsql > /dev/null 2>&1 || :
	fi
fi

%postun fsnode
if [ "$1" -ge 1 ]
then
	# XXX This doesn't deal with /etc/init.d/gfsd-IP_ADDRESS.
	if [ -f /etc/init.d/gfsd ] && /sbin/service gfsd status > /dev/null 2>&1
	then
		/sbin/service gfsd restart > /dev/null 2>&1 || :
	fi
fi

%postun server
if [ "$1" -ge 1 ]
then
	if [ -f /etc/init.d/gfmd ] && /sbin/service gfmd status > /dev/null 2>&1
	then
		/sbin/service gfmd restart > /dev/null 2>&1 || :
	fi
	# We don't have to restart gfarm-slapd and gfarm-pgsql,
	# because the binaries aren't included in the gfarm RPMs.
fi

# Part 3  file list
%files doc
%defattr(-,root,root)
%if %{gfarm_v2_not_yet}
%{man_prefix}/man1/gfarm_agent.1.gz
%{man_prefix}/man1/gfcd.1.gz
%{man_prefix}/man1/gfdf.1.gz
%{man_prefix}/man1/gfexec.1.gz
%{man_prefix}/man1/gfexport.1.gz
%{man_prefix}/man1/gfgrep.1.gz
%{man_prefix}/man1/gfhost.1.gz
%{man_prefix}/man1/gfimport_fixed.1.gz
%{man_prefix}/man1/gfimport_text.1.gz
%{man_prefix}/man1/gfkey.1.gz
%{man_prefix}/man1/gfls.1.gz
%{man_prefix}/man1/gfmkdir.1.gz
%{man_prefix}/man1/gfmpirun_p4.1.gz
%{man_prefix}/man1/gfps.1.gz
%{man_prefix}/man1/gfpwd.1.gz
%{man_prefix}/man1/gfrcmd.1.gz
%{man_prefix}/man1/gfreg.1.gz
%{man_prefix}/man1/gfrep.1.gz
%{man_prefix}/man1/gfrm.1.gz
%{man_prefix}/man1/gfrmdir.1.gz
%{man_prefix}/man1/gfront.1.gz
%{man_prefix}/man1/gfrsh.1.gz
%{man_prefix}/man1/gfrun.1.gz
%{man_prefix}/man1/gfsched.1.gz
%{man_prefix}/man1/gfsetdir.1.gz
%{man_prefix}/man1/gfssh.1.gz
%{man_prefix}/man1/gfstat.1.gz
%{man_prefix}/man1/gfusage.1.gz
%{man_prefix}/man1/gfwc.1.gz
%{man_prefix}/man1/gfwhere.1.gz
%{man_prefix}/man1/gfwhoami.1.gz
%{man_prefix}/man3/gfarm.3.gz
%{man_prefix}/man3/gfarm_initialize.3.gz
%{man_prefix}/man3/gfarm_strings_free_deeply.3.gz
%{man_prefix}/man3/gfarm_terminate.3.gz
%{man_prefix}/man3/gfarm_url_fragments_replicate.3.gz
%{man_prefix}/man3/gfarm_url_hosts_schedule.3.gz
%{man_prefix}/man3/gfarm_url_section_replicate_from_to.3.gz
%{man_prefix}/man3/gfarm_url_section_replicate_to.3.gz
%{man_prefix}/man3/gfs_chdir.3.gz
%{man_prefix}/man3/gfs_chmod.3.gz
%{man_prefix}/man3/gfs_closedir.3.gz
%{man_prefix}/man3/gfs_glob.3.gz
%{man_prefix}/man3/gfs_glob_add.3.gz
%{man_prefix}/man3/gfs_glob_free.3.gz
%{man_prefix}/man3/gfs_glob_init.3.gz
%{man_prefix}/man3/gfs_mkdir.3.gz
%{man_prefix}/man3/gfs_opendir.3.gz
%{man_prefix}/man3/gfs_pio_close.3.gz
%{man_prefix}/man3/gfs_pio_create.3.gz
%{man_prefix}/man3/gfs_pio_datasync.3.gz
%{man_prefix}/man3/gfs_pio_eof.3.gz
%{man_prefix}/man3/gfs_pio_error.3.gz
%{man_prefix}/man3/gfs_pio_flush.3.gz
%{man_prefix}/man3/gfs_pio_getc.3.gz
%{man_prefix}/man3/gfs_pio_getline.3.gz
%{man_prefix}/man3/gfs_pio_gets.3.gz
%{man_prefix}/man3/gfs_pio_open.3.gz
%{man_prefix}/man3/gfs_pio_putc.3.gz
%{man_prefix}/man3/gfs_pio_putline.3.gz
%{man_prefix}/man3/gfs_pio_puts.3.gz
%{man_prefix}/man3/gfs_pio_read.3.gz
%{man_prefix}/man3/gfs_pio_readdelim.3.gz
%{man_prefix}/man3/gfs_pio_readline.3.gz
%{man_prefix}/man3/gfs_pio_seek.3.gz
%{man_prefix}/man3/gfs_pio_set_local.3.gz
%{man_prefix}/man3/gfs_pio_set_view_index.3.gz
%{man_prefix}/man3/gfs_pio_set_view_local.3.gz
%{man_prefix}/man3/gfs_pio_sync.3.gz
%{man_prefix}/man3/gfs_pio_truncate.3.gz
%{man_prefix}/man3/gfs_pio_ungetc.3.gz
%{man_prefix}/man3/gfs_pio_write.3.gz
%{man_prefix}/man3/gfs_readdir.3.gz
%{man_prefix}/man3/gfs_realpath.3.gz
%{man_prefix}/man3/gfs_rename.3.gz
%{man_prefix}/man3/gfs_rmdir.3.gz
%{man_prefix}/man3/gfs_stat.3.gz
%{man_prefix}/man3/gfs_stat_free.3.gz
%{man_prefix}/man3/gfs_unlink.3.gz
%{man_prefix}/man3/gfs_unlink_section.3.gz
%{man_prefix}/man3/gfs_utimes.3.gz
%{man_prefix}/man5/gfarm.conf.5.gz
%{man_prefix}/man8/gfmd.8.gz
%{man_prefix}/man8/gfsd.8.gz
%{man_prefix}/ja/man1/gfarm_agent.1.gz
%{man_prefix}/ja/man1/gfcd.1.gz
%{man_prefix}/ja/man1/gfdf.1.gz
%{man_prefix}/ja/man1/gfexec.1.gz
%{man_prefix}/ja/man1/gfexport.1.gz
%{man_prefix}/ja/man1/gfgrep.1.gz
%{man_prefix}/ja/man1/gfhost.1.gz
%{man_prefix}/ja/man1/gfimport_fixed.1.gz
%{man_prefix}/ja/man1/gfimport_text.1.gz
%{man_prefix}/ja/man1/gfkey.1.gz
%{man_prefix}/ja/man1/gfls.1.gz
%{man_prefix}/ja/man1/gfmkdir.1.gz
%{man_prefix}/ja/man1/gfmpirun_p4.1.gz
%{man_prefix}/ja/man1/gfps.1.gz
%{man_prefix}/ja/man1/gfpwd.1.gz
%{man_prefix}/ja/man1/gfrcmd.1.gz
%{man_prefix}/ja/man1/gfreg.1.gz
%{man_prefix}/ja/man1/gfrep.1.gz
%{man_prefix}/ja/man1/gfrm.1.gz
%{man_prefix}/ja/man1/gfrmdir.1.gz
%{man_prefix}/ja/man1/gfront.1.gz
%{man_prefix}/ja/man1/gfrsh.1.gz
%{man_prefix}/ja/man1/gfrun.1.gz
%{man_prefix}/ja/man1/gfsched.1.gz
%{man_prefix}/ja/man1/gfsetdir.1.gz
%{man_prefix}/ja/man1/gfssh.1.gz
%{man_prefix}/ja/man1/gfstat.1.gz
%{man_prefix}/ja/man1/gfusage.1.gz
%{man_prefix}/ja/man1/gfwc.1.gz
%{man_prefix}/ja/man1/gfwhere.1.gz
%{man_prefix}/ja/man1/gfwhoami.1.gz
%{man_prefix}/ja/man3/gfarm.3.gz
%{man_prefix}/ja/man3/gfarm_hostlist_read.3.gz
%{man_prefix}/ja/man3/gfarm_import_fragment_config_read.3.gz
%{man_prefix}/ja/man3/gfarm_import_fragment_size_alloc.3.gz
%{man_prefix}/ja/man3/gfarm_initialize.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_add.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_cat.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_elem.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_free.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_init.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_length.3.gz
%{man_prefix}/ja/man3/gfarm_strings_free_deeply.3.gz
%{man_prefix}/ja/man3/gfarm_terminate.3.gz
%{man_prefix}/ja/man3/gfarm_url_fragments_replicate.3.gz
%{man_prefix}/ja/man3/gfarm_url_hosts_schedule.3.gz
%{man_prefix}/ja/man3/gfarm_url_program_deliver.3.gz
%{man_prefix}/ja/man3/gfarm_url_program_register.3.gz
%{man_prefix}/ja/man3/gfarm_url_section_replicate_from_to.3.gz
%{man_prefix}/ja/man3/gfarm_url_section_replicate_to.3.gz
%{man_prefix}/ja/man3/gfs_chdir.3.gz
%{man_prefix}/ja/man3/gfs_chmod.3.gz
%{man_prefix}/ja/man3/gfs_closedir.3.gz
%{man_prefix}/ja/man3/gfs_glob.3.gz
%{man_prefix}/ja/man3/gfs_glob_add.3.gz
%{man_prefix}/ja/man3/gfs_glob_free.3.gz
%{man_prefix}/ja/man3/gfs_glob_init.3.gz
%{man_prefix}/ja/man3/gfs_mkdir.3.gz
%{man_prefix}/ja/man3/gfs_opendir.3.gz
%{man_prefix}/ja/man3/gfs_pio_close.3.gz
%{man_prefix}/ja/man3/gfs_pio_create.3.gz
%{man_prefix}/ja/man3/gfs_pio_datasync.3.gz
%{man_prefix}/ja/man3/gfs_pio_eof.3.gz
%{man_prefix}/ja/man3/gfs_pio_error.3.gz
%{man_prefix}/ja/man3/gfs_pio_flush.3.gz
%{man_prefix}/ja/man3/gfs_pio_getc.3.gz
%{man_prefix}/ja/man3/gfs_pio_getline.3.gz
%{man_prefix}/ja/man3/gfs_pio_gets.3.gz
%{man_prefix}/ja/man3/gfs_pio_open.3.gz
%{man_prefix}/ja/man3/gfs_pio_putc.3.gz
%{man_prefix}/ja/man3/gfs_pio_putline.3.gz
%{man_prefix}/ja/man3/gfs_pio_puts.3.gz
%{man_prefix}/ja/man3/gfs_pio_read.3.gz
%{man_prefix}/ja/man3/gfs_pio_readdelim.3.gz
%{man_prefix}/ja/man3/gfs_pio_readline.3.gz
%{man_prefix}/ja/man3/gfs_pio_seek.3.gz
%{man_prefix}/ja/man3/gfs_pio_set_local.3.gz
%{man_prefix}/ja/man3/gfs_pio_set_view_index.3.gz
%{man_prefix}/ja/man3/gfs_pio_set_view_local.3.gz
%{man_prefix}/ja/man3/gfs_pio_sync.3.gz
%{man_prefix}/ja/man3/gfs_pio_truncate.3.gz
%{man_prefix}/ja/man3/gfs_pio_ungetc.3.gz
%{man_prefix}/ja/man3/gfs_pio_write.3.gz
%{man_prefix}/ja/man3/gfs_readdir.3.gz
%{man_prefix}/ja/man3/gfs_realpath.3.gz
%{man_prefix}/ja/man3/gfs_rename.3.gz
%{man_prefix}/ja/man3/gfs_rmdir.3.gz
%{man_prefix}/ja/man3/gfs_stat.3.gz
%{man_prefix}/ja/man3/gfs_stat_free.3.gz
%{man_prefix}/ja/man3/gfs_unlink.3.gz
%{man_prefix}/ja/man3/gfs_unlink_section.3.gz
%{man_prefix}/ja/man3/gfs_utimes.3.gz
%{man_prefix}/ja/man5/gfarm.conf.5.gz
%{man_prefix}/ja/man8/gfmd.8.gz
%{man_prefix}/ja/man8/gfsd.8.gz
%{html_prefix}/index.html
%{html_prefix}/en/ref/index.html
%{html_prefix}/en/ref/man1/gfarm_agent.1.html
%{html_prefix}/en/ref/man1/gfcd.1.html
%{html_prefix}/en/ref/man1/gfdf.1.html
%{html_prefix}/en/ref/man1/gfexec.1.html
%{html_prefix}/en/ref/man1/gfexport.1.html
%{html_prefix}/en/ref/man1/gfgrep.1.html
%{html_prefix}/en/ref/man1/gfhost.1.html
%{html_prefix}/en/ref/man1/gfimport_fixed.1.html
%{html_prefix}/en/ref/man1/gfimport_text.1.html
%{html_prefix}/en/ref/man1/gfkey.1.html
%{html_prefix}/en/ref/man1/gfls.1.html
%{html_prefix}/en/ref/man1/gfmkdir.1.html
%{html_prefix}/en/ref/man1/gfmpirun_p4.1.html
%{html_prefix}/en/ref/man1/gfps.1.html
%{html_prefix}/en/ref/man1/gfpwd.1.html
%{html_prefix}/en/ref/man1/gfrcmd.1.html
%{html_prefix}/en/ref/man1/gfreg.1.html
%{html_prefix}/en/ref/man1/gfrep.1.html
%{html_prefix}/en/ref/man1/gfrm.1.html
%{html_prefix}/en/ref/man1/gfrmdir.1.html
%{html_prefix}/en/ref/man1/gfront.1.html
%{html_prefix}/en/ref/man1/gfrun.1.html
%{html_prefix}/en/ref/man1/gfsched.1.html
%{html_prefix}/en/ref/man1/gfsetdir.1.html
%{html_prefix}/en/ref/man1/gfstat.1.html
%{html_prefix}/en/ref/man1/gfusage.1.html
%{html_prefix}/en/ref/man1/gfwc.1.html
%{html_prefix}/en/ref/man1/gfwhere.1.html
%{html_prefix}/en/ref/man1/gfwhoami.1.html
%{html_prefix}/en/ref/man3/gfarm.3.html
%{html_prefix}/en/ref/man3/gfarm_initialize.3.html
%{html_prefix}/en/ref/man3/gfarm_strings_free_deeply.3.html
%{html_prefix}/en/ref/man3/gfarm_terminate.3.html
%{html_prefix}/en/ref/man3/gfarm_url_fragments_replicate.3.html
%{html_prefix}/en/ref/man3/gfarm_url_hosts_schedule.3.html
%{html_prefix}/en/ref/man3/gfarm_url_section_replicate_from_to.3.html
%{html_prefix}/en/ref/man3/gfarm_url_section_replicate_to.3.html
%{html_prefix}/en/ref/man3/gfs_chdir.3.html
%{html_prefix}/en/ref/man3/gfs_chmod.3.html
%{html_prefix}/en/ref/man3/gfs_closedir.3.html
%{html_prefix}/en/ref/man3/gfs_glob.3.html
%{html_prefix}/en/ref/man3/gfs_glob_add.3.html
%{html_prefix}/en/ref/man3/gfs_glob_free.3.html
%{html_prefix}/en/ref/man3/gfs_glob_init.3.html
%{html_prefix}/en/ref/man3/gfs_mkdir.3.html
%{html_prefix}/en/ref/man3/gfs_opendir.3.html
%{html_prefix}/en/ref/man3/gfs_pio_close.3.html
%{html_prefix}/en/ref/man3/gfs_pio_create.3.html
%{html_prefix}/en/ref/man3/gfs_pio_datasync.3.html
%{html_prefix}/en/ref/man3/gfs_pio_eof.3.html
%{html_prefix}/en/ref/man3/gfs_pio_error.3.html
%{html_prefix}/en/ref/man3/gfs_pio_flush.3.html
%{html_prefix}/en/ref/man3/gfs_pio_getc.3.html
%{html_prefix}/en/ref/man3/gfs_pio_getline.3.html
%{html_prefix}/en/ref/man3/gfs_pio_gets.3.html
%{html_prefix}/en/ref/man3/gfs_pio_open.3.html
%{html_prefix}/en/ref/man3/gfs_pio_putc.3.html
%{html_prefix}/en/ref/man3/gfs_pio_putline.3.html
%{html_prefix}/en/ref/man3/gfs_pio_puts.3.html
%{html_prefix}/en/ref/man3/gfs_pio_read.3.html
%{html_prefix}/en/ref/man3/gfs_pio_readdelim.3.html
%{html_prefix}/en/ref/man3/gfs_pio_readline.3.html
%{html_prefix}/en/ref/man3/gfs_pio_seek.3.html
%{html_prefix}/en/ref/man3/gfs_pio_set_local.3.html
%{html_prefix}/en/ref/man3/gfs_pio_set_view_index.3.html
%{html_prefix}/en/ref/man3/gfs_pio_set_view_local.3.html
%{html_prefix}/en/ref/man3/gfs_pio_sync.3.html
%{html_prefix}/en/ref/man3/gfs_pio_truncate.3.html
%{html_prefix}/en/ref/man3/gfs_pio_ungetc.3.html
%{html_prefix}/en/ref/man3/gfs_pio_write.3.html
%{html_prefix}/en/ref/man3/gfs_readdir.3.html
%{html_prefix}/en/ref/man3/gfs_realpath.3.html
%{html_prefix}/en/ref/man3/gfs_rename.3.html
%{html_prefix}/en/ref/man3/gfs_rmdir.3.html
%{html_prefix}/en/ref/man3/gfs_stat.3.html
%{html_prefix}/en/ref/man3/gfs_stat_free.3.html
%{html_prefix}/en/ref/man3/gfs_unlink.3.html
%{html_prefix}/en/ref/man3/gfs_unlink_section.3.html
%{html_prefix}/en/ref/man3/gfs_utimes.3.html
%{html_prefix}/en/ref/man5/gfarm.conf.5.html
%{html_prefix}/en/ref/man8/gfmd.8.html
%{html_prefix}/en/ref/man8/gfsd.8.html
%{html_prefix}/en/user/index.html
%{html_prefix}/en/user/nfs-gfarmfs.html
%{html_prefix}/en/user/samba-gfarmfs.html
# XXX NOTYET %{html_prefix}/en/user/samba-hook.html
%{html_prefix}/ja/ref/index.html
%{html_prefix}/ja/ref/man1/gfarm_agent.1.html
%{html_prefix}/ja/ref/man1/gfcd.1.html
%{html_prefix}/ja/ref/man1/gfdf.1.html
%{html_prefix}/ja/ref/man1/gfexec.1.html
%{html_prefix}/ja/ref/man1/gfexport.1.html
%{html_prefix}/ja/ref/man1/gfgrep.1.html
%{html_prefix}/ja/ref/man1/gfhost.1.html
%{html_prefix}/ja/ref/man1/gfimport_fixed.1.html
%{html_prefix}/ja/ref/man1/gfimport_text.1.html
%{html_prefix}/ja/ref/man1/gfkey.1.html
%{html_prefix}/ja/ref/man1/gfls.1.html
%{html_prefix}/ja/ref/man1/gfmkdir.1.html
%{html_prefix}/ja/ref/man1/gfmpirun_p4.1.html
%{html_prefix}/ja/ref/man1/gfps.1.html
%{html_prefix}/ja/ref/man1/gfpwd.1.html
%{html_prefix}/ja/ref/man1/gfrcmd.1.html
%{html_prefix}/ja/ref/man1/gfreg.1.html
%{html_prefix}/ja/ref/man1/gfrep.1.html
%{html_prefix}/ja/ref/man1/gfrm.1.html
%{html_prefix}/ja/ref/man1/gfrmdir.1.html
%{html_prefix}/ja/ref/man1/gfront.1.html
%{html_prefix}/ja/ref/man1/gfrun.1.html
%{html_prefix}/ja/ref/man1/gfsched.1.html
%{html_prefix}/ja/ref/man1/gfsetdir.1.html
%{html_prefix}/ja/ref/man1/gfstat.1.html
%{html_prefix}/ja/ref/man1/gfusage.1.html
%{html_prefix}/ja/ref/man1/gfwc.1.html
%{html_prefix}/ja/ref/man1/gfwhere.1.html
%{html_prefix}/ja/ref/man1/gfwhoami.1.html
%{html_prefix}/ja/ref/man3/gfarm.3.html
%{html_prefix}/ja/ref/man3/gfarm_hostlist_read.3.html
%{html_prefix}/ja/ref/man3/gfarm_import_fragment_config_read.3.html
%{html_prefix}/ja/ref/man3/gfarm_import_fragment_size_alloc.3.html
%{html_prefix}/ja/ref/man3/gfarm_initialize.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_add.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_cat.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_elem.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_free.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_init.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_length.3.html
%{html_prefix}/ja/ref/man3/gfarm_strings_free_deeply.3.html
%{html_prefix}/ja/ref/man3/gfarm_terminate.3.html
%{html_prefix}/ja/ref/man3/gfarm_url_fragments_replicate.3.html
%{html_prefix}/ja/ref/man3/gfarm_url_hosts_schedule.3.html
%{html_prefix}/ja/ref/man3/gfarm_url_program_deliver.3.html
%{html_prefix}/ja/ref/man3/gfarm_url_program_register.3.html
%{html_prefix}/ja/ref/man3/gfarm_url_section_replicate_from_to.3.html
%{html_prefix}/ja/ref/man3/gfarm_url_section_replicate_to.3.html
%{html_prefix}/ja/ref/man3/gfs_chdir.3.html
%{html_prefix}/ja/ref/man3/gfs_chmod.3.html
%{html_prefix}/ja/ref/man3/gfs_closedir.3.html
%{html_prefix}/ja/ref/man3/gfs_glob.3.html
%{html_prefix}/ja/ref/man3/gfs_glob_add.3.html
%{html_prefix}/ja/ref/man3/gfs_glob_free.3.html
%{html_prefix}/ja/ref/man3/gfs_glob_init.3.html
%{html_prefix}/ja/ref/man3/gfs_mkdir.3.html
%{html_prefix}/ja/ref/man3/gfs_opendir.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_close.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_create.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_datasync.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_eof.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_error.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_flush.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_getc.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_getline.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_gets.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_open.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_putc.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_putline.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_puts.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_read.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_readdelim.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_readline.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_seek.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_set_local.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_set_view_index.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_set_view_local.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_sync.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_truncate.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_ungetc.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_write.3.html
%{html_prefix}/ja/ref/man3/gfs_readdir.3.html
%{html_prefix}/ja/ref/man3/gfs_realpath.3.html
%{html_prefix}/ja/ref/man3/gfs_rename.3.html
%{html_prefix}/ja/ref/man3/gfs_rmdir.3.html
%{html_prefix}/ja/ref/man3/gfs_stat.3.html
%{html_prefix}/ja/ref/man3/gfs_stat_free.3.html
%{html_prefix}/ja/ref/man3/gfs_unlink.3.html
%{html_prefix}/ja/ref/man3/gfs_unlink_section.3.html
%{html_prefix}/ja/ref/man3/gfs_utimes.3.html
%{html_prefix}/ja/ref/man5/gfarm.conf.5.html
%{html_prefix}/ja/ref/man8/gfmd.8.html
%{html_prefix}/ja/ref/man8/gfsd.8.html
%{html_prefix}/ja/user/export-gfarm.html
%{html_prefix}/ja/user/index.html
%{html_prefix}/ja/user/nfs-gfarmfs.html
%{html_prefix}/ja/user/samba-gfarmfs.html
%{html_prefix}/ja/user/samba-hook.html
%{html_prefix}/ja/user/smboverssh.html
%{html_prefix}/pic/gfarm-logo.gif
%endif
%{doc_prefix}/INSTALL.en
%{doc_prefix}/INSTALL.ja
%{doc_prefix}/INSTALL.RPM.en
%{doc_prefix}/INSTALL.RPM.ja
%{doc_prefix}/LICENSE
%{doc_prefix}/README.en
%{doc_prefix}/README.ja
%{doc_prefix}/RELNOTES
%{doc_prefix}/OVERVIEW.en
%{doc_prefix}/OVERVIEW.ja
%{doc_prefix}/SETUP.en
%{doc_prefix}/SETUP.ja
%{doc_prefix}/SETUP.private.en
%{doc_prefix}/SETUP.private.ja
%{doc_prefix}/Gfarm-FAQ.en
%{doc_prefix}/Gfarm-FAQ.ja
%{doc_prefix}/KNOWN_PROBLEMS.en
%{doc_prefix}/KNOWN_PROBLEMS.ja
%if %{gfarm_v2_not_yet}
%{doc_prefix}/README.hook.en
%{doc_prefix}/README.hook.ja
%endif

%files libs
%defattr(-,root,root)
%{lib_prefix}/libgfarm.so.0
%{lib_prefix}/libgfarm.so.0.0.0
%if %{gfarm_v2_not_yet}
%{lib_prefix}/libgfs_hook.so.0
%{lib_prefix}/libgfs_hook.so.0.0.0
%{lib_prefix}/libgfs_hook_debug.so.0
%{lib_prefix}/libgfs_hook_debug.so.0.0.0
%{lib_prefix}/libgfs_hook_no_init.so.0
%{lib_prefix}/libgfs_hook_no_init.so.0.0.0
%{lib_prefix}/libgfs_hook_no_init_debug.so.0
%{lib_prefix}/libgfs_hook_no_init_debug.so.0.0.0
%endif
%dir %{share_prefix}
%dir %{share_prefix}/config
%{share_prefix}/config/config-gfarm.sysdep
%if %{mpi}
%{lib_prefix}/libgfs_hook_mpi.so.0
%{lib_prefix}/libgfs_hook_mpi.so.0.0.0
%{lib_prefix}/libgfs_hook_mpi_debug.so.0
%{lib_prefix}/libgfs_hook_mpi_debug.so.0.0.0
%endif
%if %{have_ns}
%{lib_prefix}/libns.so.0
%{lib_prefix}/libns.so.0.0.0
%{lib_prefix}/libnsexec.so.0
%{lib_prefix}/libnsexec.so.0.0.0
%endif

%files client
%defattr(-,root,root)
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfarm-pcp
%{prefix}/bin/gfarm-prun
%{prefix}/bin/gfarm-ptool
%{prefix}/bin/gfdf
%endif
%{prefix}/bin/gfchgrp
%{prefix}/bin/gfchmod
%{prefix}/bin/gfchown
%{prefix}/bin/gfexport
%{prefix}/bin/gfhost
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfifo.sh
%{prefix}/bin/gfimport_fixed
%{prefix}/bin/gfimport_text
%endif
%{prefix}/bin/gfkey
%{prefix}/bin/gfls
%{prefix}/bin/gfmkdir
%{prefix}/bin/gfmv
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfmpirun_p4
%{prefix}/bin/gfps
%{prefix}/bin/gfpwd
%{prefix}/bin/gfq.sh
%{prefix}/bin/gfq_commit.sh
%{prefix}/bin/gfq_setup.sh
%{prefix}/bin/gfrcmd
%endif
%{prefix}/bin/gfreg
%{prefix}/bin/gfrep
%{prefix}/bin/gfrm
%{prefix}/bin/gfrmdir
%{prefix}/bin/gfuser
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfrsh
%{prefix}/bin/gfrshl
%{prefix}/bin/gfrun
%{prefix}/bin/gfsched
%{prefix}/bin/gfsck
%{prefix}/bin/gfsetdir
%{prefix}/bin/gfssh
%{prefix}/bin/gfsshl
%endif
%{prefix}/bin/gfstat
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfstatus
%{prefix}/bin/gftest
%endif
%{prefix}/bin/gfwhere
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfwhoami
%{profile_prefix}/gfarm.sh
%{profile_prefix}/gfarm.csh
%endif

%if %{have_ns}
%{prefix}/sbin/gfarmd
%{prefix}/bin/ns_get
%{prefix}/bin/ns_lstat
%{prefix}/bin/ns_mkdir
%{prefix}/bin/ns_put
%{prefix}/bin/ns_readdir
%{prefix}/bin/ns_readlink
%{prefix}/bin/ns_rename
%{prefix}/bin/ns_stat
%{prefix}/bin/ns_symlink
%{prefix}/bin/ns_unlink
%{prefix}/bin/ns_unlink_dir
%endif

%if %{gfarm_v2_not_yet}
%files gfptool
%defattr(-,root,root)
%{prefix}/bin/gfcombine
%{prefix}/bin/gfcombine_hook
%{prefix}/bin/gfcp
%{prefix}/bin/gfcp_hook

%{prefix}/bin/gfgrep
%if %{mpi}
%{prefix}/bin/gfwc
%endif

%{prefix}/libexec/gfrepbe_client
%{prefix}/libexec/gfrepbe_server
%{prefix}/sbin/gfregister
%endif

%files fsnode
%defattr(-,root,root)
%{prefix}/bin/config-gfsd
%{prefix}/bin/gfarm.arch.guess
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfexec
%{prefix}/bin/gfsplck
%{prefix}/bin/thput-gfpio
%endif
%{prefix}/sbin/gfsd
%dir %{share_prefix}
%dir %{share_prefix}/config
%{share_prefix}/config/linux/debian/gfsd.in
%{share_prefix}/config/linux/default/gfsd.in
%{share_prefix}/config/linux/redhat/gfsd.in
%{share_prefix}/config/linux/suse/gfsd.in

%files server
%defattr(-,root,root)
%{prefix}/sbin/gfmd
%{prefix}/bin/config-gfarm
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfdump
%{prefix}/bin/gfusage
%endif
%dir %{share_prefix}
%dir %{share_prefix}/config
%{share_prefix}/config/bdb.DB_CONFIG.in
%{share_prefix}/config/config-gfarm.ldap
%{share_prefix}/config/config-gfarm.postgresql
%{share_prefix}/config/gfarm.conf-ldap.in
%{share_prefix}/config/gfarm.conf-postgresql.in
%{share_prefix}/config/gfarm.conf.in
%{share_prefix}/config/gfarm.sql
%{share_prefix}/config/gfarm.schema
%{share_prefix}/config/initial.ldif.in
%{share_prefix}/config/linux/debian/gfarm-pgsql.in
%{share_prefix}/config/linux/debian/gfarm-slapd.in
%{share_prefix}/config/linux/debian/gfmd.in
%{share_prefix}/config/linux/default/gfarm-pgsql.in
%{share_prefix}/config/linux/default/gfarm-slapd.in
%{share_prefix}/config/linux/default/gfmd.in
%{share_prefix}/config/linux/redhat/gfarm-pgsql.in
%{share_prefix}/config/linux/redhat/gfarm-slapd.in
%{share_prefix}/config/linux/redhat/gfmd.in
%{share_prefix}/config/linux/suse/gfarm-pgsql.in
%{share_prefix}/config/linux/suse/gfarm-slapd.in
%{share_prefix}/config/linux/suse/gfmd.in
%{share_prefix}/config/slapd.conf-2.0.in
%{share_prefix}/config/slapd.conf-2.1.in

%files devel
%defattr(-,root,root)
%{prefix}/include/gfarm/gfarm.h
%{prefix}/include/gfarm/gfarm_config.h
%{prefix}/include/gfarm/error.h
%{prefix}/include/gfarm/gfarm_misc.h
%{prefix}/include/gfarm/gfarm_stringlist.h
%{prefix}/include/gfarm/host_info.h
%{prefix}/include/gfarm/user_info.h
%{prefix}/include/gfarm/group_info.h
%{prefix}/include/gfarm/gfs.h
%{prefix}/include/gfarm/gfs_glob.h
%if %{gfarm_v2_not_yet}
%{prefix}/include/gfarm/gfs_hook.h
%{lib_prefix}/gfs_hook.o
%{lib_prefix}/gfs_hook_debug.o
%{lib_prefix}/gfs_hook_no_init.o
%{lib_prefix}/gfs_hook_no_init_debug.o
%{lib_prefix}/hooks_init_mpi.c
%endif
%{lib_prefix}/libgfarm.a
%{lib_prefix}/libgfarm.la
%{lib_prefix}/libgfarm.so
%if %{gfarm_v2_not_yet}
%{lib_prefix}/libgfs_hook.a
%{lib_prefix}/libgfs_hook.la
%{lib_prefix}/libgfs_hook.so
%{lib_prefix}/libgfs_hook_debug.a
%{lib_prefix}/libgfs_hook_debug.la
%{lib_prefix}/libgfs_hook_debug.so
%{lib_prefix}/libgfs_hook_no_init.a
%{lib_prefix}/libgfs_hook_no_init.la
%{lib_prefix}/libgfs_hook_no_init.so
%{lib_prefix}/libgfs_hook_no_init_debug.a
%{lib_prefix}/libgfs_hook_no_init_debug.la
%{lib_prefix}/libgfs_hook_no_init_debug.so
%endif
%if %{mpi}
%{lib_prefix}/gfs_hook_mpi.o
%{lib_prefix}/gfs_hook_mpi_debug.o
%{lib_prefix}/libgfs_hook_mpi.a
%{lib_prefix}/libgfs_hook_mpi.la
%{lib_prefix}/libgfs_hook_mpi.so
%{lib_prefix}/libgfs_hook_mpi_debug.a
%{lib_prefix}/libgfs_hook_mpi_debug.la
%{lib_prefix}/libgfs_hook_mpi_debug.so
%endif


%if %{have_ns}
%{prefix}/include/gfarm/comm.h
%{prefix}/include/gfarm/con.h
%{prefix}/include/gfarm/debug.h
%{prefix}/include/gfarm/gflib.h
%{prefix}/include/gfarm/ns.h
%{prefix}/include/gfarm/nsclnt.h
%{prefix}/include/gfarm/nscom.h
%{prefix}/include/gfarm/nsexec.h
%{prefix}/include/gfarm/soc-lxdr.h
%{prefix}/include/gfarm/soc.h
%{prefix}/include/gfarm/type.h
%{lib_prefix}/libns.a
%{lib_prefix}/libns.la
%{lib_prefix}/libns.so
%{lib_prefix}/libnsexec.a
%{lib_prefix}/libnsexec.la
%{lib_prefix}/libnsexec.so
%endif

%if %{gfarm_v2_not_yet}
%files gfront
%defattr(-,root,root)
%{prefix}/bin/gfront
%{prefix}/share/java/gfront.jar
%endif
