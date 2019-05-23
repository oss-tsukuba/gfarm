/*
 * $Id$
 */

/*
 * fsngroup := [-_0-9A-Za-z]*
 *
 * fsnset := fsngroup | fsngroup '+' fsnset
 *
 * repplace := fsnset ':' number
 *
 * repspec := repplace | repplace ',' repspec
 */

struct repspec; /* abstact syntax struct of repattr */
struct repplace;

gfarm_error_t repattr_parse_to_repspec(const char *, struct repspec **);
void repspec_free(struct repspec *);
int repspec_get_total_amount(struct repspec *);
gfarm_error_t repspec_validate(struct repspec *);
gfarm_error_t repspec_normalize(struct repspec *);
gfarm_error_t repspec_to_string(struct repspec *, char **);

int repspec_get_repplace_number(struct repspec *);
struct repplace *repspec_get_repplace(struct repspec *, int);

int repplace_get_fsngroup_number(struct repplace *);
const char *repplace_get_fsngroup(struct repplace *, int);
int repplace_get_amount(struct repplace *);
