msgno: assign_msgno

ASSIGNMSGNO=$(top_srcdir)/makes/assign_msgno.pl

assign_msgno:
	GFARM_TOPDIR=$(top_srcdir) $(ASSIGNMSGNO) $(SRCS);
