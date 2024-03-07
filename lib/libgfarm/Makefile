# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

GLOBUS_TARGETS = gfsl_gsi
KERBEROS_TARGETS = gfsl_kerberos
SUBDIRS = gfutil $(globus_targets) $(kerberos_targets) gfarm

all: subdir-all

include $(top_srcdir)/makes/subdir.mk
