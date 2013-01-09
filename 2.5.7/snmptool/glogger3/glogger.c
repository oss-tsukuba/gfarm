#include "glogger.h"
#include <signal.h>

/* local */
struct timeval timeout;
int restart = 0;
int interrupt = 0;

/* global */
struct options opt;
int bufmode;
size_t (*write_func)(const  void  *ptr,  size_t  size,  size_t  nmemb,
		     struct filelist *file);

static void restart_handler(int sig){
  restart = 1;
  printf("*** Hangup!\n");
}

static void interrupt_handler(int sig){
  interrupt = 1;
  printf("*** Interrupt!\n");
}

static void test_handler(int sig){
  printf("*** signal!!!: %d\n", sig);
}

#if 0
size_t debug_write(const  void  *ptr,  size_t  size,  size_t  nmemb,
		   struct filelist *file){
  int n = size*nmemb;
  char i = 0;
  while(i<n){
    if(bufmode == BUFMODE_MMAP){
      printf("a ");
      memcpy(file->buf_point, &i, 1);
      file->buf_point++;
    }
    else {
      fwrite(&i, 1, 1, file->fp);
    }
    i++;
  }
  file->buf_count += n;
  return n;
}
#endif

int main(int argc, char **argv){
  struct req_loop *reqloop, *reqloop_top;
  struct nodelist *topnodes;
  struct timeval maininterval, time, changetime;
  struct filelist *topfiles;
#ifdef PRINTTIME
  struct timeval now;
#endif
  int retv;
  int pid;
#define USEsigaction
#ifdef USEsigaction
  int i;
  struct sigaction int_sa, int_old, hup_sa, hup_old;
#endif

#ifdef PRINTTIME
  rdtscll(rdtsc_start);
#endif

  /* read and set options */
  glogger_read_args(argc, argv);

#if 1
  if(bufmode == BUFMODE_NONE){ 		/* every fwrite() */
    write_func = glogger_fwrite;
  }
  else if(bufmode == BUFMODE_MMAP){    /* mmap() */
    write_func = glogger_mmap;
  }
  else if(bufmode == BUFMODE_DBG || bufmode == BUFMODE_BURST){
    /* burstmode or normal buffering(every dbg) */
    write_func = glogger_write_mybuf;
  }
  else {
    printf("Error: bufmode\n");
    exit(1);
  }
#endif
  //write_func = debug_write;

  /* read_glogger_conf */
  glogger_init_read_conf();
  glogger_read_conf(opt.conffile);

  topnodes = glogger_get_top();

  //printf("global_fsyncmode = %d\n", global_fsyncmode);

  if(bufmode == BUFMODE_BURST){
    /* over write file name for burst mode */
    /* target file is only one */
    glogger_set_path_to_nodes(topnodes, opt.burst_path);
  }
  else if(bufmode == BUFMODE_MMAP){
    /* priority: option > config > default */
    if(opt.mmap_maxsize != DEFAULTMMAPOUTPUT ||
       global_outputsizemax == DEFAULTOUTPUT){
      global_outputsizemax = opt.mmap_maxsize;
    }
    glogger_set_path_to_nodes(topnodes, opt.mmap_path);
  }

  /* check configuration file -> exit */
  if(opt.checkconf == 1){
    glogger_print_conf(topnodes);
    return 1;			/* exit */
  }

  /* -daemon */
  if(opt.pidfile != NULL){
    daemon_mode();
  }

  topfiles = glogger_set_filelist(topnodes);
  glogger_open_files(topfiles);
  glogger_write_metadata_all(topfiles);

  fsync_count = 0;
  fsync_timing = 0;

  glogger_fflush_files(topfiles);
  //glogger_close_files(topfiles);
  //glogger_free_filelist(topfiles);

  reqloop = glogger_prepare_requests_from_files(topfiles, &maininterval);
  reqloop_top = reqloop;

  /* prepare fsync timing */
  fsync_count = 0;
  fsync_timing = 0;
  if(global_fsyncmode == -1){
  }
  else if(maininterval.tv_sec > 0){
    fsync_timing = global_fsyncmode / maininterval.tv_sec;
  }
  else {
    fsync_timing = global_fsyncmode * (1000000L / maininterval.tv_usec);
  }
  printf("global_fsyncmode = %d, fsync_timing = %d\n", global_fsyncmode, fsync_timing);

#ifdef USEsigaction
#if 0
  hup_sa.sa_handler = test_handler;
  hup_sa.sa_flags = 0;
  for(i=0; i<=31; i++){
    sigaction(i, &hup_sa, NULL);
  }
#endif
  hup_sa.sa_handler = restart_handler;
  int_sa.sa_handler = interrupt_handler;
  hup_sa.sa_flags = int_sa.sa_flags = 0;
  //hup_sa.sa_flags = int_sa.sa_flags = SA_ONESHOT;
  sigaction(SIGHUP, &hup_sa, &hup_old);
  sigaction(SIGINT, &int_sa, &int_old);
#else
  signal(SIGHUP, restart_handler);
  signal(SIGINT, interrupt_handler);
#endif

  /* snmpasync.c */
  glogger_init_targets(topnodes);
  gettimeofday(&timeout, NULL);

#if 1
  while(restart != 1 && interrupt != 1){
    /* save current time for output data */
    time = timeout;
#ifdef PRINTTIME
  rdtscll(rdtsc_start);
  printtime(&time, "START");
#endif
    /* set a target time of next timeout */
    glogger_update_timeout(&timeout, &maininterval);

    /* request & recieve & saving in timeout */
    retv = glogger_snmpget(reqloop->reqlist, &timeout);
    if(retv == 1){		/* ok */
      /* write values to target files */
      glogger_write(&time, reqloop->targets);
      glogger_fflush_files(topfiles);
#ifdef PRINTTIME
      gettimeofday(&now, NULL);
      printtime(&now, "END");
#endif
      /* sleep */
      glogger_sleep(&timeout);
    }
    else if(retv == 2){		/* no request */
      //glogger_fflush_files(topfiles);
      glogger_sleep(&timeout);
    }
    else if(retv == -1){	/* interrupt */
      glogger_write(&time, reqloop->targets);
    }
    else {     /* timeout */
      glogger_write(&time, reqloop->targets);
      glogger_fflush_files(topfiles);
    }

    reqloop = reqloop->next;

    if(bufmode == BUFMODE_NONE && reqloop == reqloop_top){
      /* check files and chnge file names*/
      glogger_check_and_change_file(topfiles, &changetime);
    }
  } /* end of while() */
#endif

  /* restart or exit */

  /* padding */
  while(reqloop != reqloop_top){
    glogger_write(&time, reqloop->targets);
    reqloop = reqloop->next;
  }
  
  /* write end block */
  // glogger_write_endblock(topfiles);

  glogger_fflush_files(topfiles);

  if(restart == 1){
    pid = fork();
    if(pid < 0){
      exit(3);
    }
    if(pid > 0){
    }
    else {
      execv(argv[0], argv);
      perror("execv");
      exit(5);
    }
  }

  printf("*** exit\n");
  return 0;
}
