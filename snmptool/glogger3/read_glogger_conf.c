#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>

#include "glogger.h"
#include "expand_node_name.h"

//extern char *global_defaultdir;

//#define DEBUG_READCONF

/* separate line */
struct separate {
  char *a;			/* key */
  char *b;			/* value */
};

/* --------------------------------------------------------------------- */
struct nodelist *topnodes, *nownodes;

struct timeval global_interval;
char global_defaultdir[MAXDEFAULTDIRLEN];
int global_outputsizemax;   	/* KiloBytes: *1024 */
//struct timeval global_fileinterval; /* sec */

char global_overwrite[MAXOVERWRITELEN]; /* ON, OFF, enable, disable */
char global_label[MAXLABELLEN];
char global_path[MAXPATH];
char global_community[MAXCOMMLEN];
char global_fsync[MAXFSYNCLEN]; /* (number), on, off, enable, disable */
int global_fsyncmode;

struct timeval requests_interval;
char requests_path[MAXPATH];
char requests_community[MAXCOMMLEN];

struct timeval mib_interval;
char mib_path[MAXPATH];

char *community_for_set;

struct tm now_tm;
struct timeval nowtime;

int linenum = 0;
int mode_global = 1;
int mode_requests = 0;
int mode_nodes = 0;
int mode_mib = 0;
int mode_oids = 0;
int ok_nodes = 0;
int ok_mib = 0;

/* --------------------------------------------------------------------- */
#define GLOBAL_KEY_NUMBER 10
char *global_keys[GLOBAL_KEY_NUMBER] = {
  KEYSEC,
  KEYMICROSEC,
  KEYINTERVAL,
  KEYOUTMAX,
  KEYOVERWRITE,
  KEYDEFAULTDIR,
  KEYLABEL,
  KEYPATH,
  KEYCOMMUNITY,
  KEYFSYNC
};

#define REQUESTS_KEY_NUMBER 5
char *requests_keys[REQUESTS_KEY_NUMBER] = {
  KEYSEC,
  KEYMICROSEC,
  KEYINTERVAL,
  KEYPATH,
  KEYCOMMUNITY
};

#define MIB_KEY_NUMBER 4
char *mib_keys[MIB_KEY_NUMBER] = {
  KEYSEC,
  KEYMICROSEC,
  KEYINTERVAL,
  KEYPATH,
};

/* --------------------------------------------------------------------- */
#if 0
static void myfree(char **c){
  if(*c != NULL){
    free(*c);
  }
  *c =  NULL;
}
#endif

/* default setting */
void init_global_conf(void){
  global_interval.tv_sec =  DEFAULTSEC;
  global_interval.tv_usec = DEFAULTUSEC;
  strcpy(global_path, DEFAULTPATH);
  strcpy(global_community, DEFAULTCOMM);
  strcpy(global_defaultdir, DEFAULTDIR);
  //global_outputsizemax = DEFAULTOUTPUT;
  strcpy(global_overwrite, DEFAULTOVERWRITE);
  strcpy(global_label, DEFAULTLABEL);
  //strcpy(global_fsync, DEFAULTFSYNC);
  global_fsyncmode = DEFAULTFSYNCMODE;
}

/* override */
void init_requests_conf(void){
  requests_interval = global_interval;
  strncpy(requests_path, global_path, MAXPATH);
  strncpy(requests_community, global_community, MAXCOMMLEN);
}

void init_mib_conf(void){
  mib_interval = requests_interval;
  strncpy(mib_path, requests_path, MAXPATH);
}

/* all initialize */
void init_read_conf_all(void){
  linenum = 0;
  mode_global = 1;
  mode_requests = 0;
  mode_nodes = 0;
  mode_mib = 0;
  mode_oids = 0;
  ok_nodes = 0;
  ok_mib = 0;

  init_global_conf();
  init_requests_conf();
  init_mib_conf();
  
  topnodes = NULL;
  nownodes = NULL;
}

