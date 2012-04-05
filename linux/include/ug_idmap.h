/*
 *  include/linux/ug_idmap.h
 *
 *  Mapping of UID to name and vice versa.
 *
 */

#ifndef LINUX_UG_IDMAP_H
#define LINUX_UG_IDMAP_H

#define UG_IDMAP_NAMESZ 128
#ifdef __KERNEL__
int ug_map_name_to_uid(const char *, size_t, __u32 *);
int ug_map_name_to_gid(const char *, size_t, __u32 *);
int ug_map_uid_to_name(__u32, char *, size_t);
int ug_map_gid_to_name(__u32, char *, size_t);
int ug_idmap_init(void);
void ug_idmap_exit(void);

extern int ug_idmap_proc_init(void);
extern void ug_idmap_proc_cleanup(void);
extern unsigned int ug_timeout_sec;

#endif /* __KERNEL__ */

#endif /* LINUX_UG_IDMAP_H */
