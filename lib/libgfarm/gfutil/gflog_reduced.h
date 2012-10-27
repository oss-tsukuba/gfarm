struct gflog_reduced_state {
	int reduced_mode;
	time_t stat_start, log_time;
	long stat_count, log_count;

	/* configuration constants per each log type */
	int trigger; /* check reduced mode, if count exceeds this */
	int threshold; /* reduce, if rate exceeds threshold/duration */
	int duration; /* seconds: see above */
	int log_interval; /* seconds: interval of reduced log */
};

#define GFLOG_REDUCED_STATE_INITIALIZER( \
	trigger, threshold, duration, log_interval) \
	{ \
		0, 0, 0, 0, 0, \
		trigger, threshold, duration, log_interval \
	}

void gflog_reduced_message(int, int, const char *, int, const char *,
	struct gflog_reduced_state *,
	const char *, ...) GFLOG_PRINTF_ARG(7, 8);

#define gflog_reduced_error(msg_no, state, ...)	\
	gflog_reduced_message(msg_no, LOG_ERR,\
	    __FILE__, __LINE__, __func__, state, __VA_ARGS__)
#define gflog_reduced_warning(msg_no, state, ...)	\
	gflog_reduced_message(msg_no, LOG_WARNING,\
	    __FILE__, __LINE__, __func__, state, __VA_ARGS__)
#define gflog_reduced_notice(msg_no, state, ...)	\
	gflog_reduced_message(msg_no, LOG_NOTICE,\
	    __FILE__, __LINE__, __func__, state, __VA_ARGS__)
#define gflog_reduced_info(msg_no, state, ...)	\
	gflog_reduced_message(msg_no, LOG_INFO,\
	    __FILE__, __LINE__, __func__, state, __VA_ARGS__)
#define gflog_reduced_debug(msg_no, state, ...)	\
	gflog_reduced_message(msg_no, LOG_DEBUG,\
	    __FILE__, __LINE__, __func__, state, __VA_ARGS__)