/* --------------------------------------------------------------------- */
void debugstring(char *l){
  int i = 0;
  printf("*** ");
  while(i < 10){
    printf("%c ", l[i]);
    i++;
  }
  printf("***\n");
}

/* use after  cut_comment(), cut_front_spaces() */

/* AAA  BBB  ->  a = AAA, b = BBB */
int separate_line(char *line, struct separate *s){
  char *l;

  //debugstring(line);
  s->a = NULL;
  s->b = NULL;

  /* a */
  l = line;
  s->a = l;
  while(*l != ' ' && *l != '=' && *l != '\0' && *l != '\r' && *l != '\n'){
    l++;
  }
  if(*l == '\0' || *l == '\r' || *l == '\n'){  /* ex. only "}" */
    *l = '\0';
    return 1;			/* one word */
  }
  *l = '\0';
  l++;
  //debugstring(l);

  while(*l == ' ' || *l == '='){ /* skip spaces */
    l++;
  }
  if(*l == '\0' || *l == '\r' || *l == '\n'){  /* no value */
    *l = '\0';
    return 1;			/* one word */
  }

  /* b */
  s->b = l;

  /* cut */
  while(*l != ' ' && *l != '=' && *l != '\0' && *l != '\r' && *l != '\n'){
    l++;
  }
  if(*l == '\0'){
    return 2;			/* two words */
  }
  *l = '\0';
  l++;

  /* check */
  while(*l == ' '){
    l++;
  }
  if(*l == '\0'){
    return 2;			/* two words */
  }

  s->a = NULL;
  s->b = NULL;
  return 0;			/* etc.... */
}

/* compare permitted keys */
int check_keys(char *keynow, char **keys, int keysnum){
  int i = 0;
  while(i < keysnum){
    if(strcmp(keynow, keys[i]) == 0){
      return 1;
    }
    i++;
  }
  return 0;
}

int check_close_mode(char *l){
  if(*l == '}'){
    return 1;
  }
  else {
    return 0;
  }
}

/* ---------------------------------------------------------------------- */
#define TMPLEN 64
char * convert_path(struct nodelist *nodes, struct oidlist *oids,
		    char *rule){
  //const char *format, char *dst, int maxpath){
  char *f;
  char *dst;
  int i;
  char tmp[TMPLEN];

  dst = (char*) calloc(MAXPATH, sizeof(char));
  if(dst == NULL){
    exit(2);
  }

  f = rule;
  while(*f != '\0'){
    if(MAXPATH <= strlen(dst)){
      printf("Error: path name length >= %d(MAXPATH)\n", MAXPATH);
      exit(2);
      //break;
    }
    if(*f == '%'){
      f++;
      switch(*f){ 
      case 'G': 
	strncat(dst, global_defaultdir, MAXPATH - strlen(dst));
	break;
      case 'L':
	strncat(dst, global_label, MAXPATH - strlen(dst));
	break;
      case 'n':
	strncat(dst, nodes->nodename, MAXPATH - strlen(dst));
	break;
      case 'N':
	strncat(dst, nodes->nodenick, MAXPATH - strlen(dst));
	break;
      case 'o':
	strncat(dst, oids->oidname, MAXPATH - strlen(dst));
	break;
      case 'O':
	strncat(dst, oids->oidnick, MAXPATH - strlen(dst));
	break;
      case 't':
      case 'c':
      case 's':
      case 'S':
      case 'M':
      case 'h':
      case 'd':
      case 'j':
      case 'm':
      case 'y':
	snprintf(tmp, TMPLEN, "%%%c", *f);
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
#if 0
      case '%':
	i = strlen(dst);
	dst[i] = '%';
	dst[i+1] = '\0';
#endif
      }
      /* else */
      /* do nothing */
    }
    else {
      i = strlen(dst);
      dst[i] = *f;
      dst[i+1] = '\0';
    }
    f++;
  }
  return dst;
}

