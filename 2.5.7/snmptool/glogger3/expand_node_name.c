#include <stdio.h>
#include <string.h>
#include <stdlib.h>
//#include <ctype.h>

#include "expand_node_name.h"

//#define DEBUG_RANGE


#if 0
int check_fig(int i, int base){
  char f[MAXFIGURE+2];
  int fig;

  if(base == 16){
    snprintf(f, MAXFIGURE+2, "%x", i);
  }
  else {
    snprintf(f, MAXFIGURE+2, "%d", i);
  }

  f[MAXFIGURE+1] = '\0';
  fig = strlen(f);

#ifdef DEBUG_RANGE
  printf("figure = %d (%s)\n", fig, f);
#endif

  if(fig > MAXFIGURE){
    return 0;
  }
  return fig;
}
#endif

/* [?-?] */
char * expand_range(char *expression, struct range *r){
  char *p;
  char *e = expression;
  int i;
  //int checkfig;

  r->min = 0;
  r->max = 0;
  r->figure = 0;
  r->base = 10;

  if(*e == '['){
    e++;
  }
  if(*e == '0' && (*(e+1) == 'x' || *(e+1) == 'X')){
    r->base = 16;
  }

  /* min */
  r->min = strtoul(e, &p, r->base);

#ifdef DEBUG_RANGE
  printf("min = %d, p = %s\n", r->min, p);
#endif

  if(*p != '-'){
    r->figure = 0;
    printf("Error: not use '-' after min\n");
    return NULL;
  }
  /* set figure from min figure */
  i = 0;
  while(*e){
    if(*e == '-'){
      e++;
      break;
    }
    i++;
    e++;
  }
  if(r->base == 16){
    i -= 2;
  }
  r->figure = i;

  if(*e == '0' && (*(e+1) == 'x' || *(e+1) == 'X')){
    if(r->base != 16){
      r->figure = 0;
      printf("Error: min: decimal, max: hexa\n");
      return NULL;
    }
  }
  else {
   if(r->base == 16){
      r->figure = 0;
      printf("Error: min: hexa, max: decimal\n");
      return NULL;
    }
  }
  //checkfig = check_fig(r->min, r->base);

  /* max */
  r->max = strtoul(e, &p, r->base);

#ifdef DEBUG_RANGE
  printf("max = %d, p = %s\n", r->max, p);
#endif

  /* check */
  if(*p != ']'){
    r->figure = 0;
    printf("Error: not closed ']'\n");
    return NULL;
  }
  if(r->min < 0 || r->max < 0){
    r->figure = 0;
    printf("Error: can't use minus sign\n");
    return NULL;
  }
  if(r->min >= r->max){
    r->figure = 0;
    printf("Error: min > max\n");
    return NULL;
  }

#if 0
  r->figure = check_fig(r->max, r->base);
  if(r->figure == 0){
    printf("Error: over MAXFIGURE(=%d)\n", MAXFIGURE);
  }
#endif

  return p;

#if 0
  if(checkfig != r->figure){
    r->figure = 0;
    return;
  }
#endif

}

char * replace_nick_val(char *nickdef, char **rep, int deep){
  char *c, *p;
  char *ret;

  if(nickdef == NULL){
    return NULL;
  }
  if(strlen(nickdef) <= 0){
    return NULL;
  }

  if(deep > MAXREPLACE){
    printf("Error: can't replace nick name");
    exit(2);
  }

  ret = (char*) calloc(MAXNICKNAME, sizeof(char));
  if(ret == NULL){
    printf("Error: calloc()\n"); exit(2);
  }

  c = nickdef;
  p = ret;
  while(*c){
    if(*c == '$'){
      c++;
      if(*c - 48 > deep){
	printf("Error: cannot replace nickname\n");
	exit(2);
      }
      if(*c > 48 && *c < 48 + MAXREPLACE){
	//printf("c = %d\n", *c - 48);
	strcat(p, rep[*c-49]);
	p = p + strlen(rep[*c-49]);
      }
      else {
	printf("Error: node nickname: $1...$%d\n", MAXREPLACE);
	exit(2);
      }
    }
    else {
      *p = *c;
      p++;
    }
    c++;
  }
  return ret;
}


EXNODES * add_list(EXNODES *to, EXNODES *from){
  EXNODES *top;

  if(to == NULL){
    return from;
  }
#if 0
  else {
    top = to;
    while(to->next != NULL){
      to = to->next;
    }
    to->next = from;
    return top;
  }
#else
  else {
    top = from;
    while(from->next != NULL){
      from = from->next;
    }
    from->next = to;
    return top;    
  }
#endif

}

