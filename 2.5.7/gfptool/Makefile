top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

MPI_TARGETS = gfwc
GFSHOOK_TARGETS = gfcombine_hook gfcp_hook

SUBDIRS = \
	gfcombine \
	gfcp \
	gfgrep \
	gfsplck \
	$(mpi_targets) \
	util \
	$(gfshook_targets)

include $(top_srcdir)/makes/subdir.mk