/* ---------------------------------------------------------------------- */
int set_global_value(char *key, char *val){
  double f;

  if(strcmp(key, KEYPATH) == 0){
    if(strlen(val) >= MAXPATH){
      printf("Error: path length >= %d\n", MAXPATH);
      exit(2);
    }
    strncpy(global_path, val, MAXPATH);
  }
  else {
    if(strcmp(key, KEYSEC) == 0){
      global_interval.tv_sec = atol(val);
    }
    else if(strcmp(key, KEYMICROSEC) == 0){
      global_interval.tv_usec = atol(val);
    }
    else if(strcmp(key, KEYINTERVAL) == 0){
      f = atof(val);
      global_interval.tv_sec = floor(f);
      global_interval.tv_usec = rint((f - global_interval.tv_sec)*1000000L);
    }
    else if(strcmp(key, KEYDEFAULTDIR) == 0){
      strncpy(global_defaultdir, val, MAXDEFAULTDIRLEN);
    }
    else if(strcmp(key, KEYOUTMAX) == 0){
      global_outputsizemax = atoi(val);
    }
    else if(strcmp(key, KEYOVERWRITE) == 0){
      strncpy(global_overwrite, val, MAXOVERWRITELEN);
    }
    else if(strcmp(key, KEYFSYNC) == 0){
      int fsyncinterval;
      char *errorp;
      strncpy(global_fsync, val, MAXFSYNCLEN);
      fsyncinterval = strtol(global_fsync, &errorp, 10);
      if(*errorp == '\0'){
	global_fsyncmode = fsyncinterval;
      }
      else if(strcasecmp(global_fsync, "on") == 0
	 || strcasecmp(global_fsync, "enable") == 0){
	global_fsyncmode = 0;  	/* every */
      }
      else {
	global_fsyncmode = -1;  /* disable */
      }
    }
#if 0
    else if(strcmp(key, KEYFILEINTERVAL) == 0){
      global_fileinterval.tv_sec = atoi(val);
    }
    else if(strcmp(key, KEYCHANGEFILE) == 0){
      global_changefile = val[0];
    }
#endif
    else if(strcmp(key, KEYLABEL) == 0){
      strncpy(global_label, val, MAXLABELLEN);
    }
    else if(strcmp(key, KEYCOMMUNITY) == 0){
      strncpy(global_community, val, MAXCOMMLEN);
    }


  }
  return 1;
}

int set_requests_value(char *key, char *val){
  double f;

  if(strcmp(key, KEYPATH) == 0){
    if(strlen(val) >= MAXPATH){
      printf("Error: path length >= %d\n", MAXPATH);
      exit(2);
    }
    strncpy(requests_path, val, MAXPATH);
  }
  else {
    if(strcmp(key, KEYSEC) == 0){
      requests_interval.tv_sec = atol(val);
    }
    else if(strcmp(key, KEYMICROSEC) == 0){
      requests_interval.tv_usec = atol(val);
    }
    else if(strcmp(key, KEYINTERVAL) == 0){
      f = atof(val);
      requests_interval.tv_sec = floor(f);
      requests_interval.tv_usec = rint((f - requests_interval.tv_sec)*1000000L);
    }
    else if(strcmp(key, KEYCOMMUNITY) == 0){
      strncpy(requests_community, val, MAXCOMMLEN);
    }

  }
  return 1;
}

int set_mib_value(char *key, char *val){
  double f;

  if(strcmp(key, KEYPATH) == 0){
    if(strlen(val) >= MAXPATH){
      printf("Error: path length >= %d\n", MAXPATH);
      exit(2);
    }
    strncpy(mib_path, val, MAXPATH);
    //printf("mib_path: %s\n", mib_path);
  }
  else {
    if(strcmp(key, KEYSEC) == 0){
      mib_interval.tv_sec = atol(val);
    }
    else if(strcmp(key, KEYMICROSEC) == 0){
      mib_interval.tv_usec = atol(val);
    }
    else if(strcmp(key, KEYINTERVAL) == 0){
      f = atof(val);
      mib_interval.tv_sec = floor(f);
      mib_interval.tv_usec = rint((f - mib_interval.tv_sec)*1000000L);
    }
  }
  return 1;
}


/* ---------------------------------------------------------------------- */
struct nodelist * add_nodelist_to_nodelist(struct nodelist *to,
					   struct nodelist *from){
  struct nodelist *top;
  if(to == NULL){
    return from;
  }

  top = to;
  while(to->next != NULL){
    to = to->next;
  }
  to->next = from;
  return top;
}

struct nodelist * add_nodelist(struct nodelist *nodes, char *name, char *nick){
  struct nodelist *now;

  now = (struct nodelist *) calloc(1, sizeof(struct nodelist));
  if(now == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }
  now->nodename = name;
  now->nodenick = nick;
  now->community = community_for_set;
  now->oidval = NULL;
  if(nodes == NULL){
    now->next = NULL;
    return now;
  }
  else {
    now->next = nodes;
    return now;
  }
}

void print_oidlist(struct oidlist *oids){
  while(oids != NULL){
    printf(" %.2d.%.6ld: %s [%s]\n", 
	   (int)oids->interval.tv_sec, (long)oids->interval.tv_usec,
	   oids->oidnick, oids->path);
    oids = oids->next;
  }
}

void print_nodelist(struct nodelist *nodes){
  printf("--- interval: oid nickname [target file name] ---\n");

  while(nodes != NULL){
    printf("### %s (%s) <%s> ###\n",
	   nodes->nodename, nodes->nodenick, nodes->community);
    print_oidlist(nodes->oidval);
    nodes = nodes->next;
  }
}

#if 0
void free_nodelist(struct nodelist *nodes){
  struct nodelist *tmp;
  while(nodes != NULL){
    myfree(&nodes->nodename);
    myfree(&nodes->nodenick);
    tmp = nodes;
    nodes = nodes->next;
    free(tmp);
  }
}
#endif

unsigned short glogger_mibtype(char *oidname){
  if(strstr(oidname, TYPE_laLoad) != NULL){
    return TYPEMODE_load;
  }
  else if(strstr(oidname, TYPE_ifOutOctets) != NULL){
    return TYPEMODE_interface;
  }
  else if(strstr(oidname, TYPE_ifInOctets) != NULL){
    return TYPEMODE_interface;
  }
  else if(strstr(oidname, TYPE_dskUsed) != NULL){
    return TYPEMODE_disk;
  }
  else if(strstr(oidname, TYPE_dskAvail) != NULL){
    return TYPEMODE_disk;
  }
  else if(strstr(oidname, TYPE_sysUpTime) != NULL){
    return TYPEMODE_uptime;
  }
  else {
    printf("warning: unsupported mib type (oid: %s)\n", oidname);
    return TYPEMODE_default;
  }
}

struct oidlist * add_oidlist(struct oidlist *oids, struct nodelist *nownode,
			     char *name, char *nick,
			     oid *oid, int oidlen, char *rule){
  struct oidlist *now;

  now = (struct oidlist *) calloc(1, sizeof(struct oidlist));
  if(now == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }

  now->mynode = nownode;
  now->oidname = name;
  now->oidnick = nick;
  now->oid = oid;
  now->oidlen = oidlen;
  now->pathrule = rule;
  now->path = convert_path(nownode, now, rule);
  now->myfile = NULL;

  if(mib_interval.tv_sec > MAXINTERVAL){
    printf("max interval: %d\n", MAXINTERVAL);
    exit(1);
  }

  mib_interval.tv_usec = mib_interval.tv_usec-mib_interval.tv_usec%MININTERVAL;
  //printf("interval: %d %d\n", (int)mib_interval.tv_sec, (int)mib_interval.tv_usec);
  now->interval = mib_interval; 

  now->mibtype = glogger_mibtype(name);

  if(oids == NULL){
    now->next = NULL;
    return now;
  }
  else {
    now->next = oids;
    return now;
  }
}

struct nodelist * add_oid_to_nodelist(struct nodelist *now,
				      char *oidname, char *oidnick,
				      char *rule){
  struct nodelist *top;
  oid *_oid;
  int oidlen;

  _oid = (oid*) calloc(MAX_OID_LEN, sizeof(oid));
  if(_oid == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }

  //oidlen = sizeof(oidd)/sizeof(oidd[0]);
  oidlen = MAX_OID_LEN;

  //printf("oidname: ###%s###%d %d\n", oidname, oidlen, strlen(oidname));

  if(!snmp_parse_oid(oidname, _oid, &oidlen)){
    snmp_perror("snmp_parse_oid");
    exit(1);
    //return NULL;
  }
  
  top = now;
  while(now != NULL){
    now->oidval =
      add_oidlist(now->oidval, now, oidname, oidnick, _oid, oidlen, rule);
    now = now->next;
  }
  return top;
}

/* ---------------------------------------------------------------------- */
void set_community_name(void){
  community_for_set = (char*) calloc(strlen(requests_community)+1,
				     sizeof(char));
  if(community_for_set == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }
  strcpy(community_for_set, requests_community);
}

void set_label_name(char *label){
  
}

/* ---------------------------------------------------------------------- */
/* use after cut_comment(), cut_front_spaces() */

/* 1 */
int set_global(char *line){
  struct separate s;
  if(separate_line(line, &s) == 2){
    if(strcmp(s.a, "requests") == 0 && strcmp(s.b, "{") == 0 ){
      mode_global = 0;
      mode_requests = 1;
      init_requests_conf();
      ok_nodes = 0;
      return 2;
    }
    else {
      if(check_keys(s.a, global_keys, GLOBAL_KEY_NUMBER)){
	set_global_value(s.a, s.b);
#ifdef DEBUGCONF
	printf("global: %s = %s\n", s.a, s.b);
#endif
	return 1;
      }
      else {
	printf("Unknown Key @ global(line=%d): %s = %s\n", linenum, s.a, s.b);
	exit(2);
      }
    }
  }
  else {
    printf("Syntax Error @ global(line=%d): %s\n", linenum, line);
    exit(2);
  }
}

/* 2 */
int set_requests(char *line){
  struct separate s;

  if(check_close_mode(line)){
    mode_requests = 0;
    mode_global = 1;
    return 0;
  }
  else if(separate_line(line, &s) == 2){
    if(strcmp(s.a, "nodes") == 0 && strcmp(s.b, "{") == 0 ){
      set_community_name();
      mode_nodes = 1;
      return 2;
    }
    else if(strcmp(s.a, "mib") == 0 && strcmp(s.b, "{") == 0 && ok_nodes == 1){
      mode_mib = 1;
      init_mib_conf();
      return 2;
    }
    else {
      if(check_keys(s.a, requests_keys, REQUESTS_KEY_NUMBER)){
	set_requests_value(s.a, s.b);	
#ifdef DEBUGCONF
	printf("requests: %s = %s\n", s.a, s.b);	
#endif
	return 1;
      }
      else {
	printf("Unknown Key @ requests(line=%d): %s = %s\n",linenum, s.a, s.b);
	exit(2);
      }
    }
  }
  else {
    printf("Syntax Error @ requests(line=%d): %s\n", linenum, line);
    exit(2);
  }
}

