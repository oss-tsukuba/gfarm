#include "glogger.h"

#include <errno.h>

#include <math.h>  		/* rint */
#include <unistd.h>
#include <sys/mman.h>

#ifdef USE_DLOPEN
#include <dlfcn.h>
void (*original_snmp_free_pdu)(struct snmp_pdu*);
#endif

int glogger_active_hosts; 	/* number of request */


/* ------------------------------------------------------------------ */
static void my_sleep(struct timeval *sleeptime){
  fd_set rfds;
  //int i;

  if(sleeptime == NULL){
    return;
  }

  FD_ZERO(&rfds);
#if 0
  do {
    printf("*** sleep...\n");
    i = select(1, &rfds, NULL, NULL, sleeptime);
    //printf("sleep: select = %d\n", i);
  } while(i < 0) ;
#endif
  select(1, &rfds, NULL, NULL, sleeptime);

}

void glogger_sleep(struct timeval *timeout){
  struct timeval now;
  gettimeofday(&now, NULL);
  my_sleep(glogger_checktime(&now, timeout));
}

void glogger_update_timeout(struct timeval *timeout, struct timeval *interval){
  timeout->tv_usec += interval->tv_usec;
  timeout->tv_sec += interval->tv_sec;
  if(timeout->tv_usec >= 1000000L){
    timeout->tv_usec -= 1000000L;
    timeout->tv_sec += 1;
  }
}


struct timeval * glogger_checktime(struct timeval *now,
				   struct timeval *timeout){
  static struct timeval returntime;

  if(now->tv_sec > timeout->tv_sec){
    return NULL;                /* TIMEOUT */
  }
  else if(now->tv_sec == timeout->tv_sec){
    if(now->tv_usec >= timeout->tv_usec){
      return NULL;              /* TIMEOUT */
    }
  }
  /* timeout > now */
  returntime.tv_sec = timeout->tv_sec - now->tv_sec;
  returntime.tv_usec = timeout->tv_usec - now->tv_usec;
  if(returntime.tv_usec < 0){
    returntime.tv_usec += 1000000L;
    returntime.tv_sec -= 1;
  }
  return &returntime;
}


#ifdef HOOK_snmp_free_pdu
/* hook from snmp library*/
void snmp_free_pdu(struct snmp_pdu *pdu){
  //printf("*** hooked snmp_free_pdu \n");

  if (!pdu) return;

  //printf("command = %d\n", pdu->command);

  if(pdu->command != SNMP_MSG_GET){ /* not request */
#ifdef USE_DLOPEN
    original_snmp_free_pdu(pdu);
    //printf("*** call original snmp_free_pdu  ok \n");
#else
#ifndef USE_UCDSNMP    /* for net-snmp */
    {
      struct snmp_secmod_def *sptr;
      if ((sptr = find_sec_mod(pdu->securityModel)) != NULL &&
	  sptr->pdu_free != NULL) {
        (*sptr->pdu_free) (pdu);
      }
    }
#endif /* ifndef USE_UCDSNMP */
    snmp_free_varbind(pdu->variables);
    SNMP_FREE(pdu->enterprise);
    SNMP_FREE(pdu->community);
    SNMP_FREE(pdu->contextEngineID);
    SNMP_FREE(pdu->securityEngineID);
    SNMP_FREE(pdu->contextName);
    SNMP_FREE(pdu->securityName);
#ifndef USE_UCDSNMP  /* for net-snmp */
    SNMP_FREE(pdu->transport_data);
#endif /* ifndef USE_UCDSNMP */
    free((char *)pdu);
#endif /* USE_DLOPEN */
  }

  /* else: do nothing */
}

void glogger_renew_id_requests(struct snmp_pdu *pdu){
  pdu->reqid  = snmp_get_next_reqid();
  pdu->msgid  = snmp_get_next_msgid();  
}
#endif /* HOOK_snmp_free_pdu */


void set_value_to_dbgmap(unsigned long *val, char *setstr,
			 unsigned short mibtype){
  char *p, *str;
  double f;

  //printf("setstr: %s\n", setstr);
  //printf("mibtype: %d\n", mibtype);

  switch (mibtype) {
  case TYPEMODE_load:      /* for load average -> *100 */
    /* ex. STRING: 1.58 */
    str = setstr;
    do {
      f = strtod(str, &p);
      //printf("   --> %s\n", p);
      str++;
    } while(*p != '\0');
    //printf("  float: %f\n", f);

    *val = (unsigned long) rint(f*100);

    break;
  case TYPEMODE_interface:      /* get tail value */
    /* ex. Counter32: 1609014642 */
    str = setstr;
    do {
      *val = strtoul(str, &p, 10);
      //printf("   %lu --> %s\n", *val, p);
      str++;
    } while(*p != '\0');
    break;
  case TYPEMODE_uptime:     /* uptime */
    /* ex. Timeticks: (346391518) 40 days, 2:11:55.18  */
    /*                 ^^^^^^^^^ */
    str = setstr;
    do {
      *val = strtoul(str, &p, 10);
      //printf("   %lu --> %s\n", *val, p);
      str++;
    } while(*p != ')' && *p != '\0');
    break;
  default:
    *val = 0;
  }

  //printf(" --> value: %lu\n", *val);
}

