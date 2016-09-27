struct gfcc_stack {
	int	s_size;
	int	s_num;
	int	s_free;
	int	s_waiting;
	void	*s_data;
	void	**s_stack;
	int	s_anum;
	int	s_ause;
	void	**s_alloc;
	GFCC_MUTEX_T	s_lock;
	GFCC_COND_T	s_wait;
};
int gfcc_stack_init(struct gfcc_stack *, int , int , void *, int);
void gfcc_stack_fini(struct gfcc_stack *stp, void (*dtr)(void *));
void *gfcc_stack_get(struct gfcc_stack *stp, int nowait);
int gfcc_stack_put(void *data, struct gfcc_stack *stp);
