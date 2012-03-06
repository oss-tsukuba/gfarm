# $Id: Makefile 2533 2006-03-31 10:50:56Z takuya $

top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

datadir=$(default_datadir)/gfarm/config

DATA = $(srcdir)/gfperf-config.yml \
       $(srcdir)/gfperf-simple.yml

SCRIPTS = $(srcdir)/gfperf.rb

include $(top_srcdir)/makes/script.mk
include $(top_srcdir)/makes/data.mk