/* snmp async get -------------------------------------------------------- */

#define BUFSIZE SPRINT_MAX_LEN
//#define BUFSIZE 128
static char sprintbuf[BUFSIZE];

static int glogger_response_callback(int operation, struct snmp_session *sp,
				     int reqid,
				     struct snmp_pdu *pdu, void *magic){
  struct req_list *reqlist = (struct req_list*) magic;
  struct variable_list *vp;
  struct req_oids *oids;
  //int buflen, i;

  vp = pdu->variables;
  oids = reqlist->reqoids;

  //printf("id=%d: %s\n", reqid, reqlist->node->nodenick);

  if(operation == GLOGGER_OP_RECEIVED_MESSAGE){ 
    if (pdu->errstat == SNMP_ERR_NOERROR) {
      while(vp && oids){
	//printf("%s\n", oids->oidval->oidnick);
	//print_variable(vp->name, vp->name_length, vp);
	
#if 0  	  /* test : snprint_value */
	buflen = 0; 
	do { 
	  buflen++;
	  i = snprint_value(sprintbuf, buflen, vp->name, vp->name_length, vp);
	} while(i < 0);
	printf("len = %d, buflen = %d\n", i, buflen);

	if(vp->type == ASN_OCTET_STR){
	  printf("vp->type == ASN_OCTET_STR\n");
	}
#else
	snprint_value(sprintbuf, BUFSIZE, vp->name, vp->name_length, vp);
#endif

	//printf("%s: %s\n", oids->oidval->oidnick, sprintbuf);

	/* valid */
	oids->dbgmap->flag |= FLAG_VALID;
	/* OK */
	oids->dbgmap->flag |= FLAG_OK;

	set_value_to_dbgmap(&oids->dbgmap->mibval, sprintbuf,
			    oids->oidval->mibtype);

	//print_objid(oids->oidval->oid, oids->oidval->oidlen);

	/* by same turns */
	vp = vp->next_variable;
	oids = oids->next;
      }
    }
    else {
      printf("pdu->errstat = %ld (%s)\n", pdu->errstat, snmp_errstring(pdu->errstat));
      /* do nothing */
    }
  }
  else {    /* STAT_TIMEOUT */
    while(oids){
      /* valid (only) */
      oids->dbgmap->flag |= FLAG_VALID;
      oids = oids->next;
    }
    printf("*** timeout: %s\n", reqlist->node->nodenick);
  }

  glogger_active_hosts--;
  return 1;
}

void glogger_init_targets(struct nodelist *nodelist){
  struct snmp_session sess;
#ifdef USE_DLOPEN
  /* when using dlopen() */
  void *handle;
  char *error;

  handle = dlopen(USE_DLOPEN, RTLD_LAZY);
  printf("dll = %s\n", USE_DLOPEN);
  if (!handle) {
    fputs (dlerror(), stderr);
    exit(1);
  }
  original_snmp_free_pdu = dlsym(handle, "snmp_free_pdu");
  if ((error = dlerror()) != NULL)  {
    fprintf (stderr, "%s\n", error);
    exit(1);
  }
#endif

  snmp_sess_init(&sess);
  //sess.version = SNMP_VERSION_2c;
  sess.version = SNMP_VERSION_1;
  sess.callback = glogger_response_callback;
  //sess.timeout = 100000L;
  sess.timeout = 0L;
  sess.retries = 0;

  glogger_active_hosts = 0;
  while(nodelist){
    sess.peername = nodelist->nodename;
    sess.community = nodelist->community;
    sess.community_len = strlen(sess.community);

    if (!(nodelist->session = snmp_open(&sess))) {
      snmp_perror("snmp_open");
      return;
    }
    nodelist = nodelist->next;
  }
}



#ifdef PRINTTIME
void printtime(struct timeval *time, char *name){
  struct tm *tm;
  char buf[128];

  if(time == NULL){
    fprintf(stdout, "%s\n", name);
  }
  else {
    rdtscll(rdtsc_now);
    //printf("clock %d\n", rdtsc_now-rdtsc_start);
    tm = localtime(&time->tv_sec);
    //    fprintf(stdout, "%.2d:%.2d:%.2d.%.6ld : %s\n",
    //    tm->tm_hour, tm->tm_min, tm->tm_sec, time->tv_usec,
    //    name);
    fprintf(stdout, "%.2d:%.2d:%.2d.%.6ld : %lu",
	    tm->tm_hour, tm->tm_min, tm->tm_sec, time->tv_usec,
	    rdtsc_now-rdtsc_start);
    fprintf(stdout, " : %s\n", name);
  }
}

