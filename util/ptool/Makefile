top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SCRIPTS= pcat pcp pdel pdist prun

all:

install:
	@for i in / $(SCRIPTS); do \
		case $$i in /) continue;; esac; \
		( set -x; $(INSTALL_DATA) $(srcdir)/$${i} $(bindir)/$${i} ); \
	done

clean:
veryclean: clean
distclean: veryclean
gfregister:
man:
html:
