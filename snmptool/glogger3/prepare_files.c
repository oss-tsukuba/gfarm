#include "glogger.h"
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
//#include <netinet/in.h>
#include <fcntl.h>
#include <sys/mman.h>

/* calculate greatest common denominator ---------------------------------- */
static struct timeval gcdval;
static struct timeval lcmval;
static struct timeval maxval;
static struct timeval cachegcd[MAXINTERVALCACHE];
static struct timeval cachelcm[MAXINTERVALCACHE];

static void reset_gcd_lcm(){
  gcdval.tv_sec = 0;
  gcdval.tv_usec = 0;
  lcmval.tv_sec = 0;
  lcmval.tv_usec = 0;
  maxval.tv_sec = 0;
  maxval.tv_usec = 0;

  memset((void*)cachegcd, 0, sizeof(cachegcd));
  memset((void*)cachelcm, 0, sizeof(cachelcm));
}

#if 0
static int gcd(int m, int n){
  if( (m  == 0) || (n == 0) ){
    return 0;
  }

  while(m != n && (m>0) && (n>0) ){
    if(m > n){
      m = m - n;
    }
    else{
      n = n - m;
    }
  }
  return m;
}
#endif

static int iszerotime(struct timeval *t){
  if(t->tv_sec == 0 && t->tv_usec == 0){
    return 1;
  }
  else {
    return 0;
  }
}

/* 
   m > n : 1
   m = n : 0
   m < n : -1
 */
int comparetime(struct timeval *m, struct timeval *n){
  //printf("*** cmp m n %ld %ld\n", m->tv_usec, n->tv_usec);

  if(m->tv_sec > n->tv_sec){
    return 1;
  }
  else if(m->tv_sec < n->tv_sec){
    return -1;
  }
  else {   /* equal */
    if(m->tv_usec > n->tv_usec){
      return 1;
    }
    if(m->tv_usec < n->tv_usec){
      return -1;
    }
    else {  /* equal */
      return 0;
    }
  }
}

/* for m > n */
static struct timeval subtracttime(struct timeval *m, struct timeval *n){
  struct timeval ret;

  //printf("*** sub m n %ld %ld\n", m->tv_usec, n->tv_usec);

  ret.tv_usec = m->tv_usec - n->tv_usec;
  ret.tv_sec = m->tv_sec - n->tv_sec;
  if(ret.tv_usec < 0){
    ret.tv_sec -= 1;
    ret.tv_usec += 1000000L;
  }
  return ret;
}

/* calculate greatest common divisor */
/* after using reset_gcd_lcm() */
static void calc_gcd(struct timeval *val){
  int cmp, i;
  struct timeval m, n;
  struct timeval *tmp;

  if(iszerotime(val)){
    printf("Error: bad interval (sec=0, usec=0)\n");
    exit(2);
  }
  else if(iszerotime(&gcdval)){
    gcdval = *val;
    maxval = *val;
    return;
  }
  else {
    /* compare interval cache */
    tmp = cachegcd;
    i = 0;
    while(1){
      if(i >= MAXINTERVALCACHE){
	cachegcd[0] = *val;    /* over write */
	break;
      }
      else if(iszerotime(tmp)){
	*tmp = *val;  	 /* add */
	break;
      }
      else if(comparetime(tmp, val) == 0){
	return;  	/* have calculated */
      }
      tmp++;
      i++;
    }

    /* compare maximum value */
    if(comparetime(val, &maxval) > 0){
      maxval = *val;
    }

    /* calculate gcd */
    //printf("*** start cal gcd ---------------\n");
    m = *val;
    n = gcdval;
    while( !iszerotime(&m) && !iszerotime(&n)){
      cmp = comparetime(&m, &n);
      if(cmp == 0){
	break;
      }
      else if(cmp > 0){   /* m > n */
	m = subtracttime(&m, &n);
      }
      else {		  /* m < n */
	n = subtracttime(&n, &m);
      }
    }
    gcdval = m;
    //printf("*** gcd %ld %ld\n", gcdval.tv_sec, gcdval.tv_usec);
  }
}

static struct timeval addtime(struct timeval *m, struct timeval *n){
  struct timeval ret;

  ret.tv_sec = m->tv_sec + n->tv_sec;
  ret.tv_usec = m->tv_usec + n->tv_usec;
  if(ret.tv_usec >= 1000000L){
    ret.tv_usec -= 1000000L;
    ret.tv_sec += 1;
  }
  return ret;
}


static struct timeval multiplytime(struct timeval *m, int n){
  struct timeval ret;
  int i;

  if(n <= 0){
    return *m;
  }

  ret.tv_sec = 0;
  ret.tv_usec = 0;
  i = 0;
  while(i < n){
    ret.tv_sec += m->tv_sec;
    ret.tv_usec += m->tv_usec;
    if(ret.tv_usec >= 1000000L){
      ret.tv_usec -= 1000000L;
      ret.tv_sec += 1;
    }
    i++;
  }
  return ret;
}


/* for m > n */
static int dividetime(struct timeval *m, struct timeval *n,
		      struct timeval *surplus){
  int ret;
  struct timeval tmp;
  ret = 0;
  tmp = *m;
  if(iszerotime(n)){
    printf("Error: divide by zero\n");
    exit(2);
  }

  while(comparetime(&tmp, n) >= 0){
    tmp = subtracttime(&tmp, n);
    ret++;
    //printf("divide: %d %ld %ld\n", ret, tmp.tv_sec, tmp.tv_usec);
  }
  if(surplus != NULL){
    surplus->tv_sec = tmp.tv_sec;
    surplus->tv_usec = tmp.tv_usec;
    //printf("divide: %ld %ld\n", surplus->tv_sec, surplus->tv_usec);
  }
  return ret;
}

/* calculate least common multiple */
/* after using reset_gcd_lcm() */
static void calc_lcm(struct timeval *val){
  struct timeval m, n, *tmp;
  int div, i;

  if(iszerotime(val)){
    printf("Error: bad interval (sec=0, usec=0)\n");
    exit(2);
  }
  else if(iszerotime(&lcmval)){
    lcmval = *val;
    return;
  }
  /* compare cache */
  /* compare interval cache */
  tmp = cachelcm;
  i = 0;
  while(1){
    if(i >= MAXINTERVALCACHE){
      cachelcm[0] = *val;    /* over write */
      break;
    }
    else if(iszerotime(tmp)){
      *tmp = *val;  	 /* add */
      break;
    }
    else if(comparetime(tmp, val) == 0){
      //printf("*** lcm cached\n");
      return;  	/* have calculated */
    }
    tmp++;
    i++;
  }
  
  m = *val;
  n = lcmval;
  gcdval = n; 	 /* reset */
  memset((void*)cachegcd, 0, sizeof(cachegcd));
  calc_gcd(&m);
  if(comparetime(&m, &n) >= 0){
    div = dividetime(&m, &gcdval, NULL);
    lcmval = multiplytime(&n, div);
  }
  else {
    div = dividetime(&n, &gcdval, NULL);
    lcmval = multiplytime(&m, div);
  }
  //printf("*** lcm %ld %ld\n", lcmval.tv_sec, lcmval.tv_usec);
}

/* create filelist ------------------------------------------------------- */
void glogger_free_filelist(struct filelist *files){
  struct filelist *tmp;
  while(files != NULL){
    //printf("free_filelist: %s\n", files->path);
    tmp = files;
    files = files->next;
    free(tmp);
  }
}

/* add hostdefine and count or seek hostid */
static struct hostdefine * add_hostdef(struct hostdefine *hostdef,
				       struct nodelist *nodes,
				       unsigned short *hostid){
  struct hostdefine *now, *tmp;
  struct hostent *he;
  char *ip;

  *hostid = 0;
  now = hostdef;
  while(now){
    if(now->fqdn == NULL || nodes->nodename == NULL){
      exit(2);
    }
    if(strcmp(now->fqdn, nodes->nodename) == 0){
      return hostdef;
    }
    now = now->next;
    *hostid += 1;
  }
  
  /* now == NULL */
  *hostid = 0;
  now = (struct hostdefine*) calloc(1, sizeof(struct hostdefine));
  if(now == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }
  he = gethostbyname(nodes->nodename);
  if(he == NULL){
    printf("Error: gethostbyname(): %s\n", nodes->nodename);
    exit(2);
  }
  
  ip = (char*) calloc(1, strlen(inet_ntoa(*(struct in_addr*)he->h_addr))+1);
  if(ip == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }
  strcpy(ip, inet_ntoa(*(struct in_addr*)he->h_addr));
  now->ip = ip;
  //printf("IP = %s\n", now->ip);
  now->fqdn = nodes->nodename;
  now->nick = nodes->nodenick;
  now->community = nodes->community;

  now->next = NULL;
  if(hostdef == NULL){
    return now;
  }
  else {
    tmp = hostdef;
    *hostid += 1;
    while(tmp->next){
      *hostid += 1;
      tmp = tmp->next;
    }
    //printf("hostid = %d\n", *hostid);
    tmp->next = now;
    return hostdef;
  }
}

/* add oiddefine and count or seek oidid */
static struct oiddefine * add_oiddef(struct oiddefine *oiddef,
				     struct oidlist *oids,
				     unsigned short *oidid){
  struct oiddefine *now, *tmp;

  *oidid = 0;
  now = oiddef;
  while(now){
    if(now->oidname == NULL || oids->oidname == NULL){
      exit(2);
    }
    if(strcmp(now->oidname, oids->oidname) == 0){
      /* do nothing */
      return oiddef;
    }
    now = now->next;
    *oidid += 1;
  }
  
  /* now == NULL */
  *oidid = 0;
  now = (struct oiddefine*) calloc(1, sizeof(struct oiddefine));
  if(now == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }

#if 0
  print_objid(oids->oid, oids->oidlen);
  print_description(oids->oid, oids->oidlen, 80);
#endif
  now->oidname = oids->oidname;
  now->nick = oids->oidnick;

  now->next = NULL;
  if(oiddef == NULL){
    return now;
  }
  else {
    tmp = oiddef;
    *oidid += 1;
    while(tmp->next){
      *oidid += 1;
      tmp = tmp->next;
    }
    //printf("oidid = %d\n", *oidid);
    tmp->next = now;
    return oiddef;
  }
}

