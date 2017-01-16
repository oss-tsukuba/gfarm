# Part 1 data definition
%define pkg	gfarm
%if %{undefined ver}
%define ver	2.6.16
%endif
%if %{undefined rel}
%define rel	1
%endif

# a hook to make RPM version number different from %{ver}
%define pkgver	%{ver}

%define prefix		%{_prefix}
%define lib_prefix	%{_libdir}
%define libexec_prefix	%{_libexecdir}
%define man_prefix	%{_mandir}
%define share_prefix	%{_prefix}/share/%{pkg}
%define doc_prefix	%{_prefix}/share/doc/%{name}-%{ver}
%define html_prefix	%{doc_prefix}/html
%define rc_prefix	%{_sysconfdir}/rc.d/init.d
%define profile_prefix	%{_sysconfdir}/profile.d
%define sysconfdir	%{_sysconfdir}

%define gfarm_v2_not_yet 0

#
# check && enable/disable Globus
#
# do the followings to build gfarm-gsi-*.rpm:
#   # env GFARM_CONFIGURE_OPTION=--with-globus rpmbuild -bb gfarm.spec
# or
#   # env GFARM_CONFIGURE_OPTION="--with-globus=/usr/grid --with-globus-flavor=gcc32 --with-globus-static" \
#	rpmbuild -bb gfarm.spec

%define globus %(echo "${GFARM_CONFIGURE_OPTION}" | grep -e --with-globus > /dev/null && echo 1 || echo 0)

%if %{undefined pkg_suffix}
%if %{globus}
%define pkg_suffix	-gsi
%else
%define pkg_suffix	%{nil}
%endif
%endif
%define package_name	%{pkg}%{pkg_suffix}

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
Group: Applications/File
License: BSD
Vendor: National Institute of Advanced Industrial Science and Technology (AIST) and Osamu Tatebe
URL: http://gfarm.sourceforge.net/
Packager: Osamu Tatebe <tatebe@cs.tsukuba.ac.jp>
BuildRoot: %{_tmppath}/%{name}-%{version}-buildroot

%package doc
Summary: Document for Gfarm file system
Group: Documentation
# always provide "gfarm-doc" as a virtual package.
%if "%{pkg_suffix}" != ""
Provides: %{pkg}-doc = %{pkgver}-%{rel}
%endif

%package libs
Summary: Runtime libraries for Gfarm file system
Group: System Environment/Libraries
# always provide "gfarm-libs" as a virtual package.
%if "%{pkg_suffix}" != ""
Provides: %{pkg}-libs = %{pkgver}-%{rel}
%endif
BuildRequires: openssl-devel, postgresql-devel

%package client
Summary: Clients for Gfarm file system
Group: Applications/File
# always provide "gfarm-client" as a virtual package.
%if "%{pkg_suffix}" != ""
Provides: %{pkg}-client = %{pkgver}-%{rel}
%endif
Requires: %{package_name}-libs = %{pkgver}

%package fsnode
Summary: File system daemon for Gfarm file system
Group: System Environment/Daemons
# always provide "gfarm-fsnode" as a virtual package.
%if "%{pkg_suffix}" != ""
Provides: %{pkg}-fsnode = %{pkgver}-%{rel}
%endif
Requires: %{package_name}-libs = %{pkgver}, %{package_name}-client = %{pkgver}

%package server
Summary: Metadata server for Gfarm file system
Group: System Environment/Daemons
# always provide "gfarm-server" as a virtual package.
%if "%{pkg_suffix}" != ""
Provides: %{pkg}-server = %{pkgver}-%{rel}
%endif
Requires: %{package_name}-libs = %{pkgver}

%package ganglia
Summary: Gfarm performance monitoring plugin for Ganglia
Group: System Environment/Libraries
# always provide "gfarm-ganglia" as a virtual package.
%if "%{pkg_suffix}" != ""
Provides: %{pkg}-ganglia = %{pkgver}-%{rel}
%endif

%package devel
Summary: Development header files and libraries for Gfarm file system
Group: Development/Libraries
%if "%{pkg_suffix}" != ""
Provides: %{pkg}-devel = %{pkgver}-%{rel}
%endif
Requires: %{package_name}-libs = %{pkgver}

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

%description fsnode
File system daemon for Gfarm file system

%description server
Metadata server for Gfarm file system

%description ganglia
Gfarm performance monitoring plugin for Ganglia

%description devel
Development header files and libraries for Gfarm file system

%changelog
* Sat Jan 16 2016 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.6.8-1
- Gfarm version 2.6.8 released

* Sat Nov 28 2015 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.6.7-1
- Gfarm version 2.6.7 released

* Sat Aug 29 2015 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.6.6-1
- Gfarm version 2.6.6 released

* Thu Jun 25 2015 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.6.5-1
- Gfarm version 2.6.5 released

* Fri Mar 27 2015 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.6.2-1
- Gfarm version 2.6.2 released

* Mon Mar  2 2015 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.6.1-1
- Gfarm version 2.6.1 released

* Wed Dec 17 2014 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.6.0-1
- Gfarm version 2.6.0 released

* Mon Apr 22 2013 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.5.8-1
- Gfarm version 2.5.8 released

* Thu Jan 15 2013 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.5.8rc2-1
- Gfarm version 2.5.8 released candidate 2
- gfruntest, gfservice and systest are included in the client package

* Thu Nov  1 2012 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.5.7.2-1
- Gfarm version 2.5.7.2 released
- Use GFARM_CONFIGURE_OPTION instead of GLOBUS_PREFIX and
  GLOBUS_FLAVOR to build gfarm-gsi package

* Mon Sep  3 2012 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.5.7-1
- Gfarm version 2.5.7 released

* Tue Feb 25 2012 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.5.4.1-1
- Gfarm version 2.5.4.1 released

* Tue Feb 22 2012 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.5.4-1
- Gfarm version 2.5.4 released

* Mon Dec 19 2011 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.5.2-1
- Gfarm version 2.5.2 released

* Wed Sep 14 2011 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.5.1-1
- Gfarm version 2.5.1 released

* Thu Apr 22 2011 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.4.2-1
- Gfarm version 2.4.2 released

* Thu Jul 22 2010 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.4.0-1
- Gfarm version 2.4.0 released

* Wed Jul 21 2010 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.3.2-3
- portability fix for old rpm that does not support nested conditionals
- compatibility fix for Linux 2.4 and old OpenLDAP library

