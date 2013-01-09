#include "glogger.h"
#include <stdio.h>
//#include <netinet/in.h>
#include <stdlib.h>
#include <time.h>

struct dbgmap {
  int n;      /* the number */
  char *map;
  struct dbgmap *next;
};


/* ------------------------------------------------------------------------ */
void print_blockname(char *str){
  printf("# %s\n", str);
}

int read16(uint16_t *n, FILE *fp){
  int r;
  r = fread(n, 1, 2, fp);
  if(r == 0){
    exit(1);
  }
  *n = ntohs(*n);
  return r;
}

int read32(uint32_t *n, FILE *fp){
  int r;
  r = fread(n, 1, 4, fp);
  if(r == 0){
    exit(1);
  }
  *n = ntohl(*n);
  return r;
}

char *readvariable(unsigned long n, FILE *fp){
  char *c;
  c = (char*) calloc(1, n);
  if(c == NULL){
    printf("Error: calloc()\n");
    exit(1);
  }
  if(fread(c, 1, n, fp) == 0){
    exit(1);
  }
  return c;
}

char *mytime(const time_t *n){
  struct tm *tm;
  static char str[32];
  tm = localtime(n);
  snprintf(str, 32, "%.4d/%.2d/%.2d %.2d:%.2d:%.2d",
	   tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	   tm->tm_hour, tm->tm_min, tm->tm_sec);

  return str;
  //return ctime((long*)n);
}

int print_bit(FILE *fp){
  char c;
  int i;
  int r;
  printf(":");
  r = fread(&c, 1, 1, fp);
  if(r == 0){
    exit(1);
  }
  for(i=0; i<8; i++){
    if(c & (0x80 >> i)){
      printf("1");
    }
    else {
      printf("0");
    }
  }
  return r;
}

/* ------------------------------------------------------------------------ */
void metadata1check(FILE *fp){
  uint16_t n16;
  read16(&n16, fp);
  if(n16 != 22){
    printf("*** metadata1check: size = %u\n", n16);
    exit(1);
  }
}

void VersionOfFormat(FILE *fp){
  uint16_t n16;
  read16(&n16, fp);
  printf("VersionOfFormat::%u\n", n16);
}

void DataBlockGroupSize(FILE *fp){
  uint32_t n32;
  read32(&n32, fp);
  printf("DataBlockGroupSize::%u\n", n32);
}

void StartDateTimeInUNIX(FILE *fp){
  uint32_t n32;
  read32(&n32, fp);
  printf("StartDateTimeInUNIXSeconds::%u:\"%s\"\n", n32, mytime((time_t *)&n32));
  //printf("StartDateTimeInUNIXSeconds::%u\n", n32);
  read32(&n32, fp);
  printf("StartDateTimeInUNIXuSeconds::%u\n", n32);
}

void DataBlockGroupIntervalInUNIX(FILE *fp){
  uint32_t n32;
  read32(&n32, fp);
  printf("DataBlockGroupIntervalInUNIXSeconds::%u\n", n32);
  read32(&n32, fp);
  printf("DataBlockGroupIntervalInUNIXuSeconds::%u\n", n32);
}

void metadata2check(FILE *fp){
  uint32_t n32;
  read32(&n32, fp);
  printf("SecondMetaBlockSize::%u\n", n32);
}

void HostDefElement(FILE *fp){
  uint16_t n16;
  char *s;
  int i, count;

  read16(&n16, fp);
  s = readvariable(n16, fp);
  i = count = 0;
  while(i < n16){
    /* IP */
    printf("HostDefElement:%d:%s", count, s);
    while(*s != '\0') {s++; i++;}
    s++; i++;
    /* FQDN */
    printf(":%s", s);
    while(*s != '\0') {s++; i++;}
    s++; i++;
    /* nick */
    printf(":%s", s);
    while(*s != '\0') {s++; i++;}
    s++; i++;
    /* community */
    printf(":%s\n", s);
    while(*s != '\0') {s++; i++;}
    s++; i++;
    count++;
  }
}

void OIDDefElement(FILE *fp){
  uint16_t n16;
  char *s;
  int i, count;

  read16(&n16, fp);
  s = readvariable(n16, fp);
  i = count = 0;
  while(i < n16){
    /* OID */
    printf("OIDDefElement:%d:%s", count, s);
    while(*s != '\0') {s++; i++;}
    s++; i++;
    /* nick */
    printf(":%s\n", s);
    while(*s != '\0') {s++; i++;}
    s++; i++;
    count++;
  }
}

void IntervalDefElement(FILE *fp){
  uint16_t n16;
  unsigned short m;
  unsigned long n;
  char *s;
  int i, count;

  read16(&n16, fp);
  s = readvariable(n16, fp);
  i = count = 0;
  while(i < n16){
    printf("IntervalDefElement:%d", count);
    /* host index */
    m = ntohs(*(uint16_t*)s);
    printf(":%d-", m);
    s = s + 2;
    /* oid index */
    m = ntohs(*(uint16_t*)s);
    printf("%d", m);
    s = s + 2;
    /* second */
    n = ntohl(*(uint32_t*)s);
    printf(":%lu", n);
    s = s + 4;
    /* microsecond */
    n = ntohl(*(uint32_t*)s);
    printf(".%.6lu\n", n);
    s = s + 4;
    i = i + 12;
    count++;
  }
}


struct dbgmap * add_dgbmaplist(struct dbgmap *dm, int count, char *s){
  struct dbgmap *now, *top;
  top = dm;
  now = (struct dbgmap*)calloc(1, sizeof(struct dbgmap));
  if(now == NULL){
    exit(2);
  }
  now->n = count;
  now->map = s;
  now->next = NULL;

  if(dm == NULL){
    return now;
  }
  /* add to tail */
  while(dm->next){
    dm = dm->next;
  }
  dm->next = now;
  return top;
}

struct dbgmap * DataBlockGroupElement(FILE *fp){
  uint16_t n16;
  unsigned short m;
  //unsigned long n;
  char *s;
  int i, j, count;
  struct dbgmap *dbgmap = NULL;

  read16(&n16, fp);
  s = readvariable(n16, fp);
  i = count = 0;
  while(i < n16){
    printf("DataBlockGroupElement");
    /* the number */
    m = ntohs(*(uint16_t*)s);
    printf(":%u", m);
    s = s + 2;
    count = m;
    i = i + 2;
    dbgmap = add_dgbmaplist(dbgmap, count, s);
    /*  */
    j = 0;
    while(j < count){
      /* host index */
      //m = ntohs(*(uint16_t*)s);
      m = *(uint16_t*)s = ntohs(*(uint16_t*)s);
      printf(":%u", m);
      s = s + 2;
      /* oid index */
      //m = ntohs(*(uint16_t*)s);
      m = *(uint16_t*)s = ntohs(*(uint16_t*)s);
      printf("-%u", m);
      s = s + 2;
      i = i + 4;
      j++;
    }
    printf("\n");
  }
  return dbgmap;
}

int datablock_count = 0;

int DataBlock(FILE *fp, struct dbgmap *dm){
  uint32_t n32;
  int i;
  unsigned short hostid, oidid;
  char *s;

  s = dm->map;

  if(dm->n == 0){
    printf("# DataBlock %d\n", datablock_count++);
    return 2;
  }
  read32(&n32, fp); 	 /* EOF -> EXIT */
  printf("# DataBlock %d\n", datablock_count++);
  printf("TimeStampInSeconds::%u:\"%s\"\n", n32, mytime((time_t *)&n32));
  read32(&n32, fp);
  printf("TimeStampInuSeconds::%u\n", n32);

  //printf("%d\n", dm->n);
  i = 0;
  while(i < dm->n){
    hostid = *(uint16_t*)s;
    s = s + 2;
    oidid = *(uint16_t*)s;
    s = s + 2;
    printf("MeasurementData:%u-%u", hostid, oidid);
    print_bit(fp);
    read32(&n32, fp);
    printf(":%u\n", n32);
    i++;
  }

  return 1;
}

/* main */
int main(int argc, char **argv){
  FILE *fp;
  struct dbgmap *dbgmap, *dbgmaptop;
  //uint32_t n32;
  //uint16_t n16;

  if(argc != 2){
    printf("%s [file(.glg)]\n", argv[0]);
    exit(1);
  }

  fp = fopen(argv[1], "r");
  if(fp == NULL){
    printf("Error: cannot open %s\n", argv[1]);
  }

  /* metadata1 */
  print_blockname("First Meta Block");
  metadata1check(fp);
  VersionOfFormat(fp);
  DataBlockGroupSize(fp);
  StartDateTimeInUNIX(fp);
  DataBlockGroupIntervalInUNIX(fp);

  /* metadata2 */
  print_blockname("Second Meta Block");
  metadata2check(fp);
  print_blockname("HostDefBlock");
  HostDefElement(fp);
  print_blockname("OIDDefBlock");
  OIDDefElement(fp);
  print_blockname("IntervalDefBlock");
  IntervalDefElement(fp);
  print_blockname("DataBlockGroupTable");
  dbgmap = DataBlockGroupElement(fp);

  dbgmaptop = dbgmap;
  
#if 1
  /* data block */
  while(DataBlock(fp, dbgmap) > 0){
    dbgmap = dbgmap->next;
    if(dbgmap == NULL){
      dbgmap = dbgmaptop;
      //exit(0);
    }
  }
#endif

  return 0;
}
