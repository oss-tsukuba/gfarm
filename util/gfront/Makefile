top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .
javadir = $(prefix)/share/java

include $(top_srcdir)/makes/var.mk

SCRIPTS= gfront
JAR= gfront.jar

all:

install:
	@for i in / $(SCRIPTS); do \
		case $$i in /) continue;; esac; \
		( set -x; $(INSTALL_PROGRAM) $(srcdir)/$${i} $(bindir)/$${i} ); \
	done
	@for i in / $(JAR); do \
		case $$i in /) continue;; esac; \
		( set -x; $(INSTALL_DATA) $(srcdir)/$${i} $(javadir)/$${i} ); \
	done

clean:
veryclean: clean
distclean: veryclean
	-rm -f gfront

gfregister:
man:
html:
