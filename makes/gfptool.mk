gfregister: prog-gfregister gfptool-gfregister post-gfregister-hook

include $(top_srcdir)/makes/prog.mk

gfptool-gfregister: $(PROGRAM)
	-gfreg $(PROGRAM) gfarm:/bin/$(PROGRAM)
