msgno: assign_msgno

assign_msgno:
	GFARM_TOPDIR=$(top_srcdir) $(ASSIGNMSGNO) $(SRCS);
