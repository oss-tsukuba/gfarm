# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SUBDIRS= sysdep/$(config_gfarm_sysdep_subdir)

datadir=$(default_datadir)/gfarm/config

PRIVATE_SRCS = $(PRIVATE_GFTOOL_CONFIG_GFARM_SRCS)

SCRIPTS = $(srcdir)/gfarm.arch.guess $(optional_srcs)
BUILT_SCRIPTS = config-gfarm config-gfarm-update config-gfsd \
	config-gfmd-iostat config-gfsd-iostat

DATA =	$(srcdir)/config-gfarm.common \
	$(srcdir)/config-gfarm.postgresql \
	$(srcdir)/config-gfarm.ldap \
	$(srcdir)/config-gfarm.iostat \
	$(srcdir)/config-gfarm-update.postgresql \
	$(srcdir)/config-gfarm-update.ldap \
	$(srcdir)/unconfig-gfarm.sh.in \
	$(srcdir)/unconfig-gfsd.sh.in \
	$(srcdir)/gfarm.conf-postgresql.in \
	$(srcdir)/gfarm.conf-ldap.in \
	$(srcdir)/gfarm.conf.in \
	$(srcdir)/slapd.conf-2.0.in \
	$(srcdir)/slapd.conf-2.1.in \
	$(srcdir)/gfarm.sql \
	$(srcdir)/gfarm-xmlattr.sql \
	$(srcdir)/gfarm.schema \
	$(srcdir)/initial.ldif.in \
	$(srcdir)/bdb.DB_CONFIG.in

include $(top_srcdir)/makes/script.mk
include $(top_srcdir)/makes/data.mk
include $(top_srcdir)/makes/subdir.mk

config-gfarm: $(srcdir)/config-gfarm.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@

config-gfarm-update: $(srcdir)/config-gfarm-update.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@

config-gfsd: $(srcdir)/config-gfsd.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@

config-gfmd-iostat: $(srcdir)/config-gfmd-iostat.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@

config-gfsd-iostat: $(srcdir)/config-gfsd-iostat.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