/* 3 */
int expand_nodes(char *line){
  struct separate s;
  int w;

  if(check_close_mode(line)){
    mode_nodes = 0;
    ok_nodes = 1;
    //print_nodelist(nownodes);
    return 0;
  }
  w = separate_line(line, &s);
  if(w == 1 || w == 2){
    EXNODES *exnodes;
    char *rep[MAXREPLACE];
    int i;

    //printf("nodes: name = %s, nick = %s\n", s.a, s.b);
#if 1
    for(i=0; i<MAXREPLACE; i++){
      rep[i] = (char*) calloc(MAXFIGURE+1, sizeof(char));
      if(rep[i] == NULL){
	printf("Error: calloc()\n");
	exit(2);
      }
    }
    exnodes = expand_node_name(s.a, s.b, rep, 0);
    while(exnodes != NULL){
      nownodes = add_nodelist(nownodes, exnodes->name, exnodes->nick);
      exnodes = exnodes->next;
    }
#else  /* test: not expand */
    {
      char *name, *nick;
      name = (char*) calloc(strlen(s.a)+1, sizeof(char));
      nick = (char*) calloc(strlen(s.b)+1, sizeof(char));
      if(name == NULL || nick == NULL){
	printf("Error: calloc()\n");
	exit(2);
      }
      strcpy(name, s.a);
      if(w == 1){
	free(nick);
	nick = name;
      }
      else {
	strcpy(nick, s.b);
      }
      nodes = add_nodelist(nodes, name, nick);
    }
#endif
    return 1;
  }
  else { 
    printf("Syntax Error @ nodes(line=%d): %s\n", linenum, line);
    exit(2);
  }
}

/* 4 */
int set_mib(char *line){
  struct separate s;

  if(check_close_mode(line)){
    mode_mib = 0;
    return 0;
  }
  else if(separate_line(line, &s) == 2){
    if(strcmp(s.a, "oids") == 0 && strcmp(s.b, "{") == 0 ){
      mode_oids = 1;
      return 2;
    }
    else {
      if(check_keys(s.a, mib_keys, MIB_KEY_NUMBER)){
	set_mib_value(s.a, s.b);	
#ifdef DEBUGCONF
	printf("mib: %s = %s\n", s.a, s.b);    
#endif
	return 1;
      }
      else {
	printf("Unknown Key @ mib(line=%d): %s = %s\n", linenum, s.a, s.b);
	exit(2);
      }
    }
  }
  else {
    printf("Syntax Error @ mib(line=%d): %s\n", linenum, line);
    exit(2);
  }
}

/* 5 */
int set_oids(char *line){
  struct separate s;
  int w;

  if(check_close_mode(line)){
    mode_oids = 0;
    //print_nodelist(nownodes);
    return 2;
  }

  w = separate_line(line, &s);
  if(w == 2){
    char *name;
    char *nick;
    char *rule;
#ifdef DEBUGCONF
    printf("oids: oid = %s, nick = %s\n", s.a, s.b);
#endif
    name = (char*) calloc(strlen(s.a)+1, sizeof(char));
    nick = (char*) calloc(strlen(s.b)+1, sizeof(char));
    rule = (char*) calloc(strlen(mib_path)+1, sizeof(char));
    if(name == NULL || nick == NULL || rule == NULL){
      printf("Error: calloc()\n");
      exit(2);
    }
    strcpy(name, s.a);
    strcpy(nick, s.b);
    strcpy(rule, mib_path);
    nownodes = add_oid_to_nodelist(nownodes, name, nick, rule);
    if(nownodes == NULL){
      return 0;
      // exit(2);
    }
    return 1;
  }
#if 1
  else if(w == 1){
    printf("Syntax Error @ oids(line=%d): please set oid nickname\n", linenum);
    exit(2);
  }
#endif
  else { 
    printf("Syntax Error @ oids(line=%d): %s\n", linenum, line);
    exit(2);
  }
}

/* ---------------------------------------------------------------------- */
char * cut_front_spaces(char *p){
  while(*p ==  ' ' || *p ==  '\t'){
    p++;
  }

  if(*p == '\0' || *p == '\r' || *p == '\n'){
    return NULL;
  }
  else {
    return p;
  }
}

