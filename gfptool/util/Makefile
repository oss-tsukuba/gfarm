top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

# gfregister is an administration command.
bindir = $(sbindir)

SCRIPTS= gfregister

all:

install:
	@for i in / $(SCRIPTS); do \
		case $$i in /) continue;; esac; \
		( set -x; $(INSTALL_PROGRAM) $${i} $(bindir)/$${i} ); \
	done

clean:
veryclean: clean
distclean: veryclean
	-rm -f $(SCRIPTS)

gfregister:
man:
html:
