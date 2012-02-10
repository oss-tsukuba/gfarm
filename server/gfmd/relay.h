struct relayed_reqeust;

gfarm_error_t relay_put_request(struct relayed_reqeust **, const char *,
	gfarm_int32_t, const char *, ...);
gfarm_error_t relay_get_reply(struct relayed_reqeust *, const char *,
	gfarm_error_t *, const char *, ...);
