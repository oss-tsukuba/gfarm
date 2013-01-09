#include "glogger.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define OPT_CONFIG    "-config"
#define OPT_BUFMODE   "-bufmode"
#define OPT_BURSTMODE "-burst"
#define OPT_MMAPMODE  "-mmap"
#define OPT_MAXSIZE   "-maxsize"
#define OPT_CHECKCONF "-checkconf"
#define OPT_DAEMON    "-daemon"

//static char *defaultconf = "./test.conf";
static char *defaultconf = "/etc/glogger/glogger.conf";
static char *default_burstout = "./burstout.glg";
static char *default_mmapout = "./mmap.glg";


void daemon_mode(){
  FILE *fp;
  pid_t pid;
  int fd;

  daemon(1, 0);
  fp = fopen(opt.pidfile, "w");
  if(fp == NULL){
    remove(opt.pidfile);
    exit(1);
  }
  fprintf(fp, "%d", getpid());
  fclose(fp);
}

int glogger_read_args(int argc, char **argv){
  /* default */
  global_outputsizemax = DEFAULTOUTPUT;
  bufmode = BUFMODE_NONE;
  opt.conffile = defaultconf;
  opt.pidfile = NULL;
  opt.checkconf = 0;
  opt.burst_path = default_burstout;
  opt.mmap_path = default_mmapout;
  opt.mmap_maxsize = DEFAULTMMAPOUTPUT;
  opt.pidfile = NULL;

  argv++;      /* skip argv[0] */
  while(*argv){
    /* input config file */
    if(strncmp(*argv, OPT_CONFIG, strlen(OPT_CONFIG)) == 0){
      argv++;
      if(*argv == NULL){
	printf("Error: %s [config file | - (stdin)]\n", OPT_CONFIG);
	exit(3);
      }
      if(opt.pidfile != NULL && (strncmp(*argv, "-", strlen(*argv)) == 0) ){
	printf("Error: cannot use \"-\" when using \"%s\"\n", OPT_DAEMON);

      }
      if((*argv)[0] == '-' && (*argv)[1] != '\0'){
	printf("Error: %s %s\n", OPT_CONFIG, *argv);
	exit(3);
      }
      opt.conffile = *argv;
    }
    /* for perfomance test */
    else if(strncmp(*argv, OPT_BUFMODE, strlen(OPT_BUFMODE)) == 0){
      argv++;
      if(*argv == NULL || *argv[0] == '-'){
	printf("Error: %s [0:normal, 1:every dbg, 2:burst, 3:mmap]\n", OPT_BUFMODE);
	exit(3);
      }
      bufmode = atoi(*argv);
    }
    /* burst mode / plan: using mloc()... !!!!!! */
    else if(strncmp(*argv, OPT_BURSTMODE, strlen(OPT_BURSTMODE)) == 0){
      argv++;
      if(*argv == NULL || *argv[0] == '-'){
	printf("Error: %s [output file]\n", OPT_BURSTMODE);
	exit(3);
      }
      bufmode = BUFMODE_BURST;
      opt.burst_path = *argv;
    }
    /* mmap() mode (target file is only one) */
    else if(strncmp(*argv, OPT_MMAPMODE, strlen(OPT_MMAPMODE)) == 0){
      argv++;
      if(*argv == NULL || *argv[0] == '-'){
	printf("Error: %s [output file]\n", OPT_MMAPMODE);
	exit(3);
      }
      bufmode = BUFMODE_MMAP;
      opt.mmap_path = *argv;
    }
    /* max output size */
    else if(strncmp(*argv, OPT_MAXSIZE, strlen(OPT_MAXSIZE)) == 0){
      argv++;
      if(*argv == NULL || *argv[0] == '-'){
	printf("Error: %s [max file size(KB)]\n", OPT_MAXSIZE);
	exit(3);
      }
      global_outputsizemax = opt.mmap_maxsize = atoi(*argv);
    }
    /* check cofiguration file */
    else if(strncmp(*argv, OPT_CHECKCONF, strlen(OPT_CHECKCONF)) == 0){
      opt.checkconf = 1;
    }
#if 0
    /* snmpasyncget */
    else if(strncmp(*argv, "-asyncget", strlen(*argv)) == 0){

    }
#endif
    /* daemon mode */
    else if(strncmp(*argv, OPT_DAEMON, strlen(OPT_DAEMON)) == 0){
      argv++;
      if(*argv == NULL){
	printf("Error: %s [PID file]\n", OPT_DAEMON);
	exit(3);
      }
      if(strncmp(opt.conffile, "-", strlen(opt.conffile)) == 0){
	printf("Error: cannot use \"%s\" when using \"%s -\"\n", OPT_DAEMON, OPT_CONFIG);
	exit(3);
      }
      opt.pidfile = *argv;
    }
    else {
      printf("unknown option: %s\n", *argv);
      printf("options: %s %s %s %s %s\n",
	     OPT_CONFIG,
	     //OPT_BUFMODE,
	     //OPT_BURSTMODE,
	     OPT_MMAPMODE,
	     OPT_MAXSIZE,
	     OPT_CHECKCONF,
	     OPT_DAEMON
	     );
      exit(3);
    }
    argv++;
  }

  return 1;
}