/* add hostid and oidid and interval to intervalmap  */
static struct intervalmap * add_intervalmap(struct intervalmap *map,
					    struct oidlist *oids,
					    unsigned short hostid,
					    unsigned short oidid){
  struct intervalmap *now, *tmp;

  //printf("add_intervalmap: id: %d.%d\n", hostid, oidid);
  
  now = (struct intervalmap*) calloc(1, sizeof(struct intervalmap));
  if(now == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }
  now->hostindex = htons(hostid);
  now->oidindex = htons(oidid);
  now->interval_sec = htonl(oids->interval.tv_sec);
  now->interval_usec = htonl(oids->interval.tv_usec);
  now->myoid = oids;

  //printf("add_im: %d %d %ld %ld\n", hostid, oidid, oids->interval.tv_sec, oids->interval.tv_usec);

  now->next = NULL;
  if(map == NULL){
    return now;
  }
  else {
    tmp = map;
    while(tmp->next){		/* add to tail */
      tmp = tmp->next;
    }
    tmp->next = now;
    return map;
  }
}


/* use calc_lcm() and lcmval */
static void calc_dbg_interval_severalfiles(struct filelist *files){
  struct intervalmap *tmp;
  struct timeval interval;
  while(files){
    tmp = files->interval_map;
    reset_gcd_lcm();
    while(tmp){
      interval.tv_sec = ntohl(tmp->interval_sec);
      interval.tv_usec = ntohl(tmp->interval_usec);
      calc_lcm(&interval);
      tmp = tmp->next;
    }
    files->interval_lcm = lcmval;
    files->dbg_interval_sec = htonl(lcmval.tv_sec);
    files->dbg_interval_usec = htonl(lcmval.tv_usec);
    //printf("lcm: %s: %ld %ld\n", files->path, lcmval.tv_sec, lcmval.tv_usec);

    files = files->next;
  }
}

static void add_filelist_property(struct filelist *files,
				  struct oidlist *oids){
  struct filelist *now;
  struct nodelist *nodes;
  unsigned short hostid, oidid;
  //int size;

  now = files;
  nodes = oids->mynode;
  //printf("add_filelist_property: %s: %s\n", nodes->nodename, oids->oidnick);

  now->hostdef = add_hostdef(now->hostdef, nodes, &hostid);
  now->oiddef = add_oiddef(now->oiddef, oids, &oidid);
  now->interval_map = add_intervalmap(now->interval_map, oids, hostid, oidid);
}

/* seek or create a target output file */
static struct filelist * add_filelist(struct filelist *files,
				      struct oidlist *oids){
  struct filelist *now;
  //struct stat st;
  //struct timeval *starttime;
  //int fd;

  now = files;
  while(now){
    if(now->path == NULL || oids->path == NULL){
      printf("Error: add_filelist\n");
      exit(2);
      //return NULL;
    }
    if(strcmp(now->path, oids->path) == 0){
      add_filelist_property(now, oids);
      oids->myfile = now;
      //printf("-- %s\n", now->path);
      return files;
    }
    now = now->next;
  }

  /* if(now == NULL) -> create a new target output file */
  now = (struct filelist*) calloc(1, sizeof(struct filelist));
  if(now == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }
  
  now->path = oids->path;

  /* set and initialize */
  now->version = htons(OUTDATA_VERSION);
#if 0
  starttime = glogger_get_nowtime();
  now->start_sec = htonl(starttime->tv_sec);
  now->start_usec = htonl(starttime->tv_usec);
#endif
  now->nowpath = NULL;
  now->hostdef = NULL;
  now->oiddef = NULL;
  now->interval_map = NULL;
  add_filelist_property(now, oids);
  oids->myfile = now;

  now->next = files;	  /* add to tail */
  return now;
}

static void calc_metadatalength_severalfiles(struct filelist *files){
  struct hostdefine *htmp;
  struct oiddefine *otmp;
  struct intervalmap *itmp;
  struct dbg_map *dtmp;
  unsigned long size;
  unsigned short hsize, osize, imsize, dmsize;

  while(files){
    /* calculate length of metadata1 */
    /* bytes of this version */
    /* version  : 2 */
    /* dbg_size : 4 */
    /* start_sec  : 4 */
    /* start_usec : 4 */
    /* dbg_interval_sec  : 4 */
    /* dbg_interval_usec : 4 */
    files->metadata1size = htons(22);

    /* calculate length of metadata2 */
    size = 0;

    /* host definition */
    hsize = 0;
    htmp = files->hostdef;
    while(htmp){
      hsize +=
	strlen(htmp->ip) + 1 +
	strlen(htmp->fqdn) + 1 +
	strlen(htmp->nick) + 1 +
	strlen(htmp->community) + 1;
      htmp = htmp->next;
    }
    size += hsize + 2;
    files->hostdefsize = htons(hsize);

    /* oid definition */
    osize = 0;
    otmp = files->oiddef;
    while(otmp){
      osize +=
	strlen(otmp->oidname) + 1 +
	strlen(otmp->nick) + 1;
      otmp = otmp->next;
    }
    size += osize + 2;
    files->oiddefsize = htons(osize);

    /* interval map definition */
    imsize = 0;
    itmp = files->interval_map;
    while(itmp){
      /* bytes -> hostindex:2, oidindex:2, inteval:8 */
      imsize += 12;
      itmp = itmp->next;
    }
    size += imsize + 2;
    files->imsize = htons(imsize);

    /* data block group definition map */
    dmsize = 0;
    dtmp = files->dbgmap;
    while(dtmp){
      if(dtmp->im == NULL){	/* separater */
	dmsize += 2;  /* for a number of value in this data block */
      }
      else {
	/* host index, oid index */
	dmsize += 4;
      }
      dtmp = dtmp->next;
    }
    size += dmsize + 2;
    files->dbgmapsize = htons(dmsize);

    //printf("dmsize: %u\n", dmsize);
    //printf("meta2: %s: %ld\n", files->path, size);
    files->metadata2size = htonl(size);
    files = files->next;
  }  
}


static struct dbg_map * add_dbg_map(struct dbg_map *dm,
				    struct intervalmap *im){
  struct dbg_map *now, *top;
  
  now = (struct dbg_map*)calloc(1, sizeof(struct dbg_map));
  if(now == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }

  now->im = im;
  now->next = NULL;

#if 0  // for DEBUG
  if(im != NULL){
    printf("add_dbg_map: %s %s\n", im->myoid->mynode->nodename, im->myoid->oidnick);
  }
  else {
    printf("add_dbg_map: NULL (separater)\n");
  }
#endif

  if(dm == NULL){
    return now;
  }
  else {
    top = dm;
    while(dm->next){
      dm = dm->next;
    }
    dm->next = now;
    return top;
  }
}


static void create_dbg_map(struct filelist *files){
  struct intervalmap *im;
  struct dbg_map *dm;
  struct timeval tv, dbgtime, surplus, check;
  unsigned long value, bufnum;
  int i, n, div, found;

  while(files){
    //printf("calc_dbg: %s \n", files->path);
    /* calculate gcd of several intervals */
    reset_gcd_lcm();
    im = files->interval_map;
    while(im){
      tv.tv_sec = ntohl(im->interval_sec);
      tv.tv_usec = ntohl(im->interval_usec);
      calc_gcd(&tv);
      im = im->next;
    }
    files->interval_gcd = gcdval;

    /* calculate (lcm / gcd) */
    dbgtime.tv_sec = ntohl(files->dbg_interval_sec);
    dbgtime.tv_usec = ntohl(files->dbg_interval_usec);
    n = dividetime(&dbgtime, &gcdval, &surplus);
    if(!iszerotime(&surplus)){
      printf("calc_dbg_length: Error:\n");
      exit(3);
    }

    /* seek matched intervals */
    dm = NULL;
    value = 0;
    i = 0;
    check.tv_sec = 0;
    check.tv_usec = 0;
    /* n == lcm / gcd == a number of all patterns */
    while(i < n){
      found = 0;
      im = files->interval_map;
      //printf("---\n");
      while(im){
	tv.tv_sec = ntohl(im->interval_sec);
	tv.tv_usec = ntohl(im->interval_usec);
	div = dividetime(&check, &tv, &surplus);
	if(iszerotime(&surplus)){ /* matched */
	  value += 5;  		/* flag:1, data:4 */
	  found = 1;
	  //printf("tv   : sec=%ld, usec=%ld\n",tv.tv_sec, tv.tv_usec);
	  //printf("check: sec=%ld, usec=%ld\n",check.tv_sec, check.tv_usec);
	  //printf("gcd  : sec=%ld, usec=%ld\n",gcdval.tv_sec, gcdval.tv_usec);
	  dm = add_dbg_map(dm, im);
	}
	im = im->next;
      }
      if(found == 1){
	value += 8;  /* for this data block time */
      }
      i++;
      dm = add_dbg_map(dm, NULL);  /* add separater */
      check = addtime(&check, &gcdval);	/* next interval */
    }
    files->dbgmap = dm;
    files->dbgmap_pointer = dm;

    files->dbg_size = htonl(value);  /* data block group size */
    //printf("dbg_size: %s %ld\n", files->path, value);

    /* for buffering mode */
    if(bufmode == BUFMODE_NONE){
      files->buf = NULL;
      files->buf_point = NULL;
      files->buf_count = 0;
      files->buf_max = global_outputsizemax * 1024;
    }
    else if(bufmode == BUFMODE_MMAP){
      files->buf_num = global_outputsizemax * 1024 / value;
      //files->buf_count = 0;
    }
    else {
      if(bufmode == BUFMODE_BURST){
	bufnum = global_outputsizemax * 1024 / value;
	//bufnum = burstbuffersize * 1024 / value;
	files->buf_num = bufnum;
      }
      else if(bufmode == BUFMODE_DBG){
	files->buf_num = 1;
      }
      
      else {
	printf("unknown buffering mode (%d)\n", bufmode);
	exit(5);
      }
      files->buf = (char*) calloc(1, value*bufnum);
      if(files->buf == NULL){
	printf("Error: can't allocate enough memory (%lu*%lu = %lu)\n",
	       value, bufnum, value*bufnum);
	exit(2);
      }

#if 0 // touch
      memset(files->buf, 0, value*bufnum);
#endif
      files->buf_point = files->buf;
      files->buf_count = 0;
    }
    files = files->next;
  }
}


