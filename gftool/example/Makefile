top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

MPI_SUBDIRS = gfwc
SUBDIRS = \
	gfimport_fixed \
	gfimport_text \
	gfgrep \
	$(mpi_subdirs)

include $(top_srcdir)/makes/subdir.mk
