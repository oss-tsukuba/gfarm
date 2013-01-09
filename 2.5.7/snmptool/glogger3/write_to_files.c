#include "glogger.h"

#include <unistd.h>
#include <sys/mman.h>

/* write mib values to files ---------------------------------------------- */

/* see also: create_dbg_map() */
void fwrite_filebuf(struct filelist *file){
  int len;
  printf("fwrite_filebuf: fwrite()\n");

  len = fwrite(file->buf, 1, file->buf_count, file->fp);
  printf("fwrite len: %d\n", len);

  /* reset buf */
  file->buf_point = file->buf;
  file->buf_count = 0;
  return;
}

/* compatible with fwrite() */
size_t my_fwrite(const  void  *ptr,  size_t  size,  size_t  nmemb,
		 struct filelist *file){
  size_t n;
  unsigned long bufsize;

  n = size*nmemb;
  bufsize = ntohl(file->dbg_size)*file->buf_num;

  //printf("n: %d, buf_count: %lu, bufsize: %lu\n", n, file->buf_count, bufsize);

  file->buf_count += n;
  if(file->buf_count > bufsize){
    printf("buffer is full\n");
    file->buf_count -= n;
    fwrite_filebuf(file);
    exit(4);
  }
  memcpy(file->buf_point, ptr, n);
  
  if(file->buf_count == bufsize){	 /* full */
    fwrite_filebuf(file);
    if(bufmode == BUFMODE_BURST){
      printf("buffer is full: exit burst mode\n");
      exit(1);
    };
  }
  else {
    file->buf_point += n;
  }

  return n;
}

void glogger_fsync(int fd){
  if(fsync_count < fsync_timing){ 
    fsync_count++;
  } else {
    printf("fsync()\n");
    fsync(fd);
    fsync_count = 1;
  }
}

void glogger_fflush_files(struct filelist *files){
#ifdef PRINTTIME
  struct timeval now;
  gettimeofday(&now, NULL);
  printtime(&now, "FLUSH");
#endif
  while(files){
    if(bufmode == BUFMODE_MMAP){
      printf("msync: %s\n", files->path);
      msync(files->buf, files->buf_count, 0);
    }
    else if(bufmode != BUFMODE_NONE){
      fwrite_filebuf(files);
      printf("fflush()\n");
      fflush(files->fp);
      if(global_fsyncmode >= 0){
	glogger_fsync(fileno(files->fp));
      }
    }
    else {  /* BUFMODE_NONE */
      printf("fflush()\n");
      fflush(files->fp);
      if(global_fsyncmode >= 0){
	glogger_fsync(fileno(files->fp));
      }
    }
    files = files->next;
  }
}

static void change_file(struct filelist *file){
  printf("change file: %s\n", file->nowpath);
  glogger_close_file(file);
  glogger_open_file(file);
  glogger_write_metadata(file);
}


/* if(bufmode == BUFMODE_NONE && reqloop == reqloop_top) */
int glogger_check_and_change_file(struct filelist *files,
				  struct timeval *changetime){
  struct timeval nowtime;
  gettimeofday(&nowtime, NULL);

  //printf("%u, %u\n", global_fileinterval.tv_sec, changetime->tv_sec);

  glogger_renew_time();
  if(global_outputsizemax != 0){
    /* file size */
    while(files){
      if(files->buf_count >= files->buf_max){
	glogger_compare_and_change_filename(files);
	change_file(files);
      }
      files = files->next;
    }
  }
  else {
    /* path rule */
    while(files){
      if(glogger_compare_and_change_filename(files) > 0 ){
	change_file(files);
      }
      files = files->next;
    }
  }
  return 1;
}

/* --------------------------------------------------------------------- */
size_t glogger_fwrite(const  void  *ptr,  size_t  size,  size_t  nmemb,
		      struct filelist *file){
  int l, r, w;
  //printf("glogger_fwrite\n");
  l = size*nmemb;
  r = 0;
  while(l > r){
    w = fwrite(ptr, 1, l, file->fp);
    ptr += w;
    r += w;
  }
  file->buf_count += l;
  //printf("fwrite: count=%lu, max=%lu\n", file->buf_count, file->buf_max);
  return l;
}

size_t glogger_mmap(const  void  *ptr,  size_t  size,  size_t  nmemb,
		    struct filelist *file){
  int len = size*nmemb;
  struct timeval now;
  //printf("glogger_mmap\n");
  memcpy(file->buf_point, ptr, len);
  file->buf_point += len;
  file->buf_count += len;

  if(file->buf_count >= file->buf_max){
    printf("count(%lu) >= max(%lu)\n", file->buf_count, file->buf_max);
    printf("msync: %s\n", file->path);
    msync(file->buf, file->buf_count, 0);
#ifdef PRINTTIME
    gettimeofday(&now, NULL);
    printtime(&now, "END");
#endif
    exit(1);
  }

  return len;
}

size_t glogger_write_mybuf(const  void  *ptr,  size_t  size,  size_t  nmemb,
			   struct filelist *file){
  //printf("glogger_write_mybuf\n");
  return my_fwrite(ptr, size, nmemb, file);
}

/* write values to target files (write error flag)*/
void glogger_write(struct timeval *time, struct target_files *targets){
  struct dbg_map *dmap;
  uint32_t wtime_sec;
  uint32_t wtime_usec;
  uint32_t wvalue;
#ifdef PRINTTIME
  struct timeval now;
  gettimeofday(&now, NULL);
  printtime(&now, "WRITE");
#endif  

  //printf("write: time: %lu %lu\n", time->tv_sec, time->tv_usec);
  wtime_sec = htonl(time->tv_sec);
  wtime_usec = htonl(time->tv_usec);

  while(targets){
    printf("*** write: %s\n", targets->file->nowpath);
    dmap = targets->file->dbgmap_pointer;

    write_func((void*)&wtime_sec,  4, 1, targets->file);
    write_func((void*)&wtime_usec, 4, 1, targets->file);

    while(dmap && dmap->im){
      if(dmap->flag & FLAG_VALID){
#ifdef PRINTVALUE
	if(! (dmap->flag & FLAG_OK)){
	  printf("timeout! ");
	}
	printf(" -> %s %s %lu\n", dmap->im->myoid->mynode->nodenick, dmap->im->myoid->oidnick, dmap->mibval);
#endif
	/* success: send, receive, write */
      }
      write_func((void*)&dmap->flag, 1, 1, targets->file);
      wvalue = htonl(dmap->mibval);
      write_func((void*)&wvalue, 4, 1, targets->file);
      
      /* reset for next */
      dmap->flag = 0;
      dmap->mibval = 0;

      dmap = dmap->next;
    }
    while(dmap && !dmap->im){ 	/* skip separater */
	dmap = dmap->next;
    }
    if(dmap == NULL){
      targets->file->dbgmap_pointer = targets->file->dbgmap;
    }
    else {
      targets->file->dbgmap_pointer = dmap;
    }
    targets = targets->next;
  }
}