int glogger_write_metadata(struct filelist *files){
  void *tmp, *pnum, *pnow;
  unsigned short m1len;
  unsigned long m2len;
  int len;
  struct hostdefine *hdef;
  struct oiddefine *odef;
  struct intervalmap *im;
  struct dbg_map *dm;
  unsigned short dmcount;
  //int i, j;
  int dbgnum;

    /* part of metadata1 */

    m1len = ntohs(files->metadata1size) + sizeof(files->metadata1size);
    tmp = (void*) calloc(1, m1len);
    if(tmp == NULL){
      exit(2);
    }
    files->metadata1 = tmp;

    memcpy(tmp, (void*)&files->metadata1size, sizeof(uint16_t));
    tmp += sizeof(uint16_t);
    memcpy(tmp, (void*)&files->version, sizeof(uint16_t));
    tmp += sizeof(uint16_t);
    memcpy(tmp, (void*)&files->dbg_size, sizeof(uint32_t));
    tmp += sizeof(uint32_t);
    memcpy(tmp, (void*)&files->start_sec, sizeof(uint32_t));
    tmp += sizeof(uint32_t);
    memcpy(tmp, (void*)&files->start_usec, sizeof(uint32_t));
    tmp += sizeof(uint32_t);
    memcpy(tmp, (void*)&files->dbg_interval_sec, sizeof(uint32_t));
    tmp += sizeof(uint32_t);
    memcpy(tmp, (void*)&files->dbg_interval_usec, sizeof(uint32_t));
    tmp += sizeof(uint32_t);

    write_func(files->metadata1, 1, m1len, files);

    /* part of metadata2 */
    m2len = ntohl(files->metadata2size) + sizeof(files->metadata2size);
    tmp = (void*) calloc(1, m2len);
    if(tmp == NULL){
      exit(2);
    }
    files->metadata2 = tmp;

    memcpy(tmp, (void*)&files->metadata2size, sizeof(uint32_t));
    tmp += sizeof(uint32_t);

    /* hostdefine */
    memcpy(tmp, (void*)&files->hostdefsize, sizeof(uint16_t));
    tmp += sizeof(uint16_t);
    hdef = files->hostdef;
    while(hdef){
      len = strlen(hdef->ip)+1;
      memcpy(tmp, hdef->ip, len);
      tmp += len;
      len = strlen(hdef->fqdn)+1;
      memcpy(tmp, hdef->fqdn, len);
      tmp += len;
      len = strlen(hdef->nick)+1;
      memcpy(tmp, hdef->nick, len);
      tmp += len;
      len = strlen(hdef->community)+1;
      memcpy(tmp, hdef->community, len);
      tmp += len;
      hdef = hdef->next;
    }
    /* oiddefine */
    memcpy(tmp, (void*)&files->oiddefsize, sizeof(uint16_t));
    tmp += sizeof(uint16_t);
    odef = files->oiddef;
    while(odef){
      len = strlen(odef->oidname)+1;
      memcpy(tmp, odef->oidname, len);
      tmp += len;
      len = strlen(odef->nick)+1;
      memcpy(tmp, odef->nick, len);
      tmp += len;
      odef = odef->next;
    }
    /* intervalmap */
    memcpy(tmp, (void*)&files->imsize, sizeof(uint16_t));
    tmp += sizeof(uint16_t);
    im = files->interval_map;
    while(im){
      memcpy(tmp, (void*)&im->hostindex, sizeof(uint16_t));
      tmp += sizeof(uint16_t);
      memcpy(tmp, (void*)&im->oidindex, sizeof(uint16_t));
      tmp += sizeof(uint16_t);
      memcpy(tmp, (void*)&im->interval_sec, sizeof(uint32_t));
      tmp += sizeof(uint32_t);
      memcpy(tmp, (void*)&im->interval_usec, sizeof(uint32_t));
      tmp += sizeof(uint32_t);

      //printf("write im: %ld\n", ntohl(im->interval_sec));
      im = im->next;
    }
    /* dbgmap */
    memcpy(tmp, (void*)&files->dbgmapsize, sizeof(uint16_t));
    tmp += 2;
    dm = files->dbgmap;
    dmcount = 0;
    pnum = tmp;   /* pointer for a number of value in a data block */
    pnow = tmp+2;
    dbgnum = 0;
    while(dm){
      if(dm->im == NULL){
	//printf("dmcount: %u\n", dmcount);
	*(uint16_t*)pnum = htons(dmcount);
	//printf("pnum: %u\n", ntohs(*(uint16_t*)pnum));
	pnum += 2 + dmcount*4;
	pnow = pnum + 2;
	dmcount = 0;
	dbgnum++;
      }
      else {
	memcpy(pnow, (void*)&dm->im->hostindex, 2);
        pnow += 2;
	memcpy(pnow, (void*)&dm->im->oidindex,  2);
	pnow += 2;
	dmcount++;
      }
      dm = dm->next;
    }
#if 0  // print dbg map
    pnum = tmp;
    pnow = tmp+2;
    j=0;
    while(j < dbgnum){
      dmcount = ntohs(*(uint16_t*)pnum);
      printf("----dmcount---- %u\n", dmcount);
      i=0;
      while(i < dmcount){
	printf("%u ", ntohs(*(uint16_t*)pnow));
	pnow += 2;
	printf("%u\n", ntohs(*(uint16_t*)pnow));
	pnow += 2;
	i++;
      }
      pnum += 2 + dmcount*4;
      pnow = pnum + 2;
      j++;
    }
#endif
    tmp = pnum;

    write_func(files->metadata2, 1, m2len, files);

  return 1;
}

