#define MAXNODENAME 256
#define MAXNICKNAME 64
#define MAXNODES 1024
#define MAXFIGURE 5
#define MAXREPLACE 5

struct range {
  int min;
  int max;
  int figure;
  int base;	   /* decimal (10 or 16) */
};

struct expanded_nodelist {
  char *name;
  char *nick;
  struct expanded_nodelist *next;
};

typedef struct expanded_nodelist EXNODES;

EXNODES * expand_node_name(char *nodedef, char *nickdef, char **rep, int deep);
