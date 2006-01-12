top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

MPI_TARGETS = gfwc

SUBDIRS = \
	gfcombine \
	gfcombine_hook \
	gfcp \
	gfcp_hook \
	gfgrep \
	gfrepbe_client \
	gfrepbe_server \
	gfsplck \
	$(mpi_targets) \
	util

include $(top_srcdir)/makes/subdir.mk