int glogger_write_metadata_all(struct filelist *files){
  while(files){
    glogger_write_metadata(files);
    files = files->next;
  }
  glogger_fflush_files(files);
  return 1;
}


struct filelist * glogger_set_filelist(struct nodelist *nodes){
  struct nodelist *tmp;
  struct oidlist *oids;
  struct filelist *fl;

  fl = NULL;
  tmp = nodes;
  while(tmp){
    oids = tmp->oidval;
    while(oids){
      fl = add_filelist(fl, oids);
      oids = oids->next;
    }
    tmp = tmp->next;
  }

  /* calculate interval of a data block group from several files */
  calc_dbg_interval_severalfiles(fl);

  /* calculate length of a data block group from several files
     and create a data block group pattarn map */
  create_dbg_map(fl);

  /* calculate length of metadata1 and metadata2 from several files */
  calc_metadatalength_severalfiles(fl);

  return fl;
}

#define TMPLEN 64
static void convert_time(char *_dst, char *rule){
  char *f = rule;
  char *dst = _dst;
  char tmp[TMPLEN];
  int i;

  while(*f != '\0'){
    if(MAXPATH <= strlen(dst)){
      printf("Error: path name length >= %d(MAXPATH)\n", MAXPATH);
      exit(2);
    }
    if(*f == '%'){
      f++;
      switch(*f){
      case 't':
	snprintf(tmp, TMPLEN, "%.4d%.2d%.2d%.2d%.2d%.2d%.3d",
		 now_tm.tm_year+1900, now_tm.tm_mon+1, now_tm.tm_mday,
		 now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec,
		 (int)(nowtime.tv_usec / 1000));
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
      case 'c':
	snprintf(tmp, TMPLEN, "%.6ld", nowtime.tv_usec);
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
      case 's':	       /* milisecond */
	snprintf(tmp, TMPLEN, "%.3d", (int)(nowtime.tv_usec / 1000));
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
      case 'S':
	snprintf(tmp, TMPLEN, "%.2d", now_tm.tm_sec);
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
      case 'M':
	snprintf(tmp, TMPLEN, "%.2d", now_tm.tm_min);
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
      case 'h':
	snprintf(tmp, TMPLEN, "%.2d", now_tm.tm_hour);
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
      case 'd':
	snprintf(tmp, TMPLEN, "%.2d", now_tm.tm_mday);
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
      case 'j':
	snprintf(tmp, TMPLEN, "%.3d", now_tm.tm_yday);
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
      case 'm':
	snprintf(tmp, TMPLEN, "%.2d", now_tm.tm_mon+1);
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
      case 'y':
	snprintf(tmp, TMPLEN, "%.4d", now_tm.tm_year+1900);
	strncat(dst, tmp, MAXPATH - strlen(dst));
	break;
      }
    }

    else {
      i = strlen(dst);
      dst[i] = *f;
      dst[i+1] = '\0';
    }
    f++;
  }
}

int glogger_compare_and_change_filename(struct filelist *file){
  char path[MAXPATH];

  path[0] = '\0';
  convert_time(path, file->path);
  
  //printf("glogger_compare_and_change_filename\n");

  if(file->nowpath != NULL && strcmp(path, file->nowpath) == 0){
    //printf("%s\n", path);
    /* do nothing */
    return 0;
  }
  else {
    free(file->nowpath);
    file->nowpath = calloc(1, strlen(path)+1);
    if(file->nowpath == NULL){
      printf("Error: calloc()\n");
      exit(2);
    }
    strcpy(file->nowpath, path);
    return 1;
  }
}

