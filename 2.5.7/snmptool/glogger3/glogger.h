#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/select.h>
#include <netinet/in.h>

#ifdef USE_UCDHEADER
#include <ucd-snmp/ucd-snmp-config.h>
#include <ucd-snmp/ucd-snmp-includes.h>
#else
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#endif

/* ucd-snmp */
#ifdef RECEIVED_MESSAGE
#define GLOGGER_OP_RECEIVED_MESSAGE RECEIVED_MESSAGE
#define USE_UCDSNMP
//#warning "using snmp library: ucd-snmp"
/* net-snmp */
#elif defined(NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE)
#define GLOGGER_OP_RECEIVED_MESSAGE NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE
//#warning "using snmp library: net-snmp"
#else
#error "unsupported snmp library: cannot determine callback operations"
#endif

int fsync_count;
int fsync_timing;

//#define DEBUGCONF /* read_glogger_conf.c */

//#define PRINTVALUE  /* write_to_files.c */

/* default setting ---------------------------------------------------- */
#define MAXLINE 1024
#define MAXPATH 512	     /* path length */
#define MAXCOMMLEN 16 	     /* community name length */
#define MAXLABELLEN 16	     /* label length */
#define MAXDEFAULTDIRLEN 64  /* default_dir length */
#define MAXOVERWRITELEN 8	/* overwrite mode */
#define MAXINTERVALCACHE 16     /* for cachegcd, cachelcm */
#define MAXFSYNCLEN 8		/* fsync mode */

#define MAXINTERVAL	 3600   /* sec */
#define MININTERVAL     100000 	/* microsec */

#define DEFAULTSEC    10
#define DEFAULTUSEC    0
#define DEFAULTOUTPUT  0       /* KB */
#define DEFAULTMMAPOUTPUT  5  /* KB */
#define DEFAULTOVERWRITE "OFF" /* ON, OFF, enable, disable */
#define DEFAULTPATH   "%G/%L_%y%m%d%h%M.glg"
#define DEFAULTCOMM   "public"
#define DEFAULTDIR    "/var/log/glogger"
#define DEFAULTLABEL  "glogger"
#define DEFAULTFSYNC  "OFF"  /* ON, OFF, enable, disable */
#define DEFAULTFSYNCMODE -1  /* -1:off, 0:every, 1:1second, 2:... */

#define OUTDATA_VERSION 1

/* value flag 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80 */
#define FLAG_VALID 0x01
#define FLAG_OK    0x02

/* key words in config file */
#define KEYSEC "interval_sec"
#define KEYMICROSEC "interval_usec"
#define KEYINTERVAL "interval"	   /* x.xxx (milisecond) */
#define KEYDEFAULTDIR "defaultdir"
#define KEYOUTMAX "maxfilesize"
#define KEYOVERWRITE "overwrite"
#define KEYLABEL "label"
#define KEYPATH "path"
#define KEYCOMMUNITY "community"
#define KEYFSYNC "fsync_interval"

/* mib types */
#define TYPE_laLoad      "laLoad"
#define TYPE_ifOutOctets "ifOutOctets"
#define TYPE_ifInOctets  "ifInOctets"
#define TYPE_dskUsed     "dskUsed"
#define TYPE_dskAvail    "dskAvail"
#define TYPE_sysUpTime   "sysUpTime"

#define TYPEMODE_load      1
#define TYPEMODE_interface 2
#define TYPEMODE_disk      2
#define TYPEMODE_uptime    3
#define TYPEMODE_default   2

/* at present time */
extern struct tm now_tm;
extern struct timeval nowtime;

/* output mode definition */
extern int global_outputsizemax;    /* KiloBytes: *1024 */
//extern struct timeval global_fileinterval;
extern char global_overwrite[MAXOVERWRITELEN];
extern char global_changefile;
extern int burstbuffersize;
//extern char global_fsync[MAXFSYNCLEN];
extern int global_fsyncmode; 	/* -1:off, 0:every, 1:1second, 2:... */

#define BUFMODE_NONE  0	  /* fwrite() every timing */
#define BUFMODE_DBG   1	  /* every data block group */
#define BUFMODE_BURST 2	  /* write to only real memory */
#define BUFMODE_MMAP  3   /* use mmap() */
extern int bufmode;

#ifdef PRINTTIME
#define rdtscll(val) \
     __asm__ __volatile__("rdtsc" : "=A" (val))

unsigned long long rdtsc_start;
unsigned long long rdtsc_now;
#endif

/* --------------------------------------------------------------------- */
#if 0 // use?
extern struct nodelist *topnodes;

extern struct timeval global_interval;
extern char *global_defaultdir;

extern char *global_label;
extern char global_path[MAXPATH];
extern char *global_community;

extern struct timeval requests_interval;
extern char requests_path[MAXPATH];
extern char *requests_community;

extern struct timeval mib_interval;
extern char *mib_path;
#endif


/* --------------------------------------------------------------------- */
struct hostdefine {
  char *ip;
  char *fqdn;
  char *nick;
  char *community;
  struct hostdefine *next;
};

struct oiddefine {
  //char *oidasn1;
  char *oidname;
  char *nick;
  struct oiddefine *next;
};

struct intervalmap {
  uint16_t hostindex;
  uint16_t oidindex;
  uint32_t interval_sec;
  uint32_t interval_usec;

  struct oidlist *myoid;

  struct intervalmap *next;
};

/* dbg: data block group */
struct dbg_map {  /* data block group pattern map */
  struct intervalmap *im;   /* NULL is separater */

  unsigned char flag;
  unsigned long mibval;   /* 32bit for saving a recieved mib value */

  struct dbg_map *next;	/* end: NULL */
};

struct filelist {
  char *path;			/* only time rule: ...%t... */
  char *nowpath; 		/* current output file name */
  FILE *fp;
  int fd; 			/* file descriptor */

  //unsigned long filesize;	/* config -> buf_max */
  //struct timeval changefile;

  char *buf;			/* length of dbg_size * buf_num */
  char *buf_point;
  unsigned long buf_count;	/* a number of copied data to buf */
  unsigned long buf_num;	/* a number of dbg */
  unsigned long buf_max;	/* dbg_size * buf_num */

  struct timeval interval_gcd;
  struct timeval interval_lcm;

  /* metadata1 */
  void *metadata1;
  uint16_t metadata1size;
  uint16_t version;
  uint32_t dbg_size;
  uint32_t start_sec;
  uint32_t start_usec;
  uint32_t dbg_interval_sec;	/* = lcm of intervals in this file */
  uint32_t dbg_interval_usec;

  /* metadata2 */
  char *metadata2;
  uint32_t metadata2size;
  uint16_t hostdefsize;
  struct hostdefine *hostdef;
  uint16_t oiddefsize;
  struct oiddefine *oiddef;
  uint16_t imsize;
  struct intervalmap *interval_map;
  uint16_t dbgmapsize;
  struct dbg_map *dbgmap;
  struct dbg_map *dbgmap_pointer;   /* next writing timing */

  struct filelist *next; /* end: NULL */
};

struct nodelist;

struct oidlist {
  struct nodelist *mynode;	/* belong to */
  struct filelist *myfile;	/* belong to */

  char *oidname;
  char *oidnick;
  oid *oid;   	 /* length: MAX_OID_LEN */
  int oidlen;
  char *pathrule;
  char *path;	      /* %t is not converted */
  struct timeval interval;

  /* convert type of MIB data value */
  /* set this value in glogger_mibtype() */
  /* use in glogger_response_callback() -> set_value_to_dbgmap() */
  /* 0: unknown */
  /* 1: for load average  ( OCTETSTRING -> atoi()*100 ) */
  /* 2: 32bit unsigned number, INTERGER or COUNTER */
  /* ... */
  unsigned short mibtype;	  /* != SNMP MIB data type */

  struct oidlist *next; /* end: NULL */
};

struct nodelist {
  char *nodename;
  char *nodenick;
  char *community;
  struct snmp_session *session;

  struct oidlist *oidval;
  struct nodelist *next;  /* end: NULL */
};


