top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

all: gfsk

gfsk: $(srcdir)/gfsk.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@

install: gfsk
	cp gfsk /etc/init.d/gfsk

clean:
	rm -f gfsk

distclean: clean