#endif /* PRINTTIEM */

/*  
    return value:
    0: timeout
    1: receiving ok
    2: no request
*/
int count=1;
int glogger_snmpget(struct req_list *reqlist, struct timeval *timeout){
  struct snmp_pdu *reqpdu;
  struct timeval now;
  struct timeval *ct;    /* for checktime */
  struct req_oids *reqo;
#ifdef PRINTTIME
  int getfirst = 0;      /* flag */
#endif

  printf("=== %d ===\n", count++);
  
  gettimeofday(&now, NULL);
#if 0
  //#ifdef PRINTTIME
  printtime(&now, "START");
#endif

  if(reqlist == NULL){
    printf("NOREQUEST\n");
    return 2;
  }

  /* check first */
  ct = glogger_checktime(&now, timeout);
  if(ct == NULL){
    fprintf(stdout, "TIMEOUT1\n");
    return 0;
  }

  /* ----- request ----- */
#ifdef PRINTTIME
  gettimeofday(&now, NULL);
  printtime(&now, "REQUEST");
#endif
  glogger_active_hosts = 0;      /* initialize */
  while(reqlist){
    //reqset->host->nowreq = reqset; /* set current request */
    
    /* prepare request */
#ifdef HOOK_snmp_free_pdu
    if(reqlist->reqpdu == NULL){
      reqpdu = snmp_pdu_create(SNMP_MSG_GET);
      reqo = reqlist->reqoids;
      while(reqo != NULL){
	snmp_add_null_var(reqpdu, reqo->oidval->oid, reqo->oidval->oidlen);
	reqo = reqo->next;
      }
      reqlist->reqpdu = reqpdu;
    }
    else {
      glogger_renew_id_requests(reqlist->reqpdu);
    }
    reqpdu = reqlist->reqpdu;
#else
    reqpdu = snmp_pdu_create(SNMP_MSG_GET);
    reqo = reqlist->reqoids;
    while(reqo != NULL){
      snmp_add_null_var(reqpdu, reqo->oidval->oid, reqo->oidval->oidlen);
      reqo = reqo->next;
    }
#endif
    //printf("%s\n", reqlist->node->nodenick);
    reqlist->node->session->callback_magic = reqlist;
    if(snmp_send(reqlist->node->session, reqpdu)){
      glogger_active_hosts++;
      //printf("*** snmp_send: OK\n");
    }
    else {
      snmp_perror("snmp_send");
      snmp_free_pdu(reqpdu);
    }
    reqlist = reqlist->next;
  }

#ifdef PRINTTIME
  gettimeofday(&now, NULL);
  printtime(&now, "WAIT");
#endif

  /* ----- recieve and save ----- */
  /* loop while any active hosts */
  while(glogger_active_hosts) {
    int fds = 0, block = 0;
    fd_set fdset;
    struct timeval selecttimeout;

    gettimeofday(&now, NULL);
    ct = glogger_checktime(&now, timeout);
    if(ct == NULL){
      snmp_timeout();
      //printf("*** timeout: active_hosts = %d\n", active_hosts);
      fprintf(stdout, "TIMEOUT2\n");
      return 0;
    }
    FD_ZERO(&fdset);

    printf("*** remaining hosts: %d\n", glogger_active_hosts);

    snmp_select_info(&fds, &fdset, &selecttimeout, &block);
    selecttimeout.tv_sec = ct->tv_sec;
    selecttimeout.tv_usec = ct->tv_usec;
    fds = select(fds, &fdset, NULL, NULL, block ? NULL : &selecttimeout);
    //printf("*** select end %d\n", fds);
    if(fds > 0) {
#ifdef PRINTTIME
      if(getfirst == 0){
	gettimeofday(&now, NULL);
	printtime(&now, "RECEIVE");
	getfirst = 1;
      }
#endif
      snmp_read(&fdset);
#if 0
//#ifdef PRINTTIME
      gettimeofday(&now, NULL);
      printtime(&now, "snmp_read");
#endif
    }
    else if(fds == 0){
      //printf("no response\n");
    }
    else {   /* when catching signal or timeout */
#if 0
      switch(errno) {
      case EBADF:
        printf("EBADF\n");
        break;
      case EINTR:
        printf("EINTR\n");
        break;
      case EINVAL:
        printf("EINVAL\n");
        break;
      case ENOMEM:
        printf("ENOMEM\n");
        break;
      }
#endif
#ifdef PRINTTIME
      gettimeofday(&now, NULL);
      printtime(&now, "CANCEL");
#endif
      snmp_timeout();
      return -1;
    }
  }
  return 1;
}
