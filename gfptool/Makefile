top_objdir = ..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

MPI_SUBDIRS = gfwc

SUBDIRS = \
	gfcombine \
	gfcombine_hook \
	gfcp \
	gfcp_hook \
	gfgrep \
	gfrepbe_client \
	gfrepbe_server \
	gfsplck \
	$(mpi_subdirs) \
	util

include $(top_srcdir)/makes/subdir.mk
