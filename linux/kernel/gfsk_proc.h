struct proc_dir_entry;
#define GFSK_PROC_NAME	31
struct gfsk_procop;
struct gfsk_procop {
	char	*name;
	int	mode;
	int	(*read)(char *, int, loff_t, struct gfsk_procop*);
	int	(*write)(char *, int, loff_t, struct gfsk_procop*);
	struct proc_dir_entry *pde;
	void	*ctx;
};
#define GFSK_PDE_INT	1
#define GFSK_PDE_LONG	2
#define GFSK_PDE_STR	4
struct gfsk_procvar {
	char	*name;
	int	mode;
	int	type;
	char	*fmt;
	void	*addr;
	struct proc_dir_entry *pde;
};
int gfarm_proc_init(void);
void gfarm_proc_fini(void);
int gfarm_procvar_create(struct proc_dir_entry *parent,
			struct gfsk_procvar *vars, int num);
void gfarm_procvar_remove(struct proc_dir_entry *parent,
			struct gfsk_procvar *vars, int num);
int gfarm_procop_create(struct proc_dir_entry *parent, struct gfsk_procop *op);
void gfarm_procop_remove(struct proc_dir_entry *parent, struct gfsk_procop *op);
struct proc_dir_entry *gfarm_proc_mkdir(struct proc_dir_entry *parent,
		char *buf);
void gfarm_proc_rmdir(struct proc_dir_entry *parent, char *buf);