* Tue Jul 20 2010 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.3.2-2
- gfhost -R does not work [sf.net trac #120]
- retry another file system node in GFARM_ERR_FILE_MIGRATED case
  [sf.net trac #117]

* Thu Jul  1 2010 Osamu Tatebe <tatebe@cs.tsukuba.ac.jp> 2.3.2-1
- Gfarm version 2.3.2 released

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
	--with-openssl=/usr \
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

%post doc
if [ -d %{doc_prefix}/gfperf ]; then
	echo If you want to setup gfperf, see documents in %{doc_prefix}/gfperf
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
%endif
%{man_prefix}/man1/gfchgrp.1.gz
%{man_prefix}/man1/gfchmod.1.gz
%{man_prefix}/man1/gfchown.1.gz
%{man_prefix}/man1/gfcksum.1.gz
%{man_prefix}/man1/gfdf.1.gz
%{man_prefix}/man1/gfedquota.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/man1/gfexec.1.gz
%endif
%{man_prefix}/man1/gfexport.1.gz
%{man_prefix}/man1/gffindxmlattr.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/man1/gfgrep.1.gz
%endif
%{man_prefix}/man1/gfgetfacl.1.gz
%{man_prefix}/man1/gfgroup.1.gz
%{man_prefix}/man1/gfhost.1.gz
%{man_prefix}/man1/gfhostgroup.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/man1/gfimport_fixed.1.gz
%{man_prefix}/man1/gfimport_text.1.gz
%endif
%{man_prefix}/man1/gfjournal.1.gz
%{man_prefix}/man1/gfjournaladmin.1.gz
%{man_prefix}/man1/gfkey.1.gz
%{man_prefix}/man1/gfln.1.gz
%{man_prefix}/man1/gfls.1.gz
%{man_prefix}/man1/gflsof.1.gz
%{man_prefix}/man1/gfmdhost.1.gz
%{man_prefix}/man1/gfmkdir.1.gz
%{man_prefix}/man1/gfmv.1.gz
%{man_prefix}/man1/gfncopy.1.gz
%{man_prefix}/man1/gfpcopy.1.gz
%{man_prefix}/man1/gfprep.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/man1/gfps.1.gz
%{man_prefix}/man1/gfpwd.1.gz
%{man_prefix}/man1/gfrcmd.1.gz
%endif
%{man_prefix}/man1/gfquota.1.gz
%{man_prefix}/man1/gfquotacheck.1.gz
%{man_prefix}/man1/gfreg.1.gz
%{man_prefix}/man1/gfrep.1.gz
%{man_prefix}/man1/gfrm.1.gz
%{man_prefix}/man1/gfrmdir.1.gz
%{man_prefix}/man1/gfsched.1.gz
%{man_prefix}/man1/gfservice.1.gz
%{man_prefix}/man1/gfservice-agent.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/man1/gfront.1.gz
%{man_prefix}/man1/gfrsh.1.gz
%{man_prefix}/man1/gfrun.1.gz
%{man_prefix}/man1/gfsetdir.1.gz
%{man_prefix}/man1/gfssh.1.gz
%endif
%{man_prefix}/man1/gfsetfacl.1.gz
%{man_prefix}/man1/gfspoolpath.1.gz
%{man_prefix}/man1/gfstat.1.gz
%{man_prefix}/man1/gfstatus.1.gz
%{man_prefix}/man1/gfsudo.1.gz
%{man_prefix}/man1/gftest.1.gz
%{man_prefix}/man1/gfusage.1.gz
%{man_prefix}/man1/gfuser.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/man1/gfwc.1.gz
%endif
%{man_prefix}/man1/gfwhere.1.gz
%{man_prefix}/man1/gfwhoami.1.gz
%{man_prefix}/man1/gfxattr.1.gz
%{man_prefix}/man1/gfperf-autoreplica.1.gz
%{man_prefix}/man1/gfperf-copy.1.gz
%{man_prefix}/man1/gfperf-metadata.1.gz
%{man_prefix}/man1/gfperf-parallel-autoreplica.1.gz
%{man_prefix}/man1/gfperf-parallel-read.1.gz
%{man_prefix}/man1/gfperf-parallel-replica.1.gz
%{man_prefix}/man1/gfperf-parallel-write.1.gz
%{man_prefix}/man1/gfperf-read.1.gz
%{man_prefix}/man1/gfperf-replica.1.gz
%{man_prefix}/man1/gfperf-tree.1.gz
%{man_prefix}/man1/gfperf-wrapper.sh.1.gz
%{man_prefix}/man1/gfperf-write.1.gz
%{man_prefix}/man1/gfperf.rb.1.gz
%{man_prefix}/man1/gfstress.rb.1.gz
%{man_prefix}/man3/gfarm.3.gz
%{man_prefix}/man3/gfarm_initialize.3.gz
%{man_prefix}/man3/gfarm_terminate.3.gz
%{man_prefix}/man3/gfs_acl_add_perm.3.gz
%{man_prefix}/man3/gfs_acl_calc_mask.3.gz
%{man_prefix}/man3/gfs_acl_check.3.gz
%{man_prefix}/man3/gfs_acl_clear_perms.3.gz
%{man_prefix}/man3/gfs_acl_cmp.3.gz
%{man_prefix}/man3/gfs_acl_create_entry.3.gz
%{man_prefix}/man3/gfs_acl_delete_def_file.3.gz
%{man_prefix}/man3/gfs_acl_delete_entry.3.gz
%{man_prefix}/man3/gfs_acl_delete_perm.3.gz
%{man_prefix}/man3/gfs_acl_dup.3.gz
%{man_prefix}/man3/gfs_acl_entries.3.gz
%{man_prefix}/man3/gfs_acl_equiv_mode.3.gz
%{man_prefix}/man3/gfs_acl_error.3.gz
%{man_prefix}/man3/gfs_acl_free.3.gz
%{man_prefix}/man3/gfs_acl_from_mode.3.gz
%{man_prefix}/man3/gfs_acl_from_text.3.gz
%{man_prefix}/man3/gfs_acl_from_text_with_default.3.gz
%{man_prefix}/man3/gfs_acl_from_xattr_value.3.gz
%{man_prefix}/man3/gfs_acl_get_entry.3.gz
%{man_prefix}/man3/gfs_acl_get_file.3.gz
%{man_prefix}/man3/gfs_acl_get_perm.3.gz
%{man_prefix}/man3/gfs_acl_get_permset.3.gz
%{man_prefix}/man3/gfs_acl_get_qualifier.3.gz
%{man_prefix}/man3/gfs_acl_get_tag_type.3.gz
%{man_prefix}/man3/gfs_acl_init.3.gz
%{man_prefix}/man3/gfs_acl_set_file.3.gz
%{man_prefix}/man3/gfs_acl_set_permset.3.gz
%{man_prefix}/man3/gfs_acl_set_qualifier.3.gz
%{man_prefix}/man3/gfs_acl_set_tag_type.3.gz
%{man_prefix}/man3/gfs_acl_sort.3.gz
%{man_prefix}/man3/gfs_acl_to_any_text.3.gz
%{man_prefix}/man3/gfs_acl_to_text.3.gz
%{man_prefix}/man3/gfs_acl_to_xattr_value.3.gz
%{man_prefix}/man3/gfs_acl_valid.3.gz
%{man_prefix}/man3/gfs_pio_close.3.gz
%{man_prefix}/man3/gfs_pio_create.3.gz
%{man_prefix}/man3/gfs_pio_open.3.gz
%{man_prefix}/man3/gfs_pio_read.3.gz
%{man_prefix}/man3/gfs_pio_write.3.gz
%{man_prefix}/man3/gfs_pio_recvfile.3.gz
%{man_prefix}/man3/gfs_pio_sendfile.3.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/man3/gfarm_strings_free_deeply.3.gz
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
%{man_prefix}/man3/gfs_pio_datasync.3.gz
%{man_prefix}/man3/gfs_pio_eof.3.gz
%{man_prefix}/man3/gfs_pio_error.3.gz
%{man_prefix}/man3/gfs_pio_flush.3.gz
%{man_prefix}/man3/gfs_pio_getc.3.gz
%{man_prefix}/man3/gfs_pio_getline.3.gz
%{man_prefix}/man3/gfs_pio_gets.3.gz
%{man_prefix}/man3/gfs_pio_putc.3.gz
%{man_prefix}/man3/gfs_pio_putline.3.gz
%{man_prefix}/man3/gfs_pio_puts.3.gz
%{man_prefix}/man3/gfs_pio_readdelim.3.gz
%{man_prefix}/man3/gfs_pio_readline.3.gz
%{man_prefix}/man3/gfs_pio_seek.3.gz
%{man_prefix}/man3/gfs_pio_set_local.3.gz
%{man_prefix}/man3/gfs_pio_set_view_index.3.gz
%{man_prefix}/man3/gfs_pio_set_view_local.3.gz
%{man_prefix}/man3/gfs_pio_sync.3.gz
%{man_prefix}/man3/gfs_pio_truncate.3.gz
%{man_prefix}/man3/gfs_pio_ungetc.3.gz
%{man_prefix}/man3/gfs_readdir.3.gz
%{man_prefix}/man3/gfs_realpath.3.gz
%{man_prefix}/man3/gfs_rename.3.gz
%{man_prefix}/man3/gfs_rmdir.3.gz
%{man_prefix}/man3/gfs_stat.3.gz
%{man_prefix}/man3/gfs_stat_free.3.gz
%{man_prefix}/man3/gfs_unlink.3.gz
%{man_prefix}/man3/gfs_unlink_section.3.gz
%{man_prefix}/man3/gfs_utimes.3.gz
%endif
%{man_prefix}/man5/gfarm2.conf.5.gz
%{man_prefix}/man5/gfarm_attr.5.gz
%{man_prefix}/man5/gfservice.conf.5.gz
%{man_prefix}/man7/gfarm_environ.7.gz
%{man_prefix}/man8/gfdump.postgresql.8.gz
%{man_prefix}/man8/gfmd.8.gz
%{man_prefix}/man8/gfsd.8.gz
%{man_prefix}/man8/config-gfarm-update.8.gz
%{man_prefix}/man8/config-gfarm.8.gz
%{man_prefix}/man8/config-gfsd.8.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/ja/man1/gfarm_agent.1.gz
%{man_prefix}/ja/man1/gfcd.1.gz
%endif
%{man_prefix}/ja/man1/gfchgrp.1.gz
%{man_prefix}/ja/man1/gfchmod.1.gz
%{man_prefix}/ja/man1/gfchown.1.gz
%{man_prefix}/ja/man1/gfcksum.1.gz
%{man_prefix}/ja/man1/gfdf.1.gz
%{man_prefix}/ja/man1/gfedquota.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/ja/man1/gfexec.1.gz
%endif
%{man_prefix}/ja/man1/gfexport.1.gz
%{man_prefix}/ja/man1/gffindxmlattr.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/ja/man1/gfgrep.1.gz
%endif
%{man_prefix}/ja/man1/gfgetfacl.1.gz
%{man_prefix}/ja/man1/gfgroup.1.gz
%{man_prefix}/ja/man1/gfhost.1.gz
%{man_prefix}/ja/man1/gfhostgroup.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/ja/man1/gfimport_fixed.1.gz
%{man_prefix}/ja/man1/gfimport_text.1.gz
%endif
%{man_prefix}/ja/man1/gfjournal.1.gz
%{man_prefix}/ja/man1/gfjournaladmin.1.gz
%{man_prefix}/ja/man1/gfkey.1.gz
%{man_prefix}/ja/man1/gfln.1.gz
%{man_prefix}/ja/man1/gfls.1.gz
%{man_prefix}/ja/man1/gflsof.1.gz
%{man_prefix}/ja/man1/gfmdhost.1.gz
%{man_prefix}/ja/man1/gfmkdir.1.gz
%{man_prefix}/ja/man1/gfmv.1.gz
%{man_prefix}/ja/man1/gfncopy.1.gz
%{man_prefix}/ja/man1/gfpcopy.1.gz
%{man_prefix}/ja/man1/gfprep.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/ja/man1/gfps.1.gz
%{man_prefix}/ja/man1/gfpwd.1.gz
%{man_prefix}/ja/man1/gfrcmd.1.gz
%endif
%{man_prefix}/ja/man1/gfquota.1.gz
%{man_prefix}/ja/man1/gfquotacheck.1.gz
%{man_prefix}/ja/man1/gfreg.1.gz
%{man_prefix}/ja/man1/gfrep.1.gz
%{man_prefix}/ja/man1/gfrm.1.gz
%{man_prefix}/ja/man1/gfrmdir.1.gz
%{man_prefix}/ja/man1/gfsched.1.gz
%{man_prefix}/ja/man1/gfservice.1.gz
%{man_prefix}/ja/man1/gfservice-agent.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/ja/man1/gfront.1.gz
%{man_prefix}/ja/man1/gfrsh.1.gz
%{man_prefix}/ja/man1/gfrun.1.gz
%{man_prefix}/ja/man1/gfsetdir.1.gz
%{man_prefix}/ja/man1/gfssh.1.gz
%endif
%{man_prefix}/ja/man1/gfsetfacl.1.gz
%{man_prefix}/ja/man1/gfspoolpath.1.gz
%{man_prefix}/ja/man1/gfstat.1.gz
%{man_prefix}/ja/man1/gfstatus.1.gz
%{man_prefix}/ja/man1/gfsudo.1.gz
%{man_prefix}/ja/man1/gftest.1.gz
%{man_prefix}/ja/man1/gfusage.1.gz
%{man_prefix}/ja/man1/gfuser.1.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/ja/man1/gfwc.1.gz
%endif
%{man_prefix}/ja/man1/gfwhere.1.gz
%{man_prefix}/ja/man1/gfwhoami.1.gz
%{man_prefix}/ja/man1/gfxattr.1.gz
%{man_prefix}/ja/man1/gfperf-autoreplica.1.gz
%{man_prefix}/ja/man1/gfperf-copy.1.gz
%{man_prefix}/ja/man1/gfperf-metadata.1.gz
%{man_prefix}/ja/man1/gfperf-parallel-autoreplica.1.gz
%{man_prefix}/ja/man1/gfperf-parallel-read.1.gz
%{man_prefix}/ja/man1/gfperf-parallel-replica.1.gz
%{man_prefix}/ja/man1/gfperf-parallel-write.1.gz
%{man_prefix}/ja/man1/gfperf-read.1.gz
%{man_prefix}/ja/man1/gfperf-replica.1.gz
%{man_prefix}/ja/man1/gfperf-tree.1.gz
%{man_prefix}/ja/man1/gfperf-wrapper.sh.1.gz
%{man_prefix}/ja/man1/gfperf-write.1.gz
%{man_prefix}/ja/man1/gfperf.rb.1.gz
%{man_prefix}/ja/man1/gfstress.rb.1.gz
%{man_prefix}/ja/man3/gfarm.3.gz
%{man_prefix}/ja/man3/gfarm_initialize.3.gz
%{man_prefix}/ja/man3/gfarm_terminate.3.gz
%{man_prefix}/ja/man3/gfs_pio_close.3.gz
%{man_prefix}/ja/man3/gfs_pio_create.3.gz
%{man_prefix}/ja/man3/gfs_pio_open.3.gz
%{man_prefix}/ja/man3/gfs_pio_read.3.gz
%{man_prefix}/ja/man3/gfs_pio_write.3.gz
%{man_prefix}/ja/man3/gfs_pio_recvfile.3.gz
%{man_prefix}/ja/man3/gfs_pio_sendfile.3.gz
%if %{gfarm_v2_not_yet}
%{man_prefix}/ja/man3/gfarm_hostlist_read.3.gz
%{man_prefix}/ja/man3/gfarm_import_fragment_config_read.3.gz
%{man_prefix}/ja/man3/gfarm_import_fragment_size_alloc.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_add.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_cat.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_elem.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_free.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_init.3.gz
%{man_prefix}/ja/man3/gfarm_stringlist_length.3.gz
%{man_prefix}/ja/man3/gfarm_strings_free_deeply.3.gz
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
%{man_prefix}/ja/man3/gfs_pio_datasync.3.gz
%{man_prefix}/ja/man3/gfs_pio_eof.3.gz
%{man_prefix}/ja/man3/gfs_pio_error.3.gz
%{man_prefix}/ja/man3/gfs_pio_flush.3.gz
%{man_prefix}/ja/man3/gfs_pio_getc.3.gz
%{man_prefix}/ja/man3/gfs_pio_getline.3.gz
%{man_prefix}/ja/man3/gfs_pio_gets.3.gz
%{man_prefix}/ja/man3/gfs_pio_putc.3.gz
%{man_prefix}/ja/man3/gfs_pio_putline.3.gz
%{man_prefix}/ja/man3/gfs_pio_puts.3.gz
%{man_prefix}/ja/man3/gfs_pio_readdelim.3.gz
%{man_prefix}/ja/man3/gfs_pio_readline.3.gz
%{man_prefix}/ja/man3/gfs_pio_seek.3.gz
%{man_prefix}/ja/man3/gfs_pio_set_local.3.gz
%{man_prefix}/ja/man3/gfs_pio_set_view_index.3.gz
%{man_prefix}/ja/man3/gfs_pio_set_view_local.3.gz
%{man_prefix}/ja/man3/gfs_pio_sync.3.gz
%{man_prefix}/ja/man3/gfs_pio_truncate.3.gz
%{man_prefix}/ja/man3/gfs_pio_ungetc.3.gz
%{man_prefix}/ja/man3/gfs_readdir.3.gz
%{man_prefix}/ja/man3/gfs_realpath.3.gz
%{man_prefix}/ja/man3/gfs_rename.3.gz
%{man_prefix}/ja/man3/gfs_rmdir.3.gz
%{man_prefix}/ja/man3/gfs_stat.3.gz
%{man_prefix}/ja/man3/gfs_stat_free.3.gz
%{man_prefix}/ja/man3/gfs_unlink.3.gz
%{man_prefix}/ja/man3/gfs_unlink_section.3.gz
%{man_prefix}/ja/man3/gfs_utimes.3.gz
%endif
%{man_prefix}/ja/man5/gfarm2.conf.5.gz
%{man_prefix}/ja/man5/gfarm_attr.5.gz
%{man_prefix}/ja/man5/gfservice.conf.5.gz
%{man_prefix}/ja/man7/gfarm_environ.7.gz
%{man_prefix}/ja/man8/gfdump.postgresql.8.gz
%{man_prefix}/ja/man8/gfmd.8.gz
%{man_prefix}/ja/man8/gfsd.8.gz
%{man_prefix}/ja/man8/config-gfarm-update.8.gz
%{man_prefix}/ja/man8/config-gfarm.8.gz
%{man_prefix}/ja/man8/config-gfsd.8.gz
%{html_prefix}/index.html
%{html_prefix}/en/ref/index.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/en/ref/man1/gfarm_agent.1.html
%{html_prefix}/en/ref/man1/gfcd.1.html
%endif
%{html_prefix}/en/ref/man1/gfchgrp.1.html
%{html_prefix}/en/ref/man1/gfchmod.1.html
%{html_prefix}/en/ref/man1/gfchown.1.html
%{html_prefix}/en/ref/man1/gfcksum.1.html
%{html_prefix}/en/ref/man1/gfdf.1.html
%{html_prefix}/en/ref/man1/gfedquota.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/en/ref/man1/gfexec.1.html
%endif
%{html_prefix}/en/ref/man1/gfexport.1.html
%{html_prefix}/en/ref/man1/gffindxmlattr.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/en/ref/man1/gfgrep.1.html
%endif
%{html_prefix}/en/ref/man1/gfgetfacl.1.html
%{html_prefix}/en/ref/man1/gfgroup.1.html
%{html_prefix}/en/ref/man1/gfhost.1.html
%{html_prefix}/en/ref/man1/gfhostgroup.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/en/ref/man1/gfimport_fixed.1.html
%{html_prefix}/en/ref/man1/gfimport_text.1.html
%endif
%{html_prefix}/en/ref/man1/gfjournal.1.html
%{html_prefix}/en/ref/man1/gfjournaladmin.1.html
%{html_prefix}/en/ref/man1/gfkey.1.html
%{html_prefix}/en/ref/man1/gfln.1.html
%{html_prefix}/en/ref/man1/gfls.1.html
%{html_prefix}/en/ref/man1/gflsof.1.html
%{html_prefix}/en/ref/man1/gfmdhost.1.html
%{html_prefix}/en/ref/man1/gfmkdir.1.html
%{html_prefix}/en/ref/man1/gfmv.1.html
%{html_prefix}/en/ref/man1/gfncopy.1.html
%{html_prefix}/en/ref/man1/gfpcopy.1.html
%{html_prefix}/en/ref/man1/gfprep.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/en/ref/man1/gfps.1.html
%{html_prefix}/en/ref/man1/gfpwd.1.html
%{html_prefix}/en/ref/man1/gfrcmd.1.html
%endif
%{html_prefix}/en/ref/man1/gfquota.1.html
%{html_prefix}/en/ref/man1/gfquotacheck.1.html
%{html_prefix}/en/ref/man1/gfreg.1.html
%{html_prefix}/en/ref/man1/gfrep.1.html
%{html_prefix}/en/ref/man1/gfrm.1.html
%{html_prefix}/en/ref/man1/gfrmdir.1.html
%{html_prefix}/en/ref/man1/gfsched.1.html
%{html_prefix}/en/ref/man1/gfservice.1.html
%{html_prefix}/en/ref/man1/gfservice-agent.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/en/ref/man1/gfront.1.html
%{html_prefix}/en/ref/man1/gfrun.1.html
%{html_prefix}/en/ref/man1/gfsetdir.1.html
%endif
%{html_prefix}/en/ref/man1/gfsetfacl.1.html
%{html_prefix}/en/ref/man1/gfspoolpath.1.html
%{html_prefix}/en/ref/man1/gfstat.1.html
%{html_prefix}/en/ref/man1/gfstatus.1.html
%{html_prefix}/en/ref/man1/gfsudo.1.html
%{html_prefix}/en/ref/man1/gftest.1.html
%{html_prefix}/en/ref/man1/gfusage.1.html
%{html_prefix}/en/ref/man1/gfuser.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/en/ref/man1/gfwc.1.html
%endif
%{html_prefix}/en/ref/man1/gfwhere.1.html
%{html_prefix}/en/ref/man1/gfwhoami.1.html
%{html_prefix}/en/ref/man1/gfxattr.1.html
%{html_prefix}/en/ref/man1/gfperf-autoreplica.1.html
%{html_prefix}/en/ref/man1/gfperf-copy.1.html
%{html_prefix}/en/ref/man1/gfperf-metadata.1.html
%{html_prefix}/en/ref/man1/gfperf-parallel-autoreplica.1.html
%{html_prefix}/en/ref/man1/gfperf-parallel-read.1.html
%{html_prefix}/en/ref/man1/gfperf-parallel-replica.1.html
%{html_prefix}/en/ref/man1/gfperf-parallel-write.1.html
%{html_prefix}/en/ref/man1/gfperf-read.1.html
%{html_prefix}/en/ref/man1/gfperf-replica.1.html
%{html_prefix}/en/ref/man1/gfperf-tree.1.html
%{html_prefix}/en/ref/man1/gfperf-wrapper.sh.1.html
%{html_prefix}/en/ref/man1/gfperf-write.1.html
%{html_prefix}/en/ref/man1/gfperf.rb.1.html
%{html_prefix}/en/ref/man1/gfstress.rb.1.html
%{html_prefix}/en/ref/man3/gfarm.3.html
%{html_prefix}/en/ref/man3/gfarm_initialize.3.html
%{html_prefix}/en/ref/man3/gfarm_terminate.3.html
%{html_prefix}/en/ref/man3/gfs_acl_add_perm.3.html
%{html_prefix}/en/ref/man3/gfs_acl_calc_mask.3.html
%{html_prefix}/en/ref/man3/gfs_acl_check.3.html
%{html_prefix}/en/ref/man3/gfs_acl_clear_perms.3.html
%{html_prefix}/en/ref/man3/gfs_acl_cmp.3.html
%{html_prefix}/en/ref/man3/gfs_acl_create_entry.3.html
%{html_prefix}/en/ref/man3/gfs_acl_delete_def_file.3.html
%{html_prefix}/en/ref/man3/gfs_acl_delete_entry.3.html
%{html_prefix}/en/ref/man3/gfs_acl_delete_perm.3.html
%{html_prefix}/en/ref/man3/gfs_acl_dup.3.html
%{html_prefix}/en/ref/man3/gfs_acl_entries.3.html
%{html_prefix}/en/ref/man3/gfs_acl_equiv_mode.3.html
%{html_prefix}/en/ref/man3/gfs_acl_error.3.html
%{html_prefix}/en/ref/man3/gfs_acl_free.3.html
%{html_prefix}/en/ref/man3/gfs_acl_from_mode.3.html
%{html_prefix}/en/ref/man3/gfs_acl_from_text.3.html
%{html_prefix}/en/ref/man3/gfs_acl_from_text_with_default.3.html
%{html_prefix}/en/ref/man3/gfs_acl_from_xattr_value.3.html
%{html_prefix}/en/ref/man3/gfs_acl_get_entry.3.html
%{html_prefix}/en/ref/man3/gfs_acl_get_file.3.html
%{html_prefix}/en/ref/man3/gfs_acl_get_perm.3.html
%{html_prefix}/en/ref/man3/gfs_acl_get_permset.3.html
%{html_prefix}/en/ref/man3/gfs_acl_get_qualifier.3.html
%{html_prefix}/en/ref/man3/gfs_acl_get_tag_type.3.html
%{html_prefix}/en/ref/man3/gfs_acl_init.3.html
%{html_prefix}/en/ref/man3/gfs_acl_set_file.3.html
%{html_prefix}/en/ref/man3/gfs_acl_set_permset.3.html
%{html_prefix}/en/ref/man3/gfs_acl_set_qualifier.3.html
%{html_prefix}/en/ref/man3/gfs_acl_set_tag_type.3.html
%{html_prefix}/en/ref/man3/gfs_acl_sort.3.html
%{html_prefix}/en/ref/man3/gfs_acl_to_any_text.3.html
%{html_prefix}/en/ref/man3/gfs_acl_to_text.3.html
%{html_prefix}/en/ref/man3/gfs_acl_to_xattr_value.3.html
%{html_prefix}/en/ref/man3/gfs_acl_valid.3.html
%{html_prefix}/en/ref/man3/gfs_pio_close.3.html
%{html_prefix}/en/ref/man3/gfs_pio_create.3.html
%{html_prefix}/en/ref/man3/gfs_pio_open.3.html
%{html_prefix}/en/ref/man3/gfs_pio_read.3.html
%{html_prefix}/en/ref/man3/gfs_pio_write.3.html
%{html_prefix}/en/ref/man3/gfs_pio_recvfile.3.html
%{html_prefix}/en/ref/man3/gfs_pio_sendfile.3.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/en/ref/man3/gfarm_strings_free_deeply.3.html
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
%{html_prefix}/en/ref/man3/gfs_pio_datasync.3.html
%{html_prefix}/en/ref/man3/gfs_pio_eof.3.html
%{html_prefix}/en/ref/man3/gfs_pio_error.3.html
%{html_prefix}/en/ref/man3/gfs_pio_flush.3.html
%{html_prefix}/en/ref/man3/gfs_pio_getc.3.html
%{html_prefix}/en/ref/man3/gfs_pio_getline.3.html
%{html_prefix}/en/ref/man3/gfs_pio_gets.3.html
%{html_prefix}/en/ref/man3/gfs_pio_putc.3.html
%{html_prefix}/en/ref/man3/gfs_pio_putline.3.html
%{html_prefix}/en/ref/man3/gfs_pio_puts.3.html
%{html_prefix}/en/ref/man3/gfs_pio_readdelim.3.html
%{html_prefix}/en/ref/man3/gfs_pio_readline.3.html
%{html_prefix}/en/ref/man3/gfs_pio_seek.3.html
%{html_prefix}/en/ref/man3/gfs_pio_set_local.3.html
%{html_prefix}/en/ref/man3/gfs_pio_set_view_index.3.html
%{html_prefix}/en/ref/man3/gfs_pio_set_view_local.3.html
%{html_prefix}/en/ref/man3/gfs_pio_sync.3.html
%{html_prefix}/en/ref/man3/gfs_pio_truncate.3.html
%{html_prefix}/en/ref/man3/gfs_pio_ungetc.3.html
%{html_prefix}/en/ref/man3/gfs_readdir.3.html
%{html_prefix}/en/ref/man3/gfs_realpath.3.html
%{html_prefix}/en/ref/man3/gfs_rename.3.html
%{html_prefix}/en/ref/man3/gfs_rmdir.3.html
%{html_prefix}/en/ref/man3/gfs_stat.3.html
%{html_prefix}/en/ref/man3/gfs_stat_free.3.html
%{html_prefix}/en/ref/man3/gfs_unlink.3.html
%{html_prefix}/en/ref/man3/gfs_unlink_section.3.html
%{html_prefix}/en/ref/man3/gfs_utimes.3.html
%endif
%{html_prefix}/en/ref/man5/gfarm2.conf.5.html
%{html_prefix}/en/ref/man5/gfarm_attr.5.html
%{html_prefix}/en/ref/man5/gfservice.conf.5.html
%{html_prefix}/en/ref/man7/gfarm_environ.7.html
%{html_prefix}/en/ref/man8/gfdump.postgresql.8.html
%{html_prefix}/en/ref/man8/gfmd.8.html
%{html_prefix}/en/ref/man8/gfsd.8.html
%{html_prefix}/en/ref/man8/config-gfarm-update.8.html
%{html_prefix}/en/ref/man8/config-gfarm.8.html
%{html_prefix}/en/ref/man8/config-gfsd.8.html
%{html_prefix}/en/user/index.html
%{html_prefix}/en/user/samba-gfarmfs.html
%{html_prefix}/en/user/redundancy-tutorial.html
%{html_prefix}/en/user/cipher-comparison.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/en/user/nfs-gfarmfs.html
%endif
%{html_prefix}/ja/ref/index.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/ja/ref/man1/gfarm_agent.1.html
%{html_prefix}/ja/ref/man1/gfcd.1.html
%endif
%{html_prefix}/ja/ref/man1/gfchgrp.1.html
%{html_prefix}/ja/ref/man1/gfchmod.1.html
%{html_prefix}/ja/ref/man1/gfchown.1.html
%{html_prefix}/ja/ref/man1/gfcksum.1.html
%{html_prefix}/ja/ref/man1/gfdf.1.html
%{html_prefix}/ja/ref/man1/gfedquota.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/ja/ref/man1/gfexec.1.html
%endif
%{html_prefix}/ja/ref/man1/gfexport.1.html
%{html_prefix}/ja/ref/man1/gffindxmlattr.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/ja/ref/man1/gfgrep.1.html
%endif
%{html_prefix}/ja/ref/man1/gfgetfacl.1.html
%{html_prefix}/ja/ref/man1/gfgroup.1.html
%{html_prefix}/ja/ref/man1/gfhost.1.html
%{html_prefix}/ja/ref/man1/gfhostgroup.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/ja/ref/man1/gfimport_fixed.1.html
%{html_prefix}/ja/ref/man1/gfimport_text.1.html
%endif
%{html_prefix}/ja/ref/man1/gfjournal.1.html
%{html_prefix}/ja/ref/man1/gfjournaladmin.1.html
%{html_prefix}/ja/ref/man1/gfkey.1.html
%{html_prefix}/ja/ref/man1/gfln.1.html
%{html_prefix}/ja/ref/man1/gfls.1.html
%{html_prefix}/ja/ref/man1/gflsof.1.html
%{html_prefix}/ja/ref/man1/gfmdhost.1.html
%{html_prefix}/ja/ref/man1/gfmkdir.1.html
%{html_prefix}/ja/ref/man1/gfmv.1.html
%{html_prefix}/ja/ref/man1/gfncopy.1.html
%{html_prefix}/ja/ref/man1/gfpcopy.1.html
%{html_prefix}/ja/ref/man1/gfprep.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/ja/ref/man1/gfps.1.html
%{html_prefix}/ja/ref/man1/gfpwd.1.html
%{html_prefix}/ja/ref/man1/gfrcmd.1.html
%endif
%{html_prefix}/ja/ref/man1/gfquota.1.html
%{html_prefix}/ja/ref/man1/gfquotacheck.1.html
%{html_prefix}/ja/ref/man1/gfreg.1.html
%{html_prefix}/ja/ref/man1/gfrep.1.html
%{html_prefix}/ja/ref/man1/gfrm.1.html
%{html_prefix}/ja/ref/man1/gfrmdir.1.html
%{html_prefix}/ja/ref/man1/gfsched.1.html
%{html_prefix}/ja/ref/man1/gfservice.1.html
%{html_prefix}/ja/ref/man1/gfservice-agent.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/ja/ref/man1/gfront.1.html
%{html_prefix}/ja/ref/man1/gfrun.1.html
%{html_prefix}/ja/ref/man1/gfsetdir.1.html
%endif
%{html_prefix}/ja/ref/man1/gfsetfacl.1.html
%{html_prefix}/ja/ref/man1/gfspoolpath.1.html
%{html_prefix}/ja/ref/man1/gfstat.1.html
%{html_prefix}/ja/ref/man1/gfstatus.1.html
%{html_prefix}/ja/ref/man1/gfsudo.1.html
%{html_prefix}/ja/ref/man1/gftest.1.html
%{html_prefix}/ja/ref/man1/gfusage.1.html
%{html_prefix}/ja/ref/man1/gfuser.1.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/ja/ref/man1/gfwc.1.html
%endif
%{html_prefix}/ja/ref/man1/gfwhere.1.html
%{html_prefix}/ja/ref/man1/gfwhoami.1.html
%{html_prefix}/ja/ref/man1/gfxattr.1.html
%{html_prefix}/ja/ref/man1/gfperf-autoreplica.1.html
%{html_prefix}/ja/ref/man1/gfperf-copy.1.html
%{html_prefix}/ja/ref/man1/gfperf-metadata.1.html
%{html_prefix}/ja/ref/man1/gfperf-parallel-autoreplica.1.html
%{html_prefix}/ja/ref/man1/gfperf-parallel-read.1.html
%{html_prefix}/ja/ref/man1/gfperf-parallel-replica.1.html
%{html_prefix}/ja/ref/man1/gfperf-parallel-write.1.html
%{html_prefix}/ja/ref/man1/gfperf-read.1.html
%{html_prefix}/ja/ref/man1/gfperf-replica.1.html
%{html_prefix}/ja/ref/man1/gfperf-tree.1.html
%{html_prefix}/ja/ref/man1/gfperf-wrapper.sh.1.html
%{html_prefix}/ja/ref/man1/gfperf-write.1.html
%{html_prefix}/ja/ref/man1/gfperf.rb.1.html
%{html_prefix}/ja/ref/man1/gfstress.rb.1.html
%{html_prefix}/ja/ref/man3/gfarm.3.html
%{html_prefix}/ja/ref/man3/gfarm_initialize.3.html
%{html_prefix}/ja/ref/man3/gfarm_terminate.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_close.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_create.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_open.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_read.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_write.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_recvfile.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_sendfile.3.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/ja/ref/man3/gfarm_hostlist_read.3.html
%{html_prefix}/ja/ref/man3/gfarm_import_fragment_config_read.3.html
%{html_prefix}/ja/ref/man3/gfarm_import_fragment_size_alloc.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_add.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_cat.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_elem.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_free.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_init.3.html
%{html_prefix}/ja/ref/man3/gfarm_stringlist_length.3.html
%{html_prefix}/ja/ref/man3/gfarm_strings_free_deeply.3.html
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
%{html_prefix}/ja/ref/man3/gfs_pio_datasync.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_eof.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_error.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_flush.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_getc.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_getline.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_gets.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_putc.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_putline.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_puts.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_readdelim.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_readline.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_seek.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_set_local.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_set_view_index.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_set_view_local.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_sync.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_truncate.3.html
%{html_prefix}/ja/ref/man3/gfs_pio_ungetc.3.html
%{html_prefix}/ja/ref/man3/gfs_readdir.3.html
%{html_prefix}/ja/ref/man3/gfs_realpath.3.html
%{html_prefix}/ja/ref/man3/gfs_rename.3.html
%{html_prefix}/ja/ref/man3/gfs_rmdir.3.html
%{html_prefix}/ja/ref/man3/gfs_stat.3.html
%{html_prefix}/ja/ref/man3/gfs_stat_free.3.html
%{html_prefix}/ja/ref/man3/gfs_unlink.3.html
%{html_prefix}/ja/ref/man3/gfs_unlink_section.3.html
%{html_prefix}/ja/ref/man3/gfs_utimes.3.html
%endif
%{html_prefix}/ja/ref/man5/gfarm2.conf.5.html
%{html_prefix}/ja/ref/man5/gfarm_attr.5.html
%{html_prefix}/ja/ref/man5/gfservice.conf.5.html
%{html_prefix}/ja/ref/man7/gfarm_environ.7.html
%{html_prefix}/ja/ref/man8/gfdump.postgresql.8.html
%{html_prefix}/ja/ref/man8/gfmd.8.html
%{html_prefix}/ja/ref/man8/gfsd.8.html
%{html_prefix}/ja/ref/man8/config-gfarm-update.8.html
%{html_prefix}/ja/ref/man8/config-gfarm.8.html
%{html_prefix}/ja/ref/man8/config-gfsd.8.html
%{html_prefix}/ja/user/index.html
%{html_prefix}/ja/user/samba-gfarmfs.html
%if %{gfarm_v2_not_yet}
%{html_prefix}/ja/user/export-gfarm.html
%{html_prefix}/ja/user/nfs-gfarmfs.html
%endif
%{html_prefix}/ja/user/smboverssh.html
%{html_prefix}/ja/user/redundancy-tutorial.html
%{html_prefix}/ja/user/cipher-comparison.html
%{html_prefix}/pic/gfarm-logo.gif
%{doc_prefix}/INSTALL.en
%{doc_prefix}/INSTALL.ja
%{doc_prefix}/INSTALL.RPM.en
%{doc_prefix}/INSTALL.RPM.ja
%{doc_prefix}/INSTALL.Debian.en
%{doc_prefix}/INSTALL.Debian.ja
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
%{doc_prefix}/gfperf/CONFIG-gfperf.en
%{doc_prefix}/gfperf/README-gfperf.en
%{doc_prefix}/gfperf/SETUP-gfperf.en
%{doc_prefix}/gfperf/SUPPORT-gfperf.en
%{doc_prefix}/gfperf/USING-gfperf.en
%{doc_prefix}/gfperf/CONFIG-gfperf.ja
%{doc_prefix}/gfperf/README-gfperf.ja
%{doc_prefix}/gfperf/SETUP-gfperf.ja
%{doc_prefix}/gfperf/SUPPORT-gfperf.ja
%{doc_prefix}/gfperf/USING-gfperf.ja

%files libs
%defattr(-,root,root)
%{lib_prefix}/libgfarm.so.1
%{lib_prefix}/libgfarm.so.1.0.0
%{lib_prefix}/libgfperf.so.1
%{lib_prefix}/libgfperf.so.1.0.0
%dir %{share_prefix}
%dir %{share_prefix}/config
%{share_prefix}/config/config-gfarm.sysdep
%{share_prefix}/config/config-gfarm.common

%files client
%defattr(-,root,root)
%{prefix}/bin/gfarm-pcp
%{prefix}/bin/gfarm-prun
%{prefix}/bin/gfarm-ptool
%{prefix}/bin/gfdf
%{prefix}/bin/gfchgrp
%{prefix}/bin/gfchmod
%{prefix}/bin/gfchown
%{prefix}/bin/gfcksum
%{prefix}/bin/gfdirpath
%{prefix}/bin/gfdirquota
%{prefix}/bin/gfedquota
%{prefix}/bin/gfexport
%{prefix}/bin/gffilepath
%{prefix}/bin/gffindxmlattr
%{prefix}/bin/gfgetfacl
%{prefix}/bin/gfgroup
%{prefix}/bin/gfhost
%{prefix}/bin/gfhostgroup
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfifo.sh
%{prefix}/bin/gfimport_fixed
%{prefix}/bin/gfimport_text
%endif
%{prefix}/bin/gfkey
%{prefix}/bin/gfln
%{prefix}/bin/gfls
%{prefix}/bin/gflsof
%{prefix}/bin/gfmdhost
%{prefix}/bin/gfmkdir
%{prefix}/bin/gfmv
%{prefix}/bin/gfncopy
%{prefix}/bin/gfpcopy
%{prefix}/bin/gfprep
%{prefix}/bin/gfpcopy-test.sh
%{prefix}/bin/gfpcopy-stress
%{prefix}/bin/gfpath
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfps
%{prefix}/bin/gfpwd
%{prefix}/bin/gfq.sh
%{prefix}/bin/gfq_commit.sh
%{prefix}/bin/gfq_setup.sh
%{prefix}/bin/gfrcmd
%endif
%{prefix}/bin/gfquota
%{prefix}/bin/gfquotacheck
%{prefix}/bin/gfreg
%{prefix}/bin/gfrep
%{prefix}/bin/gfrepcheck
%{prefix}/bin/gfrm
%{prefix}/bin/gfrmdir
%{prefix}/bin/gfruntest
%{prefix}/bin/gfusage
%{prefix}/bin/gfuser
%{prefix}/bin/gfsched
%{prefix}/bin/gfsetfacl
%{libexec_prefix}/gfs_pio_test
%if %{gfarm_v2_not_yet}
%{prefix}/bin/gfrsh
%{prefix}/bin/gfrshl
%{prefix}/bin/gfrun
%{prefix}/bin/gfsck
%{prefix}/bin/gfsetdir
%{prefix}/bin/gfssh
%{prefix}/bin/gfsshl
%endif
%{prefix}/bin/gfservice
%{prefix}/bin/gfservice-agent
%{prefix}/bin/gfservice-timeout
%{prefix}/bin/gfspoolgen
%{prefix}/bin/gfspoolinum
%{prefix}/bin/gfstat
%{prefix}/bin/gfstatus
%{prefix}/bin/gfsudo
%{prefix}/bin/gftest
%{prefix}/bin/gfwhere
%{prefix}/bin/gfwhoami
%{prefix}/bin/gfxattr
%if %{gfarm_v2_not_yet}
%{profile_prefix}/gfarm.sh
%{profile_prefix}/gfarm.csh
%endif

%dir %{share_prefix}
%{share_prefix}/gfservice
%{share_prefix}/systest

%{prefix}/bin/gfcreate-test
%{prefix}/bin/gfperf-autoreplica
%{prefix}/bin/gfperf-copy
%{prefix}/bin/gfperf-metadata
%{prefix}/bin/gfperf-parallel-autoreplica
%{prefix}/bin/gfperf-parallel-read
%{prefix}/bin/gfperf-parallel-replica
%{prefix}/bin/gfperf-parallel-write
%{prefix}/bin/gfperf-read
%{prefix}/bin/gfperf-replica
%{prefix}/bin/gfperf-tree
%{prefix}/bin/gfperf-wrapper.sh
%{prefix}/bin/gfperf-write
%{prefix}/bin/gfperf.rb
%{prefix}/bin/gfstress.rb
%{prefix}/bin/gfiops
%dir %{share_prefix}/config
%{share_prefix}/config/gfperf-config.yml
%{share_prefix}/config/gfperf-simple.yml
%dir %{share_prefix}/gfperf-web
%{share_prefix}/gfperf-web/config.php
%{share_prefix}/gfperf-web/config_view.php
%{share_prefix}/gfperf-web/gnuplot.php
%{share_prefix}/gfperf-web/graph_draw.php
%{share_prefix}/gfperf-web/graph_page.php
%{share_prefix}/gfperf-web/index.php
%{share_prefix}/gfperf-web/view_error.php
%{share_prefix}/gfperf-web/view_result.php

%files fsnode
%defattr(-,root,root)
%{prefix}/bin/config-gfsd
%{prefix}/bin/gfarm.arch.guess
%{prefix}/bin/gfspooldigest
%{prefix}/bin/gfspoolmd5
%{prefix}/bin/gfspoolpath
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
%{share_prefix}/config/linux/systemd/gfsd.service.in

%files ganglia
%defattr(-,root,root)
%{prefix}/bin/config-gfmd-iostat
%{prefix}/bin/config-gfsd-iostat
%dir %{share_prefix}
%dir %{share_prefix}/config
%{share_prefix}/config/config-gfarm.iostat
%{share_prefix}/ganglia

%files server
%defattr(-,root,root)
%{prefix}/sbin/gfmd
%{prefix}/bin/config-gfarm
%{prefix}/bin/config-gfarm-update
%{prefix}/bin/gfdump.postgresql
%{prefix}/bin/gfjournal
%{prefix}/bin/gfjournaladmin
%{prefix}/bin/gfjournaldump
%dir %{share_prefix}
%dir %{share_prefix}/config
%{share_prefix}/config/bdb.DB_CONFIG.in
%{share_prefix}/config/config-gfarm.ldap
%{share_prefix}/config/config-gfarm.postgresql
%{share_prefix}/config/config-gfarm-update.postgresql
%{share_prefix}/config/config-gfarm-update.ldap
%{share_prefix}/config/gfarm.conf-ldap.in
%{share_prefix}/config/gfarm.conf-postgresql.in
%{share_prefix}/config/gfarm.conf.in
%{share_prefix}/config/gfarm.sql
%{share_prefix}/config/gfarm-xmlattr.sql
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
%{share_prefix}/config/linux/systemd/gfarm-pgsql.service.in
%{share_prefix}/config/linux/systemd/gfmd.service.in
%{share_prefix}/config/slapd.conf-2.0.in
%{share_prefix}/config/slapd.conf-2.1.in
%{share_prefix}/config/unconfig-gfarm.sh.in
%{share_prefix}/config/unconfig-gfsd.sh.in
%{share_prefix}/ruby/gfcrc32.rb
%{share_prefix}/ruby/gfjournalfile.rb

%files devel
%defattr(-,root,root)
%{prefix}/include/gfarm/gfarm.h
%{prefix}/include/gfarm/gfarm_config.h
%{prefix}/include/gfarm/error.h
%{prefix}/include/gfarm/gfarm_misc.h
%{prefix}/include/gfarm/gfarm_stringlist.h
%{prefix}/include/gfarm/gflog.h
%{prefix}/include/gfarm/host_info.h
%{prefix}/include/gfarm/user_info.h
%{prefix}/include/gfarm/group_info.h
%{prefix}/include/gfarm/gfs.h
%{prefix}/include/gfarm/gfs_glob.h
# XXX - this should not be here
%{prefix}/include/gfarm/gfarm_msg_enums.h
%{lib_prefix}/libgfarm.a
%{lib_prefix}/libgfarm.la
%{lib_prefix}/libgfarm.so
%{lib_prefix}/libgfperf.a
%{lib_prefix}/libgfperf.la
%{lib_prefix}/libgfperf.so