int cut_comment(char *p){
  while(*p){
    if(*p == '#' || *p == '\r' || *p == '\n'){
      *p = '\0';
      return 1;
    }
    p++;
  }
  return 0;
}

/* line < MAXLINE ? */
int check_line(char *line){
  int l;
  for(l = 0; l < MAXLINE; l++){
    if(line[l] == '\0' || line[l] == '\r' || line[l] == '\n'){
      return 1;
    }
  }
  return 0;
}

/* public APIs ----------------------------------------------------------- */
struct nodelist * glogger_get_top(void){
  return topnodes;
}

struct timeval * glogger_get_nowtime(void){
  return &nowtime;
}

void glogger_print_conf(struct nodelist *nodes){
  print_nodelist(nodes);
}

void glogger_renew_time(void){
  gettimeofday(&nowtime, NULL);
  localtime_r(&nowtime.tv_sec, &now_tm);
}

/* over write */
void glogger_set_path_to_nodes(struct nodelist *nodes, char *path){
  struct oidlist *oids;

  //printf("glogger_set_path_to_nodes: %s\n", path);

  while(nodes){
    oids = nodes->oidval;
    while(oids){
      oids->path = path;
      oids->pathrule = path;
      oids = oids->next;
    }
    nodes = nodes->next;
  }
}

void glogger_init_read_conf(void){
  init_snmp("glogger3");

  /* clear list */
  init_read_conf_all();

  /* save starting time */
  gettimeofday(&nowtime, NULL);
  localtime_r(&nowtime.tv_sec, &now_tm);
}

/* main */
int glogger_read_conf(char *configfilename){
  FILE *fp;
  char line[MAXLINE], *linep;

  if(configfilename == NULL){
    printf("Error: filename = NULL\n");
    exit(1);
  }
  else if(strcmp("-", configfilename) == 0){
    fp = stdin;
  }
  else {
    fp = fopen(configfilename, "r");
    if(fp == NULL){
      fprintf(stderr, "Access Error: %s\n", configfilename);
      exit(2);
    }
  }

  while(fgets(line, MAXLINE, fp)){
    linenum++;

    /* line < MAXLINE ? */
    if(check_line(line)){
      /* OK */
      //fprintf(stdout, "%s", line);
    }
    else {
      fprintf(stderr, "Error: over MAXLINE(%d) @ line=%d\n", MAXLINE, linenum);
      return 1;
    }
    
    /* delete comments */
    if(cut_comment(line)){
    }
    //fprintf(stdout, "%s\n", line);

    /* delete front spaces */
    linep = cut_front_spaces(line);
    if(linep){
    }
    else { /* null line */
      continue;
    }

    /* 1.global */
    if(mode_global == 1){
      topnodes = add_nodelist_to_nodelist(topnodes, nownodes);
      nownodes = NULL;
      set_global(linep);
    }
    /* 2.requests */
    else if(mode_requests == 1){
      /* 3.requests override */
      if(mode_nodes == 0 && mode_mib == 0) {
	set_requests(linep);
      }
      /* 4.nodes */
      else if(mode_nodes == 1 && ok_nodes == 0){
	expand_nodes(linep);
      }
      /* 5.mib */
      else if(mode_mib == 1 && ok_nodes == 1){
	/* 6.mib override */
	if(mode_oids == 0){
	  set_mib(linep);
	}
	/* 7.oid */
	else {
	  set_oids(linep);
	}
      }
      else {
	printf("Syntax Error @ requests(line=%d): %s\n", linenum, linep);
       exit(2);
      }
    }
    else {
      /* not pass */
      fprintf(stdout, "%s\n", linep);
    }
  }
  topnodes = add_nodelist_to_nodelist(topnodes, nownodes);

  fclose(fp);
  return 0;
}