int glogger_open_file(struct filelist *now){
  struct stat st;
  unsigned long size;
  char c;

  now->start_sec = htonl(nowtime.tv_sec);
  now->start_usec = htonl(nowtime.tv_usec);


  if(strcasecmp(global_overwrite, "on") != 0
     && strcasecmp(global_overwrite, "enable") != 0){
    /* over write is forbidden */
    /* check existing */
    stat(now->nowpath, &st);
    if(S_ISREG(st.st_mode)){
      printf("Error: same file exist: %s\n", now->nowpath);
      exit(4);
    }
  }

  if(bufmode == BUFMODE_MMAP){
    now->fd = open(now->nowpath, O_RDWR|O_TRUNC|O_CREAT, 0666);
    if(now->fd == -1){
      //printf("Error: cannot open: %s\n", now->path);
      printf("Error: cannot open: %s\n", now->nowpath);
      exit(2);
    }
    size = 2 + ntohs(now->metadata1size) + 4 + ntohl(now->metadata2size) 
      + now->buf_num * ntohl(now->dbg_size);
    
    now->buf_max = size;
    printf("mmap size: %lu = 2+%u+2+%u+%lu*%u\n",
	   size,
	   ntohs(now->metadata1size), ntohl(now->metadata2size),
	   now->buf_num, ntohl(now->dbg_size));
    
    //size = 1;
    
    if(lseek(now->fd, size-1, SEEK_SET) < 0){
      printf("lseek\n");
      exit(2);
    }
#if 0
    if(read(now->fd, &c, 1) == -1){
      c = '\0';
      printf("read\n");
      exit(2);
    }
#endif
    if(write(now->fd, &c, 1) == -1){
      printf("write\n");
      exit(2);
    }
    
    now->buf = (char*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, now->fd, 0);
    if((int)now->buf == -1){
      //printf("Error: cannot open: %s\n", now->path);
      printf("Error: cannot open: %s\n", now->nowpath);
      printf("Error: fail mmap(): size=%lu\n", size);
      exit(2);
    }
    
    // for test
    //memset(now->buf, 0, size);
    
    now->buf_point = now->buf;
    now->buf_count = 0;
    
    now->fp = NULL;
  }
  else {   /* not mmap */
    //now->fp = fopen(now->path, "wb");
    now->fp = fopen(now->nowpath, "wb");
    if(now->fp == NULL){
      printf("Error: cannot open: %s\n", now->nowpath);
      exit(2);
    }
    now->buf_count = 0;    
  }

  return 1;
}

int glogger_open_files(struct filelist *files){
  struct filelist *now;
  now = files;
  while(now){
    glogger_compare_and_change_filename(now);
    glogger_open_file(now);
    now = now->next;
  }
  return 1;
}

/* free and close */
int glogger_close_file(struct filelist *file){
  if(bufmode == BUFMODE_MMAP){
    munmap(file->buf, file->buf_max);
    close(file->fd);
  }
  else {
    fclose(file->fp);
  }
  return 1;
}

int glogger_close_files(struct filelist *files){
  while(files){
    glogger_close_file(files);
    files = files->next;
  }
  return 1;
}


/* -------------------------------------------------------------------- */
int calc_interval_from_files(struct timeval *setinterval,
			     struct filelist *files){
  struct filelist *tmp;
  int ret;

  reset_gcd_lcm();
  tmp = files;
  while(files){
    //printf(" interval_gcd -> %ld %ld\n", files->interval_gcd.tv_sec, files->interval_gcd.tv_usec);
    calc_gcd(&files->interval_gcd);
    files = files->next;
  }
  *setinterval = gcdval;
  
  //printf("*** gcd %ld %ld\n", gcdval.tv_sec, gcdval.tv_usec);
  //printf("*** max %ld %ld\n", maxval.tv_sec, maxval.tv_usec);

  reset_gcd_lcm();
  files = tmp;
  while(files){
    //printf(" interval_lcm -> %ld %ld\n", files->interval_lcm.tv_sec, files->interval_lcm.tv_usec);
    calc_lcm(&files->interval_lcm);
    files = files->next;
  }
  //printf("*** lcm %ld %ld\n", lcmval.tv_sec, lcmval.tv_usec);
  ret = dividetime(&lcmval, setinterval, NULL);
  //printf("*** count %d\n", ret);
  return ret;
}