void print_list(EXNODES *list){
  while(list){
    printf("name=%s, nick=%s.\n", list->name, list->nick);
    list = list->next;
  }
}

void exfree(char *c){
  if(c != NULL){
    free(c);
  }
}

void free_list(EXNODES *list){
  char *tmp;
  while(list){
    exfree(list->name);
    exfree(list->nick);
    tmp = (char*) list;
    list = list->next;
    exfree(tmp);
  }
}


#define MAXFORM 32

EXNODES * expand_node_name(char *nodedef, char *nickdef,
			   char **rep, int deep){
  char *c, *p;
  char *tmpname, *tmp;
  char newname[MAXNODENAME];
  struct range r;
  int i;
  char format[MAXFORM];

  EXNODES *newlist, *list;

  if(deep > MAXREPLACE){
    printf("Error: over MAXREPLACE(%d)\n", MAXREPLACE);
    exit(2);
  }
  if(strlen(nodedef) == 0){
    printf("Error: node definition length = 0\n");
    exit(2);
  }
  
  tmpname = (char*) calloc(strlen(nodedef)+1, sizeof(char));
  if(tmpname == NULL){
    printf("Error: calloc()\n"); exit(2);
  }

  list = NULL;
  c = nodedef;
  tmp = tmpname;
  while(*c){
    /* ex. [00-99] */

    if(*c == '['){
      p = expand_range(c, &r);
      if(p == NULL){
	//return NULL;
	exit(2);
      }
      p++;
      for(i=r.min; i<=r.max; i++){
	if(r.base == 16){
	  snprintf(format, MAXFORM, "%%.%dx", r.figure);
	}
	else {
	  snprintf(format, MAXFORM, "%%.%dd", r.figure);
	}

	snprintf(rep[deep], MAXFIGURE+1, format, i);

	//printf("*** %s%s%s\n", tmpname, rep[deep], p);

	snprintf(newname, MAXNODENAME, "%s%s%s", tmpname, rep[deep], p);

	/* recursive call */
	newlist = expand_node_name(newname, nickdef, rep, deep+1);
	list = add_list(list, newlist);
      }
      free(tmpname);
      return list;
    }
    else {
      *tmp = *c;
      tmp++;
    }
    c++;
  }

  /* no more [?-?] */
  {
    EXNODES * ret;
    char *newnick;

    ret = (EXNODES*) calloc(1, sizeof(EXNODES));
    if(ret == NULL){
      printf("Error: calloc()\n"); exit(2);
    }
    ret->name = tmpname;

    newnick = replace_nick_val(nickdef, rep, deep);
    if(newnick == NULL){
      ret->nick = ret->name;
    }
    else {
      ret->nick = newnick;
    }
    ret->next = NULL;

    //printf("name=%s, nick=%s.\n", ret->name, ret->nick);
    return ret;
  }
}


#ifdef testmain

/* main for test */
int main(int argc, char **argv){
  //char *nodedef = "gfm[01-80]-[0x00-0xFF].apgrid.org";
  char *nodedef = "172.19.2.[1-255]";
  //char *nickdef = "gfm$2-$1";
  char *nickdef = NULL;
  int i;

#if 0
  {
    struct range r;
    char format[32];
  
    /* test: expand_range() */
    expand_range(argv[1], &r);
    if(r.figure == 0){
      printf("Syntax Error\n");
    }
    else if(r.base == 16){
      snprintf(format, 32, "min=%%.%dx, max=%%.%dx\n", r.figure, r.figure);
      printf(format, r.min, r.max);
    }
    else {
      snprintf(format, 32, "min=%%.%dd, max=%%.%dd\n", r.figure, r.figure);
      printf(format, r.min, r.max);
    }
    for(i=r.min; i<=r.max; i++){
      if(r.base == 16){
	snprintf(format, 32, "%%.%dx ", r.figure);
      }
      else {
	snprintf(format, 32, "%%.%dd ", r.figure);
      }
      printf(format, i);
    }
    printf("\n");
  }
#endif

#if 1  
  {
    /* test: expand_nodes_name() */
    char *rep[MAXREPLACE];
    EXNODES *top;
    
    if(argc >= 2){
      nodedef = argv[1];
    }
    if(argc >= 3){
      nickdef = argv[2];
    }

    for(i=0; i<MAXREPLACE; i++){
      rep[i] = (char*) calloc(MAXFIGURE+1, sizeof(char));
      if(rep[i] == NULL){
	printf("Error: calloc()\n");
	exit(2);
      }
    }
    top = expand_node_name(nodedef, nickdef, rep, 0);  
    print_list(top);
    free_list(top);
  }
#endif

  return 0;
}

#endif