struct req_oids {
  struct oidlist *oidval;
  struct dbg_map *dbgmap;  	/* target of file at now request */

  struct req_oids *next;  /* end: NULL */
};

/* requests of same timing */
struct req_list {
  struct nodelist *node;

#ifdef HOOK_snmp_free_pdu
  /* If I can hook snmp_free_pdu(), I can use saved pdu. */
  struct snmp_pdu *reqpdu;
#endif
  /* else */
  struct req_oids *reqoids; 	/* request oids of now node at now timing */

  struct req_list *next;   /* end: NULL */
};

struct target_files {
  struct filelist *file;
  struct target_files *next;
};

struct req_loop {
  //int index;			/* 1,2,3,...,n, 0 : 0 is end of loop */
  struct req_list *reqlist;  /* NULL is no request of the timing */
  struct target_files *targets;

  struct req_loop *next;	/* end: top -> loop */
};


#if 1
struct options {
  char *conffile;  /* -config: configuration file*/
  char *defaultdir;  /* -directory : output directory  -> %G */
  char *pidfile;    /* PID number to this file */
  char asyncmode;   /* 1: ON, 0: OFF */
  char checkconf;   /* 1: ON, 0: OFF */
  char *burst_path;
  char *mmap_path;
  int mmap_maxsize;   /* KB */
};
#endif

extern struct options opt;


/* read_glogger_conf */
struct nodelist * glogger_get_top(void);
struct timeval * glogger_get_nowtime(void);
void glogger_print_conf(struct nodelist *nodes);
void glogger_renew_time(void);
void glogger_init_read_conf(void);
int  glogger_read_conf(char *configfilename);
void glogger_set_path_to_nodes(struct nodelist *nodes, char *path);

/* prepare_files.c */
struct filelist * glogger_set_filelist(struct nodelist *nodes);
void glogger_free_filelist(struct filelist *files);

//int glogger_calc_interval(struct timeval *setinterval, struct nodelist *nodes);
int glogger_compare_and_change_filename(struct filelist *file);
int glogger_open_file(struct filelist *file);
int glogger_open_files(struct filelist *files);

int glogger_write_metadata(struct filelist *file);
int glogger_write_metadata_all(struct filelist *files);

int glogger_close_file(struct filelist *file);
int glogger_close_files(struct filelist *files);
struct req_loop * glogger_prepare_requests(struct nodelist *nodes,
					   struct timeval *setinterval);
struct req_loop *
glogger_prepare_requests_from_files(struct filelist *files,
				    struct timeval *setinterval);
int comparetime(struct timeval *m, struct timeval *n);

/* snmpasync.c */
void printtime(struct timeval *time, char *name);
void glogger_init_targets(struct nodelist *nodes);
int glogger_snmpget(struct req_list *reqlist, struct timeval *timeout);
void glogger_update_timeout(struct timeval *timeout, struct timeval *interval);
void glogger_sleep(struct timeval *sleeptime);
struct timeval * glogger_checktime(struct timeval *now, struct timeval *timeout);


/* write_to_files.c */
/* function pointer for write */
extern size_t (*write_func)(const  void  *ptr,  size_t  size,  size_t  nmemb,
			    struct filelist *file);

size_t glogger_fwrite(const  void  *ptr,  size_t  size,  size_t  nmemb,
		      struct filelist *file);
size_t glogger_mmap(const  void  *ptr,  size_t  size,  size_t  nmemb,
		    struct filelist *file);
size_t glogger_write_mybuf(const  void  *ptr,  size_t  size,  size_t  nmemb,
			   struct filelist *file);

void glogger_write(struct timeval *time, struct target_files *targets);

void glogger_fflush_files(struct filelist *files);
int glogger_check_and_change_file(struct filelist *files,
				  struct timeval *changetime);
void glogger_next_changetime(struct timeval *changetime);


/* set_options.c */
void daemon_mode();
int glogger_read_args(int argc, char **argv);

/* common */
//void glogger_snmp_free_pdu(struct snmp_pdu *pdu);
