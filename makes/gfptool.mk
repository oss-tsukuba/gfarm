include $(top_srcdir)/makes/prog.mk

gfregister: $(PROGRAM)
	-gfreg $(PROGRAM) gfarm:/bin/$(PROGRAM)