static struct req_oids * add_dbgmap_to_reqoids(struct req_oids *reqoids,
					       struct dbg_map *dbgmap){
  struct req_oids *now, *top;
  //struct oidlist *addoid;

  if(dbgmap == NULL){
    printf("Error: @add_reqlist()\n");
    exit(1);
  }
  
  now = (struct req_oids*) calloc(1, sizeof(struct req_oids));
  if(now == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }
  
  now->oidval = dbgmap->im->myoid;
  now->dbgmap = dbgmap;

  //printf("pickup: %s %s\n", now->oidval->mynode->nodenick, now->oidval->oidnick);

  if(reqoids == NULL){
    return now;
  }
  top = reqoids;
  while(reqoids->next){
    reqoids = reqoids->next;
  }
  reqoids->next = now;
  return top;
}

static struct req_list * add_dbgmap_to_reqlist(struct req_list *reqlist,
					       struct dbg_map *dbgmap){
  struct nodelist *addnode;
  struct req_list *top, *reqset;
  //struct req_oids *reqoid;

  if(dbgmap == NULL){
    printf("Error: @add_dbgmap_to_reqlist()\n");
    exit(1);
  }

  addnode = dbgmap->im->myoid->mynode;

  top = reqlist;
  while(reqlist){
    /* compare node list pointer */
    if(reqlist->node == addnode){
      reqlist->reqoids = add_dbgmap_to_reqoids(reqlist->reqoids, dbgmap);
      return top;
    }
    reqlist = reqlist->next;
  }

  /* reqlist == NULL (same node was not found) */
  reqset = (struct req_list*) calloc(1, sizeof(struct req_list));
  if(reqset == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }
  reqset->node = addnode;
#ifdef HOOK_snmp_free_pdu
  reqset->reqpdu = NULL;
#endif
  reqset->reqoids = add_dbgmap_to_reqoids(NULL, dbgmap);
  reqset->next = top;
  
  return reqset;
}


static struct req_list * pickup_files(struct filelist *files,
				      struct timeval *timing, int reqnum){
  struct req_list *top;
  struct dbg_map *dmap;
  struct timeval check;
  int i;

  top = NULL;
  while(files){
    //printf("pickupfiles: %s\n", files->path);

    dmap = files->dbgmap;
    if(dmap == NULL){
      printf("Error: A data block group map is NULL.\n");
      exit(1);
    }

    /* using a law of "maininterval == dbg_interval*n [n=1,2,...]" */
    check.tv_sec = 0;
    check.tv_usec = 0;
    i = 0;
    while(i < reqnum){	/* timing match with dbg map */
      if(dmap == NULL){
	dmap = files->dbgmap; 	/* reloop */
      }

      if(dmap->im == NULL){ /* separater <-> every request pattern */
	check = addtime(&check, &files->interval_gcd);
	i++;
      }      
      else if(comparetime(&check, timing) == 0){ /* check == timing */
	while(dmap && dmap->im){
	  top = add_dbgmap_to_reqlist(top, dmap);
	  dmap = dmap->next;
	}
	continue;
      }
      dmap = dmap->next;
    }
    files = files->next;
  }
  return top;
}



static struct target_files *
add_target_files(struct target_files *targets, struct filelist *file){
  struct target_files *top;
  if(file == NULL){
    printf("Error: @add_target_files()\n");
    exit(1);
  }
  top = targets;
  while(targets){
    /* compare file list pointer */
    if(targets->file == file){
      /* do nothing */
      return top;
    }
    targets = targets->next;
  }
  /* targets == NULL (same node was not found) */
  targets = (struct target_files*) calloc(1, sizeof(struct target_files));
  if(targets == NULL){
    printf("Error: calloc()\n");
    exit(2);
  }
  targets->file = file;
  targets->next = top;
  return targets;
}

static struct target_files * seek_target_files(struct req_list *reqlist){
  struct target_files *top;
  struct req_oids *roids;

  if(reqlist == NULL){
    /* no request at this timing */
    return NULL;
  }
  top = NULL;
  while(reqlist){
    roids = reqlist->reqoids;
    while(roids){
      top = add_target_files(top, roids->oidval->myfile);
      roids = roids->next;
    }
    reqlist = reqlist->next;
  }
  return top;
}

struct req_loop *
glogger_prepare_requests_from_files(struct filelist *files,
				    struct timeval *setinterval){
  int reqnum;
  int i;
  struct req_loop *reqs, *req;
  struct timeval timing;

  /* calculate a greatest common denominator of several intervals
     & a number of in main loop */
  /* maininterval * reqnum = time of all request pattarn */
  reqnum = calc_interval_from_files(setinterval, files);

  reqs = (struct req_loop*) calloc(1, sizeof(struct req_loop) * reqnum);
  i = 0;
  while(i < reqnum-1){
    reqs[i].next = &reqs[i+1];
    i++;
  }
  reqs[reqnum-1].next = &reqs[0]; /* loop */

  timing.tv_sec = 0;
  timing.tv_usec = 0;
  i = 0;
  while(i < reqnum){
    req = &reqs[i];
    //printf("prepare_requests: %d ==================\n", i+1);
    req->reqlist = pickup_files(files, &timing, reqnum);
    req->targets = seek_target_files(req->reqlist);

    timing = addtime(&timing, setinterval);
    i++;
  }

  return reqs;
}
