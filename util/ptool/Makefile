top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SCRIPTS= pcat pcp pdel pdist prun

all:

install:
	@for i in / $(SCRIPTS); do \
		case $$i in /) continue;; esac; \
		( set -x; $(INSTALL_PROGRAM) $(srcdir)/$${i} $(bindir)/$${i} ); \
	done

clean:
veryclean: clean
distclean: veryclean
gfregister:
man:
html:
